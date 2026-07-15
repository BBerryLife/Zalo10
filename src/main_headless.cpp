#include "HeadlessService.hpp"

#include <bb/Application>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <cstdio>
#include <exception>

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

// QUAN TRỌNG — nguồn gốc thật sự của việc HeadlessService "chết lặng lẽ"
// (log dừng đột ngột, không dòng lỗi) mà nhiều lần trước đây tưởng là do
// avatar burst / SQLite lock / lỗi AES block-alignment: log thực tế bắt được
// đúng dòng "Qt has caught an exception thrown from an event handler.
// Throwing exceptions from an event handler is not supported in Qt. You
// must reimplement QApplication::notify() and catch all exceptions there."
// ngay trước khi process chết — tức có 1 exception C++ thật (nhiều khả năng
// std::bad_alloc, ví dụ từ jsonToMap()/QString/QVariantMap cấp phát bộ nhớ
// lúc đang xử lý payload groupDetails, đúng lúc hệ thống đang bận avatar
// download) bị ném ra từ trong 1 slot (onGroupDetailsDone(), nối với
// QNetworkReply::finished()) — và vì KHÔNG CÓ try/catch nào bọc quanh, Qt's
// event dispatcher bắt được, in cảnh báo, rồi để exception tiếp tục lan ra
// ngoài -> std::terminate() -> cả process chết, đúng như Qt khuyến cáo phải
// tự viết notify() để bọc lại. Đây LÀ giải pháp chính thức được Qt gợi ý,
// và cũng là lưới an toàn CHUNG cho MỌI slot khác trong app (không cần dò
// từng chỗ có thể ném exception) — nếu 1 event handler nào đó gặp lỗi cấp
// phát bộ nhớ hay exception bất kỳ, HeadlessService giờ chỉ log lỗi và tiếp
// tục sống, thay vì chết toàn bộ + tạo instance trùng lặp + hỏng WebSocket
// session (chuỗi nguyên nhân gây màn hình QR bật lại đã theo dõi nhiều lần).
class SafeHeadlessApplication : public Application
{
public:
    SafeHeadlessApplication(int argc, char **argv) : Application(argc, argv) {}

    bool notify(QObject *receiver, QEvent *event)
    {
        try {
            return Application::notify(receiver, event);
        } catch (const std::exception &e) {
            qWarning() << "[HeadlessService] CAUGHT exception in event handler (process van song):" << e.what();
            return false;
        } catch (...) {
            qWarning() << "[HeadlessService] CAUGHT unknown exception in event handler (process van song)";
            return false;
        }
    }
};

// KHÔNG dùng bb::cascades::Application ở đây — target này không có UI, không
// scene graph, không QML. bb::Application là lớp cơ sở nhẹ hơn, đúng loại
// dùng cho invoke-target kiểu "service" theo tài liệu headlesserviceui của
// BlackBerry. Cascades::Application khởi tạo hạ tầng render/window rất tốn
// tài nguyên và không cần thiết (thậm chí có thể fail) khi chạy không màn hình.
Q_DECL_EXPORT int main(int argc, char **argv)
{
    qInstallMsgHandler(headlessMessageHandler);

    SafeHeadlessApplication app(argc, argv);

    HeadlessService service;
    (void)service;

    return SafeHeadlessApplication::exec();
}
