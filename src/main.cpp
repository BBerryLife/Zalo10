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

using namespace bb::cascades;

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

    Application app(argc, argv);

    Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Bright);
    {
        QSettings settings("Berrylife", "Zalo10");
        if (settings.value("darkTheme", false).toBool()) {
            Application::instance()->themeSupport()->setVisualStyle(VisualStyle::Dark);
        }
    }

    ApplicationUI appui;

    return Application::exec();
}
