#ifndef LOGSANITIZER_HPP
#define LOGSANITIZER_HPP

#include <QRegExp>
#include <QString>

// Runs on every log line before it is written. Logs get exported and shared, so credentials
// must never reach them even if a call site forgets to leave them out.
namespace LogSanitizer {

inline bool isNoise(const QString &m)
{
    return m.contains("FilePicker destructor")
        || m.contains("pushPage : mNavigationStack")
        || m.contains("NavigationPaneOnFwdTransitionDone")
        || m.contains("Encountered unexpected parent QObject");
}

inline QString shorten(const QString &v)
{
    return v.left(3) + "***(" + QString::number(v.length()) + ")";
}

inline QString sanitize(QString m)
{
    m.replace(QRegExp("(set-cookie|cookie)\\s*:[^\\n]*", Qt::CaseInsensitive), "\\1: ***");

    QRegExp named("(zpsid|zpdid|zlogin_session|zpw_sek|zpw_enk|zpw_sid|zoauth_code|token|signkey|"
                  "secretkey|secret|sessionkey|imei|password|cipherkey)(\"?\\s*[=:]\\s*\"?)([^\\s&;,\"'<>]{4,})",
                  Qt::CaseInsensitive);
    m.replace(named, "\\1\\2***");

    QRegExp query("([?&][A-Za-z_]*(?:token|key|sig|sign|secret|sess|auth|params|data)[A-Za-z_]*=)[^&\\s\"']+",
                  Qt::CaseInsensitive);
    m.replace(query, "\\1***");

    QRegExp opaque("[A-Za-z0-9_+=-]{32,}");
    int pos = 0;
    while ((pos = opaque.indexIn(m, pos)) != -1) {
        QString r = shorten(opaque.cap(0));
        m.replace(pos, opaque.matchedLength(), r);
        pos += r.length();
    }
    return m;
}

}

#endif
