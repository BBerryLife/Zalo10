#ifndef ZALONETWORKMANAGER_HPP
#define ZALONETWORKMANAGER_HPP

#include <QDebug>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <string>

#include "core/PhoneSyncCore.hpp"

// BB10's Qt SSL stack fails the TLS handshake with some Zalo API hosts (SslHandshakeFailedError)
// while plain OpenSSL with TLS 1.2 connects fine. Requests to *.zalo.me and *.zaloapp.com go through Qt as
// usual; only when the handshake fails is the same request replayed over OpenSSL, so callers
// keep using the normal QNetworkReply API.

class ZaloRawHttpsThread : public QThread {
public:
    QString    method, url, headers;
    QByteArray reqBody, respBody, respHead;
    int        status;
    bool       ok;
    QString    info;
    ZaloRawHttpsThread() : status(0), ok(false) {}
protected:
    void run()
    {
        QUrl u = QUrl::fromEncoded(url.toUtf8());
        QByteArray host = u.host().toLatin1();
        int port = u.port(443);
#if QT_VERSION < 0x050000
        QByteArray path = u.encodedPath();
        if (u.hasQuery()) path += "?" + u.encodedQuery();
#else
        QByteArray path = u.path(QUrl::FullyEncoded).toLatin1();
        if (u.hasQuery()) path += "?" + u.query(QUrl::FullyEncoded).toLatin1();
#endif
        if (path.isEmpty()) path = "/";

        std::string b, head, inf;
        int st = 0;
        ok = PhoneSyncCore::httpsRequest(method.toLatin1().constData(), host.constData(), port, path.constData(),
                                         headers.toUtf8().constData(),
                                         std::string(reqBody.constData(), (size_t)reqBody.size()),
                                         30, b, st, head, inf);
        status   = st;
        info     = QString::fromLatin1(inf.c_str());
        respBody = QByteArray(b.data(), (int)b.size());
        respHead = QByteArray(head.data(), (int)head.size());
    }
};

class ZaloFallbackReply : public QNetworkReply {
    Q_OBJECT
public:
    ZaloFallbackReply(QNetworkAccessManager::Operation op, const QNetworkRequest &req, const QByteArray &body,
                      QNetworkReply *real, QNetworkCookieJar *jar, QObject *parent)
        : QNetworkReply(parent), m_real(real), m_jar(jar), m_body(body), m_raw(0),
          m_pos(0), m_aborted(false), m_done(false)
    {
        setOperation(op);
        setRequest(req);
        setUrl(req.url());
        setOpenMode(QIODevice::ReadOnly);
        real->setParent(this);
        connect(real, SIGNAL(readyRead()), this, SLOT(onRealReadyRead()));
        connect(real, SIGNAL(finished()), this, SLOT(onRealFinished()));
        connect(real, SIGNAL(downloadProgress(qint64,qint64)), this, SIGNAL(downloadProgress(qint64,qint64)));
        connect(real, SIGNAL(uploadProgress(qint64,qint64)), this, SIGNAL(uploadProgress(qint64,qint64)));
    }

    void abort()
    {
        m_aborted = true;
        if (m_real) m_real->abort();
    }

    qint64 bytesAvailable() const { return (m_buf.size() - m_pos) + QIODevice::bytesAvailable(); }
    bool isSequential() const { return true; }

protected:
    qint64 readData(char *data, qint64 maxlen)
    {
        qint64 n = qMin<qint64>(maxlen, m_buf.size() - m_pos);
        if (n <= 0) return m_done ? -1 : 0;
        memcpy(data, m_buf.constData() + m_pos, (size_t)n);
        m_pos += n;
        if (m_pos > (1 << 20) && m_pos == m_buf.size()) { m_buf.clear(); m_pos = 0; }
        return n;
    }

private slots:
    void onRealReadyRead()
    {
        if (!m_real) return;
        m_buf += m_real->readAll();
        emit readyRead();
    }

    void onRealFinished()
    {
        QNetworkReply *r = m_real;
        if (!r) return;
        if (r->error() == QNetworkReply::SslHandshakeFailedError && !m_aborted && m_buf.isEmpty()) {
            m_real = 0;
            r->deleteLater();
            startRaw();
            return;
        }
        m_buf += r->readAll();
        typedef QPair<QByteArray, QByteArray> HeaderPair;
        foreach (const HeaderPair &hp, r->rawHeaderPairs()) setRawHeader(hp.first, hp.second);
        static const QNetworkRequest::Attribute attrs[] = {
            QNetworkRequest::HttpStatusCodeAttribute, QNetworkRequest::HttpReasonPhraseAttribute,
            QNetworkRequest::RedirectionTargetAttribute, QNetworkRequest::ConnectionEncryptedAttribute
        };
        for (unsigned i = 0; i < sizeof(attrs) / sizeof(attrs[0]); ++i) {
            QVariant v = r->attribute(attrs[i]);
            if (v.isValid()) setAttribute(attrs[i], v);
        }
        setUrl(r->url());
        QNetworkReply::NetworkError err = r->error();
        QString errStr = r->errorString();
        m_real = 0;
        r->deleteLater();
        finish(err, errStr);
    }

    void onRawDone()
    {
        ZaloRawHttpsThread *t = m_raw;
        m_raw = 0;
        if (!t) return;
        if (m_aborted) { t->deleteLater(); finish(QNetworkReply::OperationCanceledError, "Operation canceled"); return; }
        if (!t->ok) {
            qWarning() << "[Net] raw TLS 1.2 retry failed for" << url().host() << t->info;
            t->deleteLater();
            finish(QNetworkReply::SslHandshakeFailedError, "SSL handshake failed");
            return;
        }
        parseRawResponse(t->respHead, t->status);
        m_buf += t->respBody;
        int st = t->status;
        t->deleteLater();
        emit readyRead();
        finish(errorForStatus(st), st >= 400 ? QString("HTTP %1").arg(st) : QString());
    }

private:
    static QNetworkReply::NetworkError errorForStatus(int st)
    {
        if (st < 400) return QNetworkReply::NoError;
        switch (st) {
        case 401: return QNetworkReply::AuthenticationRequiredError;
        case 403: return QNetworkReply::ContentAccessDenied;
        case 404: return QNetworkReply::ContentNotFoundError;
        case 405: return QNetworkReply::ContentOperationNotPermittedError;
        default:  return st < 500 ? QNetworkReply::UnknownContentError : QNetworkReply::ProtocolFailure;
        }
    }

    void finish(QNetworkReply::NetworkError err, const QString &errStr)
    {
        if (m_done) return;
        if (err != QNetworkReply::NoError) setError(err, errStr);
        m_done = true;
        setFinished(true);
        if (err != QNetworkReply::NoError) emit error(err);
        emit finished();
    }

    void startRaw()
    {
        static QSet<QString> logged;
        if (!logged.contains(url().host())) {
            logged.insert(url().host());
            qDebug() << "[Net] TLS handshake failed for" << url().host() << "- retrying over raw TLS 1.2";
        }
        const QNetworkRequest &req = request();
        m_raw = new ZaloRawHttpsThread;
        m_raw->method  = (operation() == QNetworkAccessManager::PostOperation) ? "POST" : "GET";
        m_raw->url     = QString::fromUtf8(req.url().toEncoded());
        m_raw->reqBody = m_body;

        QString h;
        QList<QByteArray> names = req.rawHeaderList();
        for (int i = 0; i < names.size(); ++i) {
            QByteArray n = names[i].toLower();
            if (n == "host" || n == "content-length" || n == "connection" || n == "accept-encoding" || n == "cookie")
                continue;
            h += QString::fromLatin1(names[i]) + ": " + QString::fromUtf8(req.rawHeader(names[i])) + "\r\n";
        }
        if (m_jar) {
            QList<QNetworkCookie> cs = m_jar->cookiesForUrl(req.url());
            QByteArray cookie;
            for (int i = 0; i < cs.size(); ++i) {
                if (i) cookie += "; ";
                cookie += cs[i].toRawForm(QNetworkCookie::NameAndValueOnly);
            }
            if (!cookie.isEmpty()) h += "Cookie: " + QString::fromUtf8(cookie) + "\r\n";
        }
        m_raw->headers = h;
        connect(m_raw, SIGNAL(finished()), this, SLOT(onRawDone()));
        m_raw->start();
    }

    void parseRawResponse(const QByteArray &head, int status)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        QList<QByteArray> lines = head.split('\n');
        QMap<QByteArray, QByteArray> hdr;
        QList<QNetworkCookie> cookies;
        for (int i = 1; i < lines.size(); ++i) {
            QByteArray ln = lines[i].trimmed();
            int c = ln.indexOf(':');
            if (c <= 0) continue;
            QByteArray name = ln.left(c).trimmed();
            QByteArray val  = ln.mid(c + 1).trimmed();
            if (name.toLower() == "set-cookie") cookies += QNetworkCookie::parseCookies(val);
            if (hdr.contains(name)) hdr[name] += "\n" + val; else hdr[name] = val;
        }
        for (QMap<QByteArray, QByteArray>::const_iterator it = hdr.constBegin(); it != hdr.constEnd(); ++it)
            setRawHeader(it.key(), it.value());
        if (m_jar && !cookies.isEmpty()) m_jar->setCookiesFromUrl(cookies, url());
        if (status >= 300 && status < 400 && hdr.contains("Location"))
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, url().resolved(QUrl::fromEncoded(hdr["Location"])));
    }

    QNetworkReply *m_real;
    QNetworkCookieJar *m_jar;
    QByteArray m_body;
    QByteArray m_buf;
    ZaloRawHttpsThread *m_raw;
    qint64 m_pos;
    bool m_aborted;
    bool m_done;
};

class ZaloNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT
public:
    explicit ZaloNetworkAccessManager(QObject *parent = 0) : QNetworkAccessManager(parent) {}

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoing)
    {
        const QUrl u = req.url();
        bool wrap = (op == GetOperation || op == PostOperation) && u.scheme() == "https"
                 && (u.host().endsWith(".zalo.me") || u.host() == "zalo.me"
                     || u.host().endsWith(".zaloapp.com") || u.host() == "zaloapp.com");
        QByteArray body;
        if (wrap && outgoing) {
            if (outgoing->isSequential()) {
                wrap = false;
            } else {
                qint64 pos = outgoing->pos();
                body = outgoing->readAll();
                outgoing->seek(pos);
            }
        }
        QNetworkReply *real = QNetworkAccessManager::createRequest(op, req, outgoing);
        if (!wrap) return real;
        return new ZaloFallbackReply(op, req, body, real, cookieJar(), this);
    }
};

#endif
