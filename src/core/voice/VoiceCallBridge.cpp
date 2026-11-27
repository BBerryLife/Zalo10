#include "VoiceCallBridge.hpp"
#include "../ZaloService.hpp"
#include "../../utils/json/ZaloMessageJson.hpp"

#include <bb/cascades/Application>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>

#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QUrl>
#include <QDebug>

namespace {
const int kMaxFrame = 1024 * 1024;     // engine frames are small; bound the buffer anyway
const int kMaxConnectTries = 25;       // 25 * 300 ms: python3.2 + session_probe start slowly

// BB10 app layout: <appdata>/<id>/{app/native, data, tmp}. Depending on how the app was started the
// working directory is either <id> (seen on device) or <id>/app/native, so don't assume one.
QString appRoot()
{
    QString cwd = QDir::cleanPath(QDir::currentPath());
    if (QFile::exists(cwd + "/app/native"))
        return cwd;
    const QString tail = "/app/native";
    if (cwd.endsWith(tail))
        return cwd.left(cwd.size() - tail.size());
    QString exe = QDir::cleanPath(QCoreApplication::applicationDirPath());   // .../app/native
    if (exe.endsWith(tail))
        return exe.left(exe.size() - tail.size());
    return cwd;
}
}

VoiceCallBridge::VoiceCallBridge(ZaloService *service, QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_socket(new QLocalSocket(this))
    , m_process(0)
    , m_state("idle")
    , m_muted(false)
    , m_engineReady(false)
    , m_wasIncoming(false)
    , m_appVisible(true)
    , m_sheetOpen(false)
    , m_invoke(new bb::system::InvokeManager(this))
    , m_awaitingDial(false)
    , m_connectTries(0)
    , m_readyFalse(0)
    , m_lastFailMs(0)
    , m_connectedAtMs(0)
{
    connect(m_socket, SIGNAL(connected()),    this, SLOT(onEngineConnected()));
    connect(m_socket, SIGNAL(disconnected()), this, SLOT(onEngineDisconnected()));
    connect(m_socket, SIGNAL(readyRead()),    this, SLOT(onEngineData()));

    m_retry.setSingleShot(true);
    m_retry.setInterval(300);
    connect(&m_retry, SIGNAL(timeout()), this, SLOT(connectEngine()));

    m_dialTimer.setSingleShot(true);
    m_dialTimer.setInterval(40000);   // engine waits up to 30 s for the native API helper
    connect(&m_dialTimer, SIGNAL(timeout()), this, SLOT(onDialTimeout()));

    m_tick.setInterval(1000);
    connect(&m_tick, SIGNAL(timeout()), this, SLOT(onTick()));

    bb::cascades::Application *app = bb::cascades::Application::instance();
    if (app) {
        connect(app, SIGNAL(fullscreen()), this, SLOT(onAppFullscreen()));
        connect(app, SIGNAL(thumbnail()),  this, SLOT(onAppHidden()));
        connect(app, SIGNAL(invisible()),  this, SLOT(onAppHidden()));
    }

    if (m_service) {
        connect(m_service, SIGNAL(voiceCallControl(QVariantMap)), this, SLOT(control(QVariantMap)));
        connect(m_service, SIGNAL(avatarReady(QString,QString)), this, SLOT(onAvatarReady(QString,QString)));
    }
}

VoiceCallBridge::~VoiceCallBridge()
{
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        QVariantMap quit; quit["action"] = "quit";
        sendFrame(quit);
        m_socket->flush();
    }
}

// ---------------------------------------------------------------- paths

QString VoiceCallBridge::socketPath() const
{
    // The engine only accepts <appdata>/com.BerryLife.Zalo10.*/data/zaloone-engine.sock.
    return appRoot() + "/data/zaloone-engine.sock";
}

QString VoiceCallBridge::peerAvatar() const
{
    return m_service ? m_service->localAvatarFor(m_peerId) : QString();
}

// The caller's avatar may finish downloading while the call screen is already up.
void VoiceCallBridge::onAvatarReady(const QString &threadId, const QString &localPath)
{
    Q_UNUSED(localPath);
    if (!m_peerId.isEmpty() && threadId == m_peerId) emit changed();
}

QString VoiceCallBridge::engineDir() const
{
    const QString root = appRoot();
    const QString candidates[] = {
        root + "/app/native/voice",
        QDir::cleanPath(QCoreApplication::applicationDirPath()) + "/voice",
        QDir::cleanPath(QDir::currentPath()) + "/voice"
    };
    for (int i = 0; i < 3; ++i)
        if (QFile::exists(candidates[i] + "/phone_engine.py"))
            return candidates[i];
    return QString();
}

// ---------------------------------------------------------------- engine lifecycle

void VoiceCallBridge::ensureRunning() { startEngine(true); }

// force: user-initiated (dial). Background triggers (incoming control frames) respect a 30 s
// back-off after a failed start, so a missing/broken engine can't spin or spam the UI.
void VoiceCallBridge::startEngine(bool force)
{
    if (m_socket->state() == QLocalSocket::ConnectedState || m_retry.isActive())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_lastFailMs > 0 && now - m_lastFailMs < 30000)
        return;
    m_connectTries = 0;
    connectEngine();
}

void VoiceCallBridge::connectEngine()
{
    if (m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    m_socket->connectToServer(socketPath());
    if (!m_socket->waitForConnected(150)) {
        m_socket->abort();
        if (m_connectTries == 0) {
            startEngineProcess();
            // Nothing to wait for if the engine can't even be launched on this device.
            if (engineDir().isEmpty() || !QFile::exists("/usr/bin/python3.2")) {
                m_lastFailMs = QDateTime::currentMSecsSinceEpoch();
                failDial("engine_error");
                return;
            }
        }
        if (++m_connectTries < kMaxConnectTries) {
            m_retry.start();
        } else {
            m_engineReady = false;
            m_lastFailMs = QDateTime::currentMSecsSinceEpoch();
            qDebug() << "[Voice] engine did not come up; socket=" << socketPath()
                     << "dir=" << engineDir() << "sockErr=" << m_socket->errorString();
            failDial("engine_error");   // also drops queued frames so a stale dial can't fire later
        }
    }
}

void VoiceCallBridge::startEngineProcess()
{
    QString dir = engineDir();
    if (dir.isEmpty()) {
        qDebug() << "[Voice] engine files not found; cwd=" << QDir::currentPath()
                 << "appDir=" << QCoreApplication::applicationDirPath() << "root=" << appRoot();
        return;
    }
    if (!QFile::exists("/usr/bin/python3.2")) {
        qDebug() << "[Voice] /usr/bin/python3.2 not found";
        return;
    }
    if (m_process && m_process->state() != QProcess::NotRunning)
        return;

    QSettings s("BerryLife", "Zalo10");
    s.sync();
    const QString settings = s.fileName();

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(onProcessFinished()));
        connect(m_process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(onProcessError()));
        connect(m_process, SIGNAL(readyRead()), this, SLOT(onProcessOutput()));
    }
    // Same environment as the reference build: Python 3.2 from the OS image, engine dir on the path.
    const char *script =
        "export PYTHONHOME=/usr Q10_CA_FILE=\"$1/q10-ca.pem\" "
        "PYTHONPATH=\"$1:/usr/lib/python32.zip:/usr/lib/python3.2/lib-dynload\" "
        "LD_LIBRARY_PATH=/usr/lib/qt4/lib:/base/usr/lib:/base/lib:/base/lib/dll:/proc/boot; "
        "exec /usr/bin/python3.2 \"$1/phone_engine.py\" --settings \"$2\" --socket \"$3\"";
    QStringList args;
    args << "-c" << QString::fromLatin1(script) << "sh" << dir << settings << socketPath();
    qDebug() << "[Voice] starting engine, dir=" << dir << "socket=" << socketPath();
    m_process->start("/bin/sh", args);
}

void VoiceCallBridge::onProcessError()
{
    qDebug() << "[Voice] engine process error:" << m_process->errorString();
}

void VoiceCallBridge::onProcessOutput()
{
    // Python tracebacks etc. Never contains call media/session data (engine logs none).
    QByteArray out = m_process->readAll();
    if (out.size() > 600) out.truncate(600);
    qDebug() << "[Voice] engine:" << QString::fromUtf8(out.constData(), out.size()).trimmed();
}

void VoiceCallBridge::onProcessFinished()
{
    qDebug() << "[Voice] engine process exited, code=" << (m_process ? m_process->exitCode() : -1);
    logEngineTrace();
}

void VoiceCallBridge::onEngineConnected()
{
    m_buffer.clear();
    m_retry.stop();
    if (m_sheetOpen && m_appVisible) pushForeground();
}

void VoiceCallBridge::onEngineDisconnected()
{
    m_engineReady = false;
    m_buffer.clear();
    if (m_state != "idle") {
        m_state = "idle";
        m_tick.stop();
        emit callEnded(QString::fromLatin1("engine_exited"), 0);
    }
    emit changed();
}

// ---------------------------------------------------------------- engine I/O

void VoiceCallBridge::sendFrame(const QVariantMap &frame)
{
    if (m_socket->state() != QLocalSocket::ConnectedState)
        return;
    // variantToJsonCompact (recursive, single line). Not mapToJson(): that one is flat and turns
    // nested maps such as the call "content" into "", which crashed the engine mid-call.
    QByteArray line = variantToJsonCompact(QVariant(frame));
    line.append('\n');
    m_socket->write(line);
}

void VoiceCallBridge::flushPending()
{
    while (!m_pending.isEmpty() && m_engineReady)
        sendFrame(m_pending.takeFirst());
}

void VoiceCallBridge::onEngineData()
{
    m_buffer.append(m_socket->readAll());
    if (m_buffer.size() > kMaxFrame) {          // malformed / hostile peer: reset
        m_buffer.clear();
        m_socket->abort();
        return;
    }
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        applyEvent(jsonToMap(line));
    }
}

void VoiceCallBridge::applyEvent(const QVariantMap &event)
{
    const QString kind = event.value("event").toString();
    if (kind == "ready") {
        m_engineReady = event.value("voice_service").toBool();
        qDebug() << "[Voice] engine ready, voice_service=" << m_engineReady;
        if (m_engineReady) {
            m_readyFalse = 0;
            flushPending();
        } else if (++m_readyFalse >= 2) {
            // The first "false" is the engine greeting us while its native helper is still booting;
            // a second one means the helper itself reported voice as unavailable.
            qDebug() << "[Voice] native helper reports voice unavailable";
            logEngineTrace();
            failDial("engine_error");
        }
        emit changed();
    } else if (kind == "open_ui") {
        bringToFront();
    } else if (kind == "state") {
        applyState(event);
    } else if (kind == "error") {
        qDebug() << "[Voice] engine error:" << event.value("reason").toString()
                 << "kind=" << event.value("kind").toString()
                 << "where=" << event.value("where").toString()
                 << "stage=" << event.value("stage").toString()
                 << "code=" << event.value("code").toString();
        logEngineTrace();
        emit engineError(event.value("reason").toString());
        failDial("engine_error");
    }
    // api_result / media_preparing / call_control / alert_status are diagnostics: ignored.
}

void VoiceCallBridge::applyState(const QVariantMap &event)
{
    const QString st = event.value("state").toString();
    const QString reason = event.value("reason").toString();
    qDebug() << "[Voice] state" << st << "reason=" << reason;
    if (st == "idle" && event.contains("media_counts")) {
        // Per-call audio counters from the engine (packets, concealed frames, mic/speaker levels,
        // xruns, active tuning). Numbers only; no ids or audio.
        const QVariantMap mc = event.value("media_counts").toMap();
        QStringList parts;
        for (QVariantMap::const_iterator it = mc.constBegin(); it != mc.constEnd(); ++it)
            parts << (it.key() + "=" + it.value().toString());
        qDebug() << "[Voice] media:" << parts.join(" ");
    }

    if (st == "incoming") {
        m_peerId = event.value("peer").toString();
        m_peerName = event.value("name").toString();
        if (m_peerName.isEmpty()) m_peerName = QString::fromLatin1("Zalo");
        m_wasIncoming = true;
        qDebug() << "[Voice] incoming peer=" << m_peerId << "avatar=" << peerAvatar();
    }
    // On connect the engine replays its (idle) state right after "ready"; that must not cancel a
    // dial we have just queued. A real end-of-call idle always carries a reason.
    if (m_awaitingDial) {
        if (st == "idle" && reason.isEmpty()) return;
        m_awaitingDial = false;
        m_dialTimer.stop();
    }
    const QString previous = m_state;
    m_state = st.isEmpty() ? QString::fromLatin1("idle") : st;

    if (m_state == "connected" && previous != "connected") {
        m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_tick.start();
    }
    if (m_state == "idle") {
        int secs = 0;
        if (m_connectedAtMs > 0)
            secs = int((QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs) / 1000);
        m_tick.stop();
        m_connectedAtMs = 0;
        m_muted = false;
        m_lastReason = reason;
        const bool wasActive = (previous != "idle");
        m_wasIncoming = false;
        emit changed();
        if (wasActive) emit callEnded(reason, secs);
        return;
    }
    emit changed();
    emit durationChanged();
}

void VoiceCallBridge::onTick() { emit durationChanged(); }

void VoiceCallBridge::onDialTimeout()
{
    qDebug() << "[Voice] dial timed out waiting for the engine";
    logEngineTrace();
    failDial("engine_error");
}

// The engine keeps a small privacy-safe trace (event names, error codes; no ids or media) next to
// its socket. Replaying its tail into the app log is how we see why a call never started.
void VoiceCallBridge::logEngineTrace()
{
    QFile f(QFileInfo(socketPath()).absolutePath() + "/zaloone-call-events");
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Voice] no engine trace file";
        return;
    }
    if (f.size() > 3000) f.seek(f.size() - 3000);
    const QList<QByteArray> lines = f.readAll().split('\n');
    const int from = lines.size() > 16 ? lines.size() - 16 : 0;
    for (int i = from; i < lines.size(); ++i)
        if (!lines.at(i).isEmpty())
            qDebug() << "[Voice] trace:" << QString::fromUtf8(lines.at(i).constData(), lines.at(i).size());
}

void VoiceCallBridge::failDial(const QString &reason)
{
    m_pending.clear();
    m_dialTimer.stop();
    m_awaitingDial = false;
    if (m_state != "idle") {
        m_state = "idle";
        m_lastReason = reason;
        emit changed();
        emit callEnded(reason, 0);
    }
}

// ---------------------------------------------------------------- QML API

void VoiceCallBridge::dial(const QString &peer, const QString &name)
{
    if (m_state != "idle" || peer.isEmpty()) return;
    for (int i = 0; i < peer.size(); ++i)
        if (!peer.at(i).isDigit()) return;          // engine also rejects non-numeric ids

    m_peerId = peer;
    m_peerName = name.isEmpty() ? QString::fromLatin1("Zalo") : name;
    m_state = "calling";                              // optimistic; engine confirms
    m_awaitingDial = true;
    m_dialTimer.start();
    m_lastReason.clear();
    emit changed();

    QVariantMap frame;
    frame["action"] = "call";
    frame["peer"] = peer;
    frame["name"] = m_peerName;
    frame["video"] = false;
    m_pending.append(frame);
    startEngine(true);
    flushPending();
}

void VoiceCallBridge::answer()
{
    QVariantMap f; f["action"] = "answer";
    sendFrame(f);
}

void VoiceCallBridge::hangup()
{
    if (m_state == "idle") return;
    for (int i = m_pending.size() - 1; i >= 0; --i)
        if (m_pending.at(i).value("action").toString() == "call") m_pending.removeAt(i);
    m_awaitingDial = false;
    m_dialTimer.stop();
    QVariantMap f; f["action"] = "hangup";
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        sendFrame(f);
    } else {                                          // dial never reached the engine
        m_pending.clear();
        m_state = "idle";
        emit changed();
        emit callEnded(QString::fromLatin1("local_hangup"), 0);
    }
}

void VoiceCallBridge::setMuted(bool muted)
{
    if (m_muted == muted) return;
    m_muted = muted;
    QVariantMap f; f["action"] = "mute"; f["muted"] = muted;
    sendFrame(f);
    emit changed();
}

void VoiceCallBridge::setForeground(bool foreground)
{
    m_sheetOpen = foreground;
    pushForeground();
}

// The engine counts the call screen as "in front" only while the sheet is open AND the app is visible;
// a sheet that opened inside a minimized app must not silence the engine.
void VoiceCallBridge::pushForeground()
{
    QVariantMap f; f["action"] = "ui_foreground"; f["foreground"] = (m_sheetOpen && m_appVisible);
    sendFrame(f);
}

void VoiceCallBridge::onAppFullscreen()
{
    m_appVisible = true;
    pushForeground();
}

void VoiceCallBridge::onAppHidden()
{
    m_appVisible = false;
    pushForeground();
}

// Engine asked for the call screen (incoming call while minimized or locked): OPEN invoke on our own
// target, the same way a Hub tap brings the app up. onInvoked() ignores the "zalo10:call" marker.
void VoiceCallBridge::bringToFront()
{
    if (m_appVisible) return;
    qDebug() << "[Voice] bringing the app to the front";
    bb::system::InvokeRequest req;
    req.setTarget("com.BerryLife.Zalo10.invoke");
    req.setAction("bb.action.OPEN");
    req.setMimeType("application/vnd.zalo10.hub.chat");
    req.setUri(QUrl("pim:application/vnd.zalo10.hub.chat"));
    req.setData(QByteArray("zalo10:call"));
    bb::system::InvokeTargetReply *reply = m_invoke->invoke(req);
    if (reply) connect(reply, SIGNAL(finished()), reply, SLOT(deleteLater()));
}

void VoiceCallBridge::control(const QVariantMap &content)
{
    // WS frame -> engine. Engine ignores stale/foreign call ids; we only bound what we forward.
    const QString act = content.value("act").toString();
    qDebug() << "[Voice] control act=" << act;
    if (act != "request" && act != "cancel" && act != "end_call" && act != "reject" &&
        act != "busy" && act != "answer" && act != "answer_ack" && act != "ring_ring")
        return;
    // Only a new call ("request") is worth launching the engine for; cancel/end/busy for a call
    // we never saw would just start Python for nothing.
    if (m_socket->state() != QLocalSocket::ConnectedState && act != "request")
        return;
    QVariantMap f;
    f["action"] = "control";
    f["content"] = content;
    m_pending.append(f);
    startEngine(false);
    flushPending();
}

// ---------------------------------------------------------------- presentation

QString VoiceCallBridge::reasonText(const QString &r)
{
    if (r == "peer_reject" || r == "recipient_declined") return QString::fromLatin1("Call declined");
    if (r == "peer_busy")                                 return QString::fromLatin1("User is busy");
    if (r == "peer_cancel")                               return QString::fromLatin1("Call cancelled");
    if (r == "peer_end_call" || r == "peer_ended")        return QString::fromLatin1("Call ended");
    if (r == "timeout")                                   return QString::fromLatin1("No answer");
    if (r == "microphone_or_speaker_unavailable")         return QString::fromLatin1("Microphone unavailable");
    if (r == "answered_on_another_device")                return QString::fromLatin1("Answered on another device");
    if (r.isEmpty() || r == "local_hangup")               return QString::fromLatin1("Call ended");
    return QString::fromLatin1("Call failed");
}

QString VoiceCallBridge::status() const
{
    if (m_state == "calling" || m_state == "negotiating") return QString::fromLatin1("Calling...");
    if (m_state == "ringing")   return QString::fromLatin1("Ringing...");
    if (m_state == "incoming")  return QString::fromLatin1("Incoming Zalo call");
    if (m_state == "answering") return QString::fromLatin1("Connecting...");
    if (m_state == "connected") return QString::fromLatin1("Zalo call connected");
    return reasonText(m_lastReason);
}

QString VoiceCallBridge::duration() const
{
    if (m_connectedAtMs <= 0) return QString::fromLatin1("0:00");
    int s = int((QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs) / 1000);
    if (s < 0) s = 0;
    return QString::number(s / 60) + ":" + QString("%1").arg(s % 60, 2, 10, QLatin1Char('0'));
}
