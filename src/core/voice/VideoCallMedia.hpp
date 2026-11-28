#ifndef VIDEOCALLMEDIA_HPP
#define VIDEOCALLMEDIA_HPP

// Video for 1:1 Zalo calls, on the UI side of the voice engine (voice/phone_engine.py).
//
// The engine already does the video signalling and the RTP/SRTP H.264 transport: it emits every
// received access unit as a "remote_video" event and accepts our camera's access units as
// "video_frame" actions. This class does the two device-side parts it leaves to the UI:
//
//  - camera: voice/libq10camera.so (camapi viewfinder + hardware H.264 encoder) is polled and each
//    encoded frame is handed to the bridge (localFrameEncoded) for the engine;
//  - decoding: voice/libq10video.so (FFmpeg H.264) turns received frames, and our own frames for the
//    small preview, into RGBA (at most 480 px per side) on a worker thread; the result is exposed to
//    QML as a bb::cascades::Image.
//
// Both libraries ship without headers. Their C signatures (reconstructed from the binaries):
//   int  q10_camera_open(void **cam, int front, int encode);           // 0 or -errno
//   int  q10_camera_read(void *cam, void *buf, unsigned cap, long long *ts, int *key);
//                                                                       // bytes, 0 = none yet, <0 error
//   int  q10_camera_width(void *cam), q10_camera_height(void *cam), q10_camera_last_stage(void);
//   void q10_camera_close(void *cam);
//   void *q10_decoder_open(void);
//   int  q10_decoder_frame(void *dec, const void *h264, int len, void *rgba, int cap, int *w, int *h);
//                                                                       // RGBA bytes, 0 = no picture yet
//   void q10_decoder_close(void *dec);

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QByteArray>
#include <QList>
#include <QTimer>
#include <QVariant>
#include <QString>

class VideoDecodeThread;

class VideoCallMedia : public QObject
{
    Q_OBJECT
public:
    explicit VideoCallMedia(QObject *parent = 0);
    virtual ~VideoCallMedia();

    // dlopen the two libraries from the engine directory (once). False if either is unusable.
    bool load(const QString &engineDir);

    void start();                       // a video call is connected
    void stop();                        // call ended: camera off, decoders closed, frames cleared

    void pushRemote(bool key, const QByteArray &h264);

    bool startCamera(bool front);
    void stopCamera();
    bool cameraOn() const { return m_cam != 0; }
    bool frontCamera() const { return m_front; }

    QVariant remoteImage() const { return m_remoteImage; }
    QVariant localImage() const { return m_localImage; }
    bool hasRemote() const { return m_remoteImage.isValid(); }

    QString counters();                 // numbers only, for the call-end log line

signals:
    void localFrameEncoded(bool key, const QByteArray &h264);
    void remoteImageChanged();
    void localImageChanged();
    void keyframeNeeded();              // remote stream broke: ask the peer for a new key frame
    void cameraChanged();

private slots:
    void pollCamera();
    void onDecoded(int which, const QByteArray &rgba, int w, int h);
    void onDecodeError(int which);

private:
    QByteArray withParameterSets(const QByteArray &frame, bool *key);

    typedef int  (*CamOpenFn)(void **, int, int);
    typedef int  (*CamReadFn)(void *, void *, unsigned, long long *, int *);
    typedef int  (*CamIntFn)(void *);
    typedef int  (*CamStageFn)(void);
    typedef void (*CamCloseFn)(void *);

    void *m_camLib;
    void *m_vidLib;
    bool  m_loadTried;
    bool  m_loaded;
    CamOpenFn  m_camOpen;
    CamReadFn  m_camRead;
    CamIntFn   m_camWidth;
    CamIntFn   m_camHeight;
    CamStageFn m_camStage;
    CamCloseFn m_camClose;

    void      *m_cam;
    bool       m_front;
    QTimer     m_poll;
    QByteArray m_readBuf;
    QByteArray m_paramSets;             // last SPS/PPS seen from the encoder (Annex B)

    VideoDecodeThread *m_decoder;       // remote stream
    VideoDecodeThread *m_preview;       // our own stream
    QVariant m_remoteImage;
    QVariant m_localImage;

    // diagnostics
    int m_camFrames, m_camKeys, m_camBytes, m_camErrors, m_paramPrepends;
    int m_rxFrames, m_rxKeys, m_rxShown, m_rxErrors, m_localShown;
    int m_rxW, m_rxH, m_camW, m_camH;
    bool m_loggedFirstCam, m_loggedFirstRx;
};

// One worker thread and one FFmpeg decoder per stream: 0 = remote, 1 = our own (preview). Separate
// threads so the preview (low priority) never delays the other person's picture.
class VideoDecodeThread : public QThread
{
    Q_OBJECT
public:
    typedef void *(*DecOpenFn)(void);
    typedef int   (*DecFrameFn)(void *, const void *, int, void *, int, int *, int *);
    typedef void  (*DecCloseFn)(void *);

    VideoDecodeThread(int which, int maxQueue, DecOpenFn open, DecFrameFn frame, DecCloseFn close, QObject *parent = 0);
    virtual ~VideoDecodeThread();

    // Returns false when the frame could not be used (queue overflow, or still waiting for a key
    // frame after one): the caller should ask for a new key frame.
    bool push(bool key, const QByteArray &h264);
    void reset();                       // drop queued work, reopen the decoder, clear the counters
    void stopThread();
    QString counters();                 // decoded / dropped / decode time

signals:
    void decoded(int which, const QByteArray &rgba, int w, int h);
    void decodeError(int which);

protected:
    virtual void run();

private:
    struct Job { bool key; QByteArray data; };
    int        m_which;
    int        m_maxQueue;
    DecOpenFn  m_open;
    DecFrameFn m_frame;
    DecCloseFn m_close;
    QMutex m_mutex;
    QWaitCondition m_wake;
    QList<Job> m_jobs;
    bool m_quit;
    bool m_reset;
    bool m_waitKey;                     // after a drop, skip until the next key frame
    int  m_decoded, m_dropped, m_overflows, m_msTotal, m_msMax;
};

#endif
