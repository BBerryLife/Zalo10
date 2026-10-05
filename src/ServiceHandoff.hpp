#ifndef SERVICEHANDOFF_HPP
#define SERVICEHANDOFF_HPP

// Bàn giao WebSocket giữa app UI và service headless (Zalo10Service).
// Dùng chung cho cả 2 process nên chỉ gồm hàm inline, không có .cpp riêng.
//
// Nguồn sự thật: file PID của UI trong thư mục data (cả 2 process cùng sandbox
// vì cùng 1 BAR). UI sống  -> service nhường WS. UI chết/không có file -> service
// giữ WS để nhận thông báo. Invoke chỉ để service phản ứng nhanh, mất invoke
// cũng không sao vì service còn poll file PID.

#include <QString>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QByteArray>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>

namespace ServiceHandoff {

static const char *const TARGET_SERVICE   = "com.BerryLife.Zalo10.service";
static const char *const ACTION_UI_OPENED = "com.BerryLife.Zalo10.service.UI_OPENED";
static const char *const ACTION_UI_CLOSED = "com.BerryLife.Zalo10.service.UI_CLOSED";

inline QString pidFilePath() { return QDir::homePath() + "/zalo10_ui.pid"; }

inline void markUiAlive()
{
    QFile f(pidFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray::number((qlonglong)QCoreApplication::applicationPid()));
}

inline void markUiGone() { QFile::remove(pidFilePath()); }

// true nếu file PID tồn tại VÀ process đó còn sống (kill(pid,0) không gửi
// signal thật). File PID mồ côi sau khi UI bị kill -9 sẽ trả về false.
inline bool isUiAlive()
{
    QFile f(pidFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    bool ok = false;
    qlonglong pid = f.readAll().trimmed().toLongLong(&ok);
    if (!ok || pid <= 0) return false;
    return (::kill((pid_t)pid, 0) == 0) || (errno == EPERM);
}

// Gửi invoke tới service. Lần đầu cũng đóng vai trò KHỞI ĐỘNG service nếu
// nó chưa chạy. Trả về InvokeTargetReply* (caller nối finished() để log error()) hoặc 0.
inline bb::system::InvokeTargetReply *pingService(bb::system::InvokeManager *im, const char *action)
{
    if (!im) return 0;
    bb::system::InvokeRequest req;
    req.setTarget(TARGET_SERVICE);
    req.setAction(action);
    return im->invoke(req);
}

} // namespace ServiceHandoff

#endif
