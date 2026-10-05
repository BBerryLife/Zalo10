// Zalo10Service — service headless: giữ WebSocket Zalo khi app UI đã đóng hẳn để
// vẫn nhận tin và đẩy vào BlackBerry Hub. Dùng lại nguyên ZaloService của app UI.
#include "ServiceController.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>

#include <openssl/crypto.h>

static QFile *g_logFile = 0;

static void serviceMessageHandler(QtMsgType type, const char *msg)
{
    if (!g_logFile) {
        // File riêng, KHÔNG dùng zalo10_runtime.log của UI để 2 process không trộn log.
        QString path = QDir::homePath() + "/zalo10_service.log";
        g_logFile = new QFile(path);
        QFileInfo fi(path);
        QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
        mode |= (fi.exists() && fi.size() > 1024 * 1024) ? QIODevice::Truncate : QIODevice::Append;
        g_logFile->open(mode);
        g_logFile->write(QString("\n===== Zalo10Service started %1 =====\n")
                         .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")).toUtf8());
    }
    const char *lvl = (type == QtWarningMsg) ? "WARN" : (type == QtCriticalMsg) ? "ERROR"
                    : (type == QtFatalMsg) ? "FATAL" : "DEBUG";
    QString line = QString("[%1] [%2] %3\n")
                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")).arg(lvl)
                   .arg(QString::fromUtf8(msg));
    if (g_logFile->isOpen()) { g_logFile->write(line.toUtf8()); g_logFile->flush(); }
    fprintf(stderr, "%s", line.toUtf8().constData());
    if (type == QtFatalMsg) abort();
}

Q_DECL_EXPORT int main(int argc, char **argv)
{
    qInstallMsgHandler(serviceMessageHandler);
    qDebug() << "[Service] OpenSSL:" << SSLeay_version(SSLEAY_VERSION);

    QCoreApplication app(argc, argv);
    ServiceController controller;
    return app.exec();
}
