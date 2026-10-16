#ifndef SERVICEHANDOFF_HPP
#define SERVICEHANDOFF_HPP

// WebSocket handoff between the UI app and the headless service.
// The UI pid file is the source of truth: UI alive -> service yields the WS,
// UI dead -> service keeps it. Invokes only speed this up (service also polls the file).

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

inline bool isUiAlive()
{
    QFile f(pidFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    bool ok = false;
    qlonglong pid = f.readAll().trimmed().toLongLong(&ok);
    if (!ok || pid <= 0) return false;
    return (::kill((pid_t)pid, 0) == 0) || (errno == EPERM);
}

inline bb::system::InvokeTargetReply *pingService(bb::system::InvokeManager *im, const char *action)
{
    if (!im) return 0;
    bb::system::InvokeRequest req;
    req.setTarget(TARGET_SERVICE);
    req.setAction(action);
    return im->invoke(req);
}

}

#endif
