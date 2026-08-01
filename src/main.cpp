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
    installZalo10TermHandler();

    // CHẨN ĐOÁN: log phiên bản OpenSSL THỰC SỰ đang chạy (đọc từ chính thư
    // viện đã được loader nạp vào process, không phải macro lúc compile).
    // Dùng để xác nhận: sau khi bundle OpenSSL riêng + chỉnh rpath, log này
    // phải đổi từ bản OpenSSL cũ mặc định của BB10 NDK sang bản mới mình tự
    // build. Nếu vẫn thấy bản cũ sau khi bundle -> rpath/đường dẫn thư viện
    // chưa đúng, loader vẫn đang lấy từ $QNX_TARGET hệ thống chứ không phải
    // bản bundle trong app. SSLeay_version(SSLEAY_VERSION) trả về chuỗi kiểu
    // "OpenSSL 1.0.2u  20 Dec 2019" lấy từ chính .so đã nạp.
    //
    // Đọc kết quả: TLS 1.2 chỉ được thêm vào OpenSSL từ bản 1.0.1 trở lên.
    // Nếu dòng log dưới đây in ra OpenSSL 0.9.8x hoặc 1.0.0x — chắc chắn app
    // KHÔNG THỂ làm TLS 1.2 dù set QSslConfiguration kiểu gì, vì tính năng
    // đó không tồn tại trong chính binary OpenSSL đang chạy. Nếu là 1.0.1+
    // thì TLS 1.2 có tồn tại trong lib — lúc đó lỗi handshake phải do
    // nguyên nhân khác (cipher suite bị OpenSSL bản đó thiếu, v.v.), không
    // phải "không có TLS 1.2" nữa, cần điều tra tiếp theo hướng khác.
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
