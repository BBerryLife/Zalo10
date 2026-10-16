#ifndef RAWHTTPSTHREAD_HPP
#define RAWHTTPSTHREAD_HPP

#include <QByteArray>
#include <QHostAddress>
#include <QHostInfo>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <string>

#include "PhoneSyncCore.hpp"

// GET over OpenSSL TLS 1.2 on a worker thread. Used when QNetworkAccessManager fails the TLS
// handshake. headers: "Name: value\r\n" lines. info collects resolved IPs and the OpenSSL result.
class RawHttpsThread : public QThread {
public:
    QString    url, headers;
    QByteArray body;
    int        status;
    bool       ok;
    QString    info;
    RawHttpsThread() : status(0), ok(false) {}
protected:
    void run()
    {
        QUrl u = QUrl::fromEncoded(url.toUtf8());
        QByteArray host = u.host().toLatin1();
        int port = u.port(443);
        QByteArray path = u.encodedPath();
        if (path.isEmpty()) path = "/";
        if (u.hasQuery()) path += "?" + u.encodedQuery();

        QStringList ips;
        QHostInfo hi = QHostInfo::fromName(u.host());
        for (int i = 0; i < hi.addresses().size(); ++i) ips << hi.addresses().at(i).toString();
        info = "ips=" + ips.join(",") + " ";

        std::string b, inf;
        int st = 0;
        std::string hdr = headers.toUtf8().constData();
        bool r = PhoneSyncCore::httpsGet(host.constData(), port, path.constData(), hdr, 20, b, st, inf);
        info += QString::fromLatin1(inf.c_str());
        status = st;
        if (r && st >= 200 && st < 300 && !b.empty()) {
            body = QByteArray(b.data(), (int)b.size());
            ok = true;
        }
    }
};

#endif
