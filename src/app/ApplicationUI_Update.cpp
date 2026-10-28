#include "app/ApplicationUICommon.hpp"

static const char *VERSION_MANIFEST_URL =
    "https://raw.githubusercontent.com/BBerryLife/BBerryLife.github.io/main/Data/Zalo10-version.json";

static bool isVersionNewer(const QString &a, const QString &b)
{
    QStringList pa = a.split('.');
    QStringList pb = b.split('.');
    int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        int va = (i < pa.size()) ? pa.at(i).toInt() : 0;
        int vb = (i < pb.size()) ? pb.at(i).toInt() : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

// Qt4 has no toHtmlEscaped(); '&' must be replaced first to avoid double escaping.
static QString escapeHtmlQt4(const QString &in)
{
    QString out = in;
    out.replace('&',  "&amp;");
    out.replace('<',  "&lt;");
    out.replace('>',  "&gt;");
    out.replace('"',  "&quot;");
    return out;
}

QString ApplicationUI::appVersion()
{
    return bb::ApplicationInfo().version();
}

QString ApplicationUI::exportLog()
{
    QString srcPath  = QDir::homePath() + "/zalo10_runtime.log";
    QString destDir  = "/accounts/1000/shared/documents/zalo10/log";
    QString stamp    = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString destPath = destDir + "/zalo10_log_" + stamp + ".txt";

    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qWarning() << "[App] exportLog: failed to create" << destDir;
        return QString();
    }

    if (!QFile::exists(srcPath)) {
        return QString();
    }

    bool ok = QFile::copy(srcPath, destPath);
    qWarning() << "[App] exportLog:" << (ok ? "success ->" : "FAILED ->") << destPath;
    return ok ? destPath : QString();
}

// Bundled OpenSSL is too old for TLS 1.2+ with the GitHub CDN: use AnyProtocol so it
// negotiates the highest common version.
static QNetworkRequest buildManifestRequest()
{
    QNetworkRequest req((QUrl(QString::fromLatin1(VERSION_MANIFEST_URL))));
    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConf.setProtocol(QSsl::AnyProtocol);
    req.setSslConfiguration(sslConf);
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("User-Agent", "Zalo10-BB10");
    return req;
}

void ApplicationUI::checkForUpdate()
{
    if (!m_updateManager) m_updateManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_updateManager->get(buildManifestRequest());
    connect(reply, SIGNAL(sslErrors(const QList<QSslError>&)), this, SLOT(onManifestSslErrors(const QList<QSslError>&)));
    connect(reply, SIGNAL(finished()), this, SLOT(onUpdateCheckFetchDone()));
}

void ApplicationUI::onManifestSslErrors(const QList<QSslError> &errors)
{
    for (int i = 0; i < errors.size(); ++i)
        qWarning() << "[App] manifest SSL error:" << errors.at(i).errorString();
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) reply->ignoreSslErrors();
}

void ApplicationUI::onUpdateCheckFetchDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[App] checkForUpdate: network error" << err << "-" << reply->errorString();
        emit updateCheckResult(false, "", "", "Could not check for updates. Check your internet connection.");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    QString latest      = root.value("latestVersion").toString();
    QString downloadUrl = root.value("downloadUrl").toString();
    if (latest.isEmpty()) {
        qWarning() << "[App] checkForUpdate: manifest missing/invalid";
        emit updateCheckResult(false, "", "", "Update info unavailable right now. Try again later.");
        return;
    }

    QString current = appVersion();
    bool isLatest = !isVersionNewer(latest, current);
    emit updateCheckResult(isLatest, latest, downloadUrl, "");
}

void ApplicationUI::fetchChangelog()
{
    if (!m_updateManager) m_updateManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_updateManager->get(buildManifestRequest());
    connect(reply, SIGNAL(sslErrors(const QList<QSslError>&)), this, SLOT(onManifestSslErrors(const QList<QSslError>&)));
    connect(reply, SIGNAL(finished()), this, SLOT(onChangelogFetchDone()));
}

void ApplicationUI::onChangelogFetchDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        qWarning() << "[App] fetchChangelog: network error" << err << "-" << reply->errorString();
        emit changelogReady("", "Could not load the changelog. Check your internet connection.");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    QVariantList versions = root.value("changelog").toList();
    if (versions.isEmpty()) {
        emit changelogReady("", "Changelog is empty right now.");
        return;
    }

    emit changelogReady(buildChangelogHtml(versions), "");
}

QString ApplicationUI::buildChangelogHtml(const QVariantList &versions) const
{
    static const char *tags[] = { "NEW:", "IMPROVE:", "FIX:", "REMOVED:", 0 };

    QString html;
    html += "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<style>"
            "body{font-family:'Slate Pro','Helvetica Neue',Helvetica,sans-serif;"
            "margin:0;padding:18px;color:#1a1a1a;background:#ffffff;font-size:16px;}"
            "h2{font-size:20px;font-weight:700;margin:0 0 14px 0;padding-bottom:8px;"
            "border-bottom:2px solid #2575fc;}"
            "h2.notfirst{margin-top:30px;}"
            "ul{margin:0;padding-left:22px;}"
            "li{margin-bottom:12px;line-height:1.45;}"
            "b{font-weight:700;}"
            "</style></head><body>";

    for (int i = 0; i < versions.size(); ++i) {
        QVariantMap v = versions.at(i).toMap();
        QString ver = v.value("version").toString();
        QVariantList items = v.value("items").toList();

        html += QString("<h2%1>Version %2.x</h2><ul>")
                    .arg(i == 0 ? "" : " class=\"notfirst\"")
                    .arg(escapeHtmlQt4(ver));

        for (int j = 0; j < items.size(); ++j) {
            QString line = escapeHtmlQt4(items.at(j).toString());

            for (int t = 0; tags[t] != 0; ++t) {
                QString tag = QString::fromLatin1(tags[t]);
                if (line.startsWith(tag)) {
                    line = "<b>" + tag + "</b>" + line.mid(tag.length());
                    break;
                }
            }

            QStringList parts = line.split("**");
            QString rendered;
            for (int p = 0; p < parts.size(); ++p)
                rendered += (p % 2 == 1) ? ("<b>" + parts.at(p) + "</b>") : parts.at(p);

            html += "<li>" + rendered + "</li>";
        }
        html += "</ul>";
    }

    html += "</body></html>";
    return html;
}
