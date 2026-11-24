#ifndef VOICECALLBRIDGE_HPP
#define VOICECALLBRIDGE_HPP

// UI-side bridge between QML (context property "voice") and the voice engine (voice/phone_engine.py).
//
// Transport: JSON lines over a private unix socket (QLocalSocket). The engine owns call signalling
// state (callcore.py), UDP/Opus media and the system ringtone; this class only (a) starts it,
// (b) relays Zalo "voip" control frames from the WebSocket into it, and (c) mirrors its state as
// QML properties. Voice (1:1) only: video needs the H.264 worker and is not wired up yet.

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>
#include <QStringList>
#include <QByteArray>
#include <QTimer>

class QLocalSocket;
class QProcess;
class ZaloService;

class VoiceCallBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state     READ state     NOTIFY changed)
    Q_PROPERTY(QString status    READ status    NOTIFY changed)
    Q_PROPERTY(QString peerName  READ peerName  NOTIFY changed)
    Q_PROPERTY(QString peerId    READ peerId    NOTIFY changed)
    Q_PROPERTY(bool active       READ active    NOTIFY changed)
    Q_PROPERTY(bool incoming     READ incoming  NOTIFY changed)
    Q_PROPERTY(bool connected    READ connected NOTIFY changed)
    Q_PROPERTY(bool muted        READ muted     NOTIFY changed)
    Q_PROPERTY(bool available    READ available NOTIFY changed)
    Q_PROPERTY(QString duration  READ duration  NOTIFY durationChanged)

public:
    explicit VoiceCallBridge(ZaloService *service, QObject *parent = 0);
    virtual ~VoiceCallBridge();

    QString state() const { return m_state; }
    QString status() const;
    QString peerName() const { return m_peerName; }
    QString peerId() const { return m_peerId; }
    bool active() const { return m_state != "idle"; }
    bool incoming() const { return m_state == "incoming"; }
    bool connected() const { return m_state == "connected"; }
    bool muted() const { return m_muted; }
    bool available() const { return m_engineReady; }
    QString duration() const;

public slots:
    Q_INVOKABLE void dial(const QString &peer, const QString &name);
    Q_INVOKABLE void answer();
    Q_INVOKABLE void hangup();
    Q_INVOKABLE void setMuted(bool muted);
    // The call sheet is on screen: tell the engine to drop its lock-screen style popup.
    Q_INVOKABLE void setForeground(bool foreground);
    Q_INVOKABLE void ensureRunning();
    // Wired to ZaloService::voiceCallControl (WS "voip" frames).
    void control(const QVariantMap &content);

signals:
    void changed();
    void durationChanged();
    // reason is the engine's code, e.g. "peer_reject", "timeout", "local_hangup".
    void callEnded(const QString &reason, int seconds);
    void engineError(const QString &message);

private slots:
    void connectEngine();
    void onEngineConnected();
    void onEngineDisconnected();
    void onEngineData();
    void onProcessFinished();
    void onProcessError();
    void onProcessOutput();
    void onTick();
    void onDialTimeout();

private:
    void sendFrame(const QVariantMap &frame);
    void flushPending();
    void applyEvent(const QVariantMap &event);
    void applyState(const QVariantMap &event);
    QString socketPath() const;
    QString engineDir() const;
    void startEngineProcess();
    void startEngine(bool force);
    static QString reasonText(const QString &reason);

    ZaloService  *m_service;
    QLocalSocket *m_socket;
    QProcess     *m_process;
    QTimer        m_retry;
    QTimer        m_tick;
    QTimer        m_dialTimer;      // engine never answered the dial (hung / silent)
    QByteArray    m_buffer;
    QList<QVariantMap> m_pending;
    QString m_state;
    QString m_peerName;
    QString m_peerId;
    QString m_lastReason;
    bool    m_muted;
    bool    m_engineReady;
    bool    m_wasIncoming;
    bool    m_awaitingDial;   // dial sent, engine has not left its initial idle state yet
    void failDial(const QString &reason);
    void logEngineTrace();
    int     m_connectTries;
    int     m_readyFalse;     // 'ready' frames with voice_service=false seen this session
    qint64  m_lastFailMs;       // back-off so a broken engine isn't relaunched on every event
    qint64  m_connectedAtMs;
};

#endif
