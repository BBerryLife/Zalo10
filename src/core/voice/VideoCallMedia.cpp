#include "VideoCallMedia.hpp"

#include <bb/ImageData>
#include <bb/PixelFormat>
#include <bb/cascades/Image>

#include <QFile>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QStringList>
#include <QDebug>

#include <dlfcn.h>
#include <string.h>

namespace {
const int kReadCap     = 512 * 1024;   // the camera library's own per-frame buffer size
const int kRgbaCap     = 480 * 480 * 4; // decoder output is scaled to at most 480 px per side
// Frames waiting per stream before the backlog is dropped. Remote video arrives in bursts (network
// jitter, the engine's 12 ms send pacing), so give it room; the preview may just skip.
const int kRemoteQueue  = 14;
const int kPreviewQueue = 2;

// Annex B: call fn(type, start, end) for each NAL unit (start = first byte after the start code).
struct Nal { int type; int codeAt; int start; };

QList<Nal> scanNals(const QByteArray &a)
{
    QList<Nal> out;
    const unsigned char *p = (const unsigned char *)a.constData();
    const int n = a.size();
    for (int i = 0; i + 3 < n; ++i) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            Nal x;
            x.codeAt = (i > 0 && p[i - 1] == 0) ? i - 1 : i;
            x.start = i + 3;
            x.type = p[i + 3] & 0x1f;
            out.append(x);
            i += 2;
        }
    }
    return out;
}

QString hexHead(const QByteArray &a)
{
    return QString::fromLatin1(a.left(8).toHex());
}

void *resolve(void *lib, const char *name, QStringList &missing)
{
    void *f = lib ? dlsym(lib, name) : 0;
    if (!f) missing << QString::fromLatin1(name);
    return f;
}
}

// ---------------------------------------------------------------- VideoCallMedia

VideoCallMedia::VideoCallMedia(QObject *parent)
    : QObject(parent)
    , m_camLib(0), m_vidLib(0), m_loadTried(false), m_loaded(false)
    , m_camOpen(0), m_camRead(0), m_camWidth(0), m_camHeight(0), m_camStage(0), m_camClose(0)
    , m_cam(0), m_front(true)
    , m_decoder(0)
    , m_preview(0)
    , m_camFrames(0), m_camKeys(0), m_camBytes(0), m_camErrors(0), m_paramPrepends(0)
    , m_rxFrames(0), m_rxKeys(0), m_rxShown(0), m_rxErrors(0), m_localShown(0)
    , m_rxW(0), m_rxH(0), m_camW(0), m_camH(0)
    , m_loggedFirstCam(false), m_loggedFirstRx(false)
{
    m_poll.setInterval(30);
    connect(&m_poll, SIGNAL(timeout()), this, SLOT(pollCamera()));
}

VideoCallMedia::~VideoCallMedia()
{
    stop();
    VideoDecodeThread *threads[2] = { m_decoder, m_preview };
    for (int i = 0; i < 2; ++i) {
        if (!threads[i]) continue;
        threads[i]->stopThread();
        threads[i]->wait(2000);
        delete threads[i];
    }
    m_decoder = m_preview = 0;
    // The libraries stay loaded until exit: FFmpeg/camapi keep internal state we don't control.
}

bool VideoCallMedia::load(const QString &engineDir)
{
    if (m_loadTried) return m_loaded;
    m_loadTried = true;
    if (engineDir.isEmpty()) {
        qDebug() << "[Video] engine dir not found";
        return false;
    }
    const QString camPath = engineDir + "/libq10camera.so";
    const QString vidPath = engineDir + "/libq10video.so";
    m_camLib = dlopen(QFile::encodeName(camPath).constData(), RTLD_NOW | RTLD_LOCAL);
    if (!m_camLib) qDebug() << "[Video] dlopen camera failed:" << dlerror();
    m_vidLib = dlopen(QFile::encodeName(vidPath).constData(), RTLD_NOW | RTLD_LOCAL);
    if (!m_vidLib) qDebug() << "[Video] dlopen decoder failed:" << dlerror();

    QStringList missing;
    m_camOpen   = (CamOpenFn)  resolve(m_camLib, "q10_camera_open", missing);
    m_camRead   = (CamReadFn)  resolve(m_camLib, "q10_camera_read", missing);
    m_camWidth  = (CamIntFn)   resolve(m_camLib, "q10_camera_width", missing);
    m_camHeight = (CamIntFn)   resolve(m_camLib, "q10_camera_height", missing);
    m_camStage  = (CamStageFn) resolve(m_camLib, "q10_camera_last_stage", missing);
    m_camClose  = (CamCloseFn) resolve(m_camLib, "q10_camera_close", missing);
    VideoDecodeThread::DecOpenFn  dOpen  = (VideoDecodeThread::DecOpenFn)  resolve(m_vidLib, "q10_decoder_open", missing);
    VideoDecodeThread::DecFrameFn dFrame = (VideoDecodeThread::DecFrameFn) resolve(m_vidLib, "q10_decoder_frame", missing);
    VideoDecodeThread::DecCloseFn dClose = (VideoDecodeThread::DecCloseFn) resolve(m_vidLib, "q10_decoder_close", missing);

    if (dOpen && dFrame && dClose) {
        m_decoder = new VideoDecodeThread(0, kRemoteQueue, dOpen, dFrame, dClose);
        m_preview = new VideoDecodeThread(1, kPreviewQueue, dOpen, dFrame, dClose);
        VideoDecodeThread *threads[2] = { m_decoder, m_preview };
        for (int i = 0; i < 2; ++i) {
            connect(threads[i], SIGNAL(decoded(int,QByteArray,int,int)), this, SLOT(onDecoded(int,QByteArray,int,int)),
                    Qt::QueuedConnection);
            connect(threads[i], SIGNAL(decodeError(int)), this, SLOT(onDecodeError(int)), Qt::QueuedConnection);
        }
        // The engine (audio) and the UI also need CPU: the picture we watch at normal priority,
        // our own small preview only when nothing else wants the CPU.
        m_decoder->start(QThread::NormalPriority);
        m_preview->start(QThread::LowestPriority);
    }
    m_loaded = (m_decoder != 0);
    qDebug() << "[Video] libraries loaded: decoder=" << (m_decoder != 0)
             << "camera=" << (m_camOpen && m_camRead && m_camClose)
             << (missing.isEmpty() ? QString() : ("missing=" + missing.join(",")));
    return m_loaded;
}

void VideoCallMedia::start()
{
    m_camFrames = m_camKeys = m_camBytes = m_camErrors = m_paramPrepends = 0;
    m_rxFrames = m_rxKeys = m_rxShown = m_rxErrors = m_localShown = 0;
    m_rxW = m_rxH = m_camW = m_camH = 0;
    m_loggedFirstCam = m_loggedFirstRx = false;
    m_paramSets.clear();
    if (m_decoder) m_decoder->reset();
    if (m_preview) m_preview->reset();
}

void VideoCallMedia::stop()
{
    stopCamera();
    if (m_decoder) m_decoder->reset();
    if (m_preview) m_preview->reset();
    const bool hadRemote = m_remoteImage.isValid();
    const bool hadLocal = m_localImage.isValid();
    m_remoteImage = QVariant();
    m_localImage = QVariant();
    if (hadRemote) emit remoteImageChanged();
    if (hadLocal) emit localImageChanged();
}

QString VideoCallMedia::counters()
{
    return QString("cam_frames=%1 cam_keys=%2 cam_kbytes=%3 cam_errors=%4 cam_size=%5x%6 param_prepends=%7 "
                   "rx_frames=%8 rx_keys=%9 rx_shown=%10 rx_errors=%11 rx_size=%12x%13 preview_shown=%14")
        .arg(m_camFrames).arg(m_camKeys).arg(m_camBytes / 1024).arg(m_camErrors).arg(m_camW).arg(m_camH)
        .arg(m_paramPrepends).arg(m_rxFrames).arg(m_rxKeys).arg(m_rxShown).arg(m_rxErrors)
        .arg(m_rxW).arg(m_rxH).arg(m_localShown)
        + " remote_dec[" + (m_decoder ? m_decoder->counters() : QString()) + "]"
        + " preview_dec[" + (m_preview ? m_preview->counters() : QString()) + "]";
}

// ---------------------------------------------------------------- remote stream

void VideoCallMedia::pushRemote(bool key, const QByteArray &h264)
{
    if (!m_decoder || h264.isEmpty()) return;
    m_rxFrames++;
    if (key) m_rxKeys++;
    if (!m_loggedFirstRx) {
        m_loggedFirstRx = true;
        qDebug() << "[Video] first remote frame: bytes=" << h264.size() << "key=" << key << "head=" << hexHead(h264);
    }
    if (!m_decoder->push(key, h264))
        emit keyframeNeeded();
}

void VideoCallMedia::onDecoded(int which, const QByteArray &rgba, int w, int h)
{
    if (w <= 0 || h <= 0 || rgba.size() < w * h * 4) return;
    bb::ImageData img(bb::PixelFormat::RGBA_Premultiplied, w, h);   // alpha is always 0xff
    unsigned char *dst = img.pixels();
    if (!dst) return;
    const int bpl = img.bytesPerLine();
    const char *src = rgba.constData();
    for (int y = 0; y < h; ++y)
        memcpy(dst + y * bpl, src + y * w * 4, w * 4);
    QVariant v = QVariant::fromValue(bb::cascades::Image(img));
    if (which == 0) {
        if (m_rxShown == 0) qDebug() << "[Video] first remote picture" << w << "x" << h;
        m_rxShown++;
        m_rxW = w; m_rxH = h;
        m_remoteImage = v;
        emit remoteImageChanged();
    } else {
        if (m_localShown == 0) qDebug() << "[Video] first preview picture" << w << "x" << h;
        m_localShown++;
        m_localImage = v;
        emit localImageChanged();
    }
}

void VideoCallMedia::onDecodeError(int which)
{
    if (which == 0) {
        if (m_rxErrors++ < 3) qDebug() << "[Video] remote decode error";
        emit keyframeNeeded();
    }
}

// ---------------------------------------------------------------- camera

bool VideoCallMedia::startCamera(bool front)
{
    if (m_cam) return true;
    if (!m_camOpen || !m_camRead || !m_camClose) return false;
    void *cam = 0;
    int rc = m_camOpen(&cam, front ? 1 : 0, 1);
    if (rc != 0 || !cam) {
        m_camErrors++;
        qDebug() << "[Video] camera open failed rc=" << rc << "stage=" << (m_camStage ? m_camStage() : -1)
                 << "front=" << front;
        if (cam) m_camClose(cam);
        return false;
    }
    m_cam = cam;
    m_front = front;
    m_camW = m_camWidth ? m_camWidth(cam) : 0;
    m_camH = m_camHeight ? m_camHeight(cam) : 0;
    qDebug() << "[Video] camera on, front=" << front << "size=" << m_camW << "x" << m_camH;
    m_paramSets.clear();
    if (m_readBuf.size() != kReadCap) m_readBuf.resize(kReadCap);
    m_poll.start();
    emit cameraChanged();
    return true;
}

void VideoCallMedia::stopCamera()
{
    m_poll.stop();
    if (!m_cam) return;
    m_camClose(m_cam);
    m_cam = 0;
    if (m_localImage.isValid()) {
        m_localImage = QVariant();
        emit localImageChanged();
    }
    emit cameraChanged();
}

// SPS/PPS may arrive in their own buffer (codec config) or only with the first key frame. The peer
// can only start decoding at a key frame that has them, so keep the latest set and put it in front
// of every IDR frame that lacks it. Returns an empty array for a config-only buffer.
QByteArray VideoCallMedia::withParameterSets(const QByteArray &frame, bool *key)
{
    const QList<Nal> nals = scanNals(frame);
    bool hasSps = false, hasIdr = false, hasSlice = false;
    int firstSliceAt = -1;
    for (int i = 0; i < nals.size(); ++i) {
        const int t = nals[i].type;
        if (t == 7) hasSps = true;
        if (t == 5) hasIdr = true;
        if (t >= 1 && t <= 5) {
            hasSlice = true;
            if (firstSliceAt < 0) firstSliceAt = nals[i].codeAt;
        }
    }
    if (nals.isEmpty()) {                         // not Annex B: pass through with the library's flag
        return frame;
    }
    if (hasSps) m_paramSets = (firstSliceAt >= 0) ? frame.left(firstSliceAt) : frame;
    if (!hasSlice) return QByteArray();
    *key = *key || hasIdr;
    if (hasIdr && !hasSps && !m_paramSets.isEmpty()) {
        m_paramPrepends++;
        return m_paramSets + frame;
    }
    return frame;
}

void VideoCallMedia::pollCamera()
{
    if (!m_cam) return;
    for (int n = 0; n < 4; ++n) {                 // the library queues up to 4 frames
        long long ts = 0;
        int key = 0;
        int len = m_camRead(m_cam, m_readBuf.data(), (unsigned)m_readBuf.size(), &ts, &key);
        if (len == 0) return;
        if (len < 0) {
            if (m_camErrors++ < 3) qDebug() << "[Video] camera read error" << len;
            return;
        }
        QByteArray frame(m_readBuf.constData(), len);
        if (!m_loggedFirstCam) {
            m_loggedFirstCam = true;
            qDebug() << "[Video] first camera frame: bytes=" << len << "key=" << key << "head=" << hexHead(frame);
        }
        bool isKey = (key != 0);
        QByteArray out = withParameterSets(frame, &isKey);
        if (out.isEmpty()) continue;
        m_camFrames++;
        m_camBytes += out.size();
        if (isKey) m_camKeys++;
        emit localFrameEncoded(isKey, out);
        if (m_preview) m_preview->push(isKey, out);
    }
}

// ---------------------------------------------------------------- decode thread

VideoDecodeThread::VideoDecodeThread(int which, int maxQueue, DecOpenFn open, DecFrameFn frame, DecCloseFn close,
                                     QObject *parent)
    : QThread(parent), m_which(which), m_maxQueue(maxQueue), m_open(open), m_frame(frame), m_close(close)
    , m_quit(false), m_reset(false), m_waitKey(true)
    , m_decoded(0), m_dropped(0), m_overflows(0), m_msTotal(0), m_msMax(0)
{
}

VideoDecodeThread::~VideoDecodeThread()
{
    stopThread();
    wait(2000);
}

bool VideoDecodeThread::push(bool key, const QByteArray &h264)
{
    QMutexLocker lock(&m_mutex);
    if (m_waitKey) {
        if (!key) { m_dropped++; return false; }  // still waiting: ask (again) for a key frame
        m_waitKey = false;
    }
    if (key && !m_jobs.isEmpty()) {
        // A key frame makes everything queued before it unnecessary: catch up at once.
        m_dropped += m_jobs.size();
        m_jobs.clear();
    } else if (m_jobs.size() >= m_maxQueue) {
        // Too slow to keep up: drop the backlog and restart at the next key frame.
        m_overflows++;
        m_dropped += m_jobs.size() + 1;
        m_jobs.clear();
        m_waitKey = true;
        return false;
    }
    Job j; j.key = key; j.data = h264;
    m_jobs.append(j);
    m_wake.wakeOne();
    return true;
}

void VideoDecodeThread::reset()
{
    QMutexLocker lock(&m_mutex);
    m_jobs.clear();
    m_reset = true;
    m_waitKey = true;
    m_decoded = m_dropped = m_overflows = m_msTotal = m_msMax = 0;
    m_wake.wakeOne();
}

void VideoDecodeThread::stopThread()
{
    QMutexLocker lock(&m_mutex);
    m_quit = true;
    m_jobs.clear();
    m_wake.wakeOne();
}

QString VideoDecodeThread::counters()
{
    QMutexLocker lock(&m_mutex);
    return QString("decoded=%1 dropped=%2 overflows=%3 avg_ms=%4 max_ms=%5")
        .arg(m_decoded).arg(m_dropped).arg(m_overflows)
        .arg(m_decoded > 0 ? m_msTotal / m_decoded : 0).arg(m_msMax);
}

void VideoDecodeThread::run()
{
    void *dec = 0;
    QByteArray rgba(kRgbaCap, '\0');
    QElapsedTimer timer;
    for (;;) {
        Job job;
        bool doReset = false;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_quit && !m_reset && m_jobs.isEmpty())
                m_wake.wait(&m_mutex);
            if (m_quit) break;
            if (m_reset) {
                m_reset = false;
                doReset = true;
            } else {
                job = m_jobs.takeFirst();
            }
        }
        if (doReset) {
            if (dec) { m_close(dec); dec = 0; }
            continue;
        }
        if (!dec) dec = m_open();
        if (!dec) { emit decodeError(m_which); continue; }
        int w = 0, h = 0;
        timer.start();
        int n = m_frame(dec, job.data.constData(), job.data.size(), rgba.data(), rgba.size(), &w, &h);
        const int ms = (int)timer.elapsed();
        {
            QMutexLocker lock(&m_mutex);
            m_decoded++;
            m_msTotal += ms;
            if (ms > m_msMax) m_msMax = ms;
        }
        if (n > 0 && w > 0 && h > 0 && n >= w * h * 4) {
            emit decoded(m_which, QByteArray(rgba.constData(), w * h * 4), w, h);
        } else if (n < 0) {
            // The decoder may be left in a bad state: reopen it and wait for a key frame.
            m_close(dec);
            dec = 0;
            {
                QMutexLocker lock(&m_mutex);
                m_waitKey = true;
            }
            emit decodeError(m_which);
        }
    }
    if (dec) m_close(dec);
}
