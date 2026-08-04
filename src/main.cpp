#include "applicationui.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/ThemeSupport>
#include <QLocale>
#include <QTranslator>
#include <Qt/qdeclarativedebug.h>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <cstdio>
#include <signal.h>
#include <unistd.h>
#include <QSocketNotifier>

#include <openssl/opensslv.h>
#include <openssl/crypto.h>

using namespace bb::cascades;

// SIGTERM handler (self-pipe trick).
// manualExit() của Cascades không bắn khi Navigator kill app qua signal
// POSIX (vd khi user vuốt đóng card), nên phải tự bắt SIGTERM ở đây.
// Signal handler không an toàn để chạy code Qt trực tiếp, nên chỉ write()
// 1 byte vào pipe, còn QSocketNotifier sẽ đọc và gọi cleanup thật sau.
static int g_zalo10TermFd[2] = { -1, -1 };

static void zalo10TermSignalHandler(int)
{
    char a = 1;
    ssize_t ignored = ::write(g_zalo10TermFd[1], &a, sizeof(a));
    (void)ignored; // không có gì để làm nếu write() lỗi trong signal handler
}

static bool installZalo10TermHandler()
{
    if (::pipe(g_zalo10TermFd) != 0) return false;

    struct sigaction sa;
    sa.sa_handler = zalo10TermSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (::sigaction(SIGTERM, &sa, 0) != 0) return false;
    ::sigaction(SIGINT, &sa, 0); // phòng trường hợp debug bằng Ctrl+C qua IDE

    return true;
}

// Runtime log file used by ApplicationUI::exportLog() (Settings -> Export Log).
// Kept on disk across app restarts (capped) so a crash log from the previous
// session is still exportable after relaunch.
static QFile *g_zalo10LogFile = 0;

static void zalo10MessageHandler(QtMsgType type, const char *msg)
{
    if (!g_zalo10LogFile) {
        QString logPath = QDir::homePath() + "/zalo10_runtime.log";
        g_zalo10LogFile = new QFile(logPath);
        QFileInfo fi(logPath);
        QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
        mode |= (fi.exists() && fi.size() > 3 * 1024 * 1024) ? QIODevice::Truncate : QIODevice::Append;
        g_zalo10LogFile->open(mode);
        QString header = QString("\n===== Zalo10 started %1 =====\n")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        g_zalo10LogFile->write(header.toUtf8());
    }

    const char *levelStr = "DEBUG";
    if (type == QtWarningMsg)  levelStr = "WARN";
    if (type == QtCriticalMsg) levelStr = "ERROR";
    if (type == QtFatalMsg)    levelStr = "FATAL";

    QString line = QString("[%1] [%2] %3\n")
                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
                   .arg(levelStr)
                   .arg(QString::fromUtf8(msg));

    if (g_zalo10LogFile->isOpen()) {
        g_zalo10LogFile->write(line.toUtf8());
        g_zalo10LogFile->flush();
    }
    fprintf(stderr, "%s", line.toUtf8().constData());

    if (type == QtFatalMsg) abort();
}

Q_DECL_EXPORT int main(int argc, char **argv)
{
    qInstallMsgHandler(zalo10MessageHandler);
    installZalo10TermHandler();

    // Log bản OpenSSL thực sự được loader nạp (không phải bản lúc compile),
    // để xác nhận app đang dùng bản OpenSSL tự bundle chứ không phải bản cũ
    // mặc định của BB10 NDK. TLS 1.2 chỉ có từ OpenSSL 1.0.1 trở lên.
    qDebug() << "[Zalo] Runtime OpenSSL:" << SSLeay_version(SSLEAY_VERSION)
             << "| SDK header version:" << OPENSSL_VERSION_TEXT;

    Application app(argc, argv);

    Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Bright);
    {
        QSettings settings("Berrylife", "Zalo10");
        if (settings.value("darkTheme", false).toBool()) {
            Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Dark);
        }
    }

    ApplicationUI appui;

    QSocketNotifier termNotifier(g_zalo10TermFd[0], QSocketNotifier::Read);
    QObject::connect(&termNotifier, SIGNAL(activated(int)), &appui, SLOT(onTermSignal(int)));

    return Application::exec();
}
