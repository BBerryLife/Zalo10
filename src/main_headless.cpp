#include "HeadlessService.hpp"

#include <bb/Application>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <cstdio>

using namespace bb;

// Ghi log riêng cho headless service — tách khỏi zalo10_runtime.log của UI app
// để không lẫn lộn khi debug 2 process cùng lúc (đúng mẫu message-handler đã
// dùng trong main.cpp, chỉ đổi tên file log).
static QFile *g_headlessLogFile = 0;

static void headlessMessageHandler(QtMsgType type, const char *msg)
{
    if (!g_headlessLogFile) {
        QString logPath = QDir::homePath() + "/zalo10_headless.log";
        g_headlessLogFile = new QFile(logPath);
        QFileInfo fi(logPath);
        QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
        mode |= (fi.exists() && fi.size() > 3 * 1024 * 1024) ? QIODevice::Truncate : QIODevice::Append;
        g_headlessLogFile->open(mode);
        QString header = QString("\n===== HeadlessService started %1 =====\n")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        g_headlessLogFile->write(header.toUtf8());
    }

    const char *levelStr = "DEBUG";
    if (type == QtWarningMsg)  levelStr = "WARN";
    if (type == QtCriticalMsg) levelStr = "ERROR";
    if (type == QtFatalMsg)    levelStr = "FATAL";

    QString line = QString("[%1] [%2] %3\n")
                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
                   .arg(levelStr)
                   .arg(QString::fromUtf8(msg));

    if (g_headlessLogFile->isOpen()) {
        g_headlessLogFile->write(line.toUtf8());
        g_headlessLogFile->flush();
    }
    fprintf(stderr, "%s", line.toUtf8().constData());

    if (type == QtFatalMsg) abort();
}

// KHÔNG dùng bb::cascades::Application ở đây — target này không có UI, không
// scene graph, không QML. bb::Application là lớp cơ sở nhẹ hơn, đúng loại
// dùng cho invoke-target kiểu "service" theo tài liệu headlesserviceui của
// BlackBerry. Cascades::Application khởi tạo hạ tầng render/window rất tốn
// tài nguyên và không cần thiết (thậm chí có thể fail) khi chạy không màn hình.
Q_DECL_EXPORT int main(int argc, char **argv)
{
    qInstallMsgHandler(headlessMessageHandler);

    Application app(argc, argv);

    HeadlessService service;
    (void)service;

    return Application::exec();
}
