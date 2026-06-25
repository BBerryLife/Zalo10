#include "applicationui.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/ThemeSupport>
#include <QLocale>
#include <QTranslator>
#include <Qt/qdeclarativedebug.h>
#include "HeadlessService.hpp"
#include <QCoreApplication>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <cstdio>
#include <signal.h>
#include <unistd.h>
#include <QSocketNotifier>

using namespace bb::cascades;

// ─── SIGTERM handler (self-pipe trick) ─────────────────────────────────────
// LÝ DO: Application::manualExit() (Cascades) hóa ra KHÔNG bắn khi user vuốt
// card đóng app trong màn đa nhiệm trên BB10 — đã xác nhận qua log thực tế
// (process chết, "FilePicker destructor called" được log, nhưng không có
// dòng "[App] manualExit: saving session" nào trước đó). Khả năng cao
// manualExit() chỉ dành cho app TỰ gọi exit từ trong code, còn Navigator (OS)
// kill app từ ngoài thông qua signal POSIX chuẩn (SIGTERM) — bypass hết các
// signal ở tầng Cascades.
//
// Không thể gọi trực tiếp code Qt/network (saveSession, gửi WS Close frame...)
// ngay trong signal handler — handler chỉ được dùng các hàm "async-signal-safe"
// (vd write()), không an toàn để chạy logic Qt phức tạp ở đó (có thể deadlock/
// corrupt state). Giải pháp chuẩn: self-pipe trick — handler chỉ write() 1 byte
// vào 1 pipe, rồi QSocketNotifier (chạy trong event loop bình thường, an toàn)
// đọc byte đó và gọi logic cleanup thật (ApplicationUI::onTermSignal -> onManualExit).
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

    // ── Dual-mode: headless service vs UI app ─────────────────────────────
    // BB10 sets INVOKE_TARGET_KEY when launching via invoke-target.
    // Headless mode: QCoreApplication + HeadlessService (no UI, no Cascades).
    // UI mode: bb::cascades::Application + ApplicationUI (như trước).
    const QString invokeTarget = QString::fromLatin1(qgetenv("INVOKE_TARGET_KEY"));
    // Use endsWith(".headless") instead of exact match so debug builds
    // (which get a testDev_xxx suffix, e.g. com.BerryLife.Zalo10.testDev_xxx.headless)
    // are detected correctly.
    const bool isHeadless = invokeTarget.endsWith(QLatin1String(".headless"));

    if (isHeadless) {
        // ── Diagnostic: write a marker file immediately so we can confirm
        //    the headless process actually started (visible even without debugger).
        {
            QString markerPath = QDir::homePath() + "/headless_alive.txt";
            QFile mf(markerPath);
            if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                mf.write(QString("target=%1\npid=%2\n")
                    .arg(invokeTarget).arg((int)getpid()).toUtf8());
                mf.close();
            }
        }
        qDebug() << "[main] headless mode — starting HeadlessService target:" << invokeTarget;
        installZalo10TermHandler();
        QCoreApplication app(argc, argv);
        HeadlessService svc;
        // Qt4: activated(int) là protected, lambda connect không có — dùng SIGNAL/SLOT.
        // Tái dùng ApplicationUI::onTermSignal không tiện ở đây, dùng wrapper inline.
        // Wrapper phải là class thật để moc xử lý — định nghĩa ở file riêng không được
        // (đã ở main.cpp rồi), nên dùng cách đơn giản nhất: reuse ApplicationUI làm
        // helper không có — thay vào đó kết nối thẳng aboutToQuit và quit() bình thường,
        // SIGTERM sẽ gọi QCoreApplication::quit() qua ApplicationUI::onTermSignal pattern.
        // Thực tế: HeadlessService destructor gọi ZaloService destructor gọi saveSession.
        // QSocketNotifier + SIGNAL/SLOT string dạng Qt4:
        QSocketNotifier termNotifier(g_zalo10TermFd[0], QSocketNotifier::Read);
        QObject::connect(&termNotifier, SIGNAL(activated(int)), &app, SLOT(quit()));
        return app.exec();
    }

    // ── UI mode ───────────────────────────────────────────────────────────
    installZalo10TermHandler();

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
