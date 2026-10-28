#include "app/ApplicationUICommon.hpp"

static QString mimeTypeForImagePath(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".png"))                     return "image/png";
    if (lower.endsWith(".gif"))                     return "image/gif";
    if (lower.endsWith(".webp"))                     return "image/webp";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    return "image/jpeg";
}

static QString mimeTypeForVideoPath(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".3gp"))  return "video/3gpp";
    if (lower.endsWith(".mov"))  return "video/quicktime";
    if (lower.endsWith(".mkv"))  return "video/x-matroska";
    if (lower.endsWith(".pdf"))  return "application/pdf";
    if (lower.endsWith(".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (lower.endsWith(".doc"))  return "application/msword";
    if (lower.endsWith(".pptx")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (lower.endsWith(".ppt"))  return "application/vnd.ms-powerpoint";
    if (lower.endsWith(".xlsx")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (lower.endsWith(".xls"))  return "application/vnd.ms-excel";
    if (lower.endsWith(".txt"))  return "text/plain";
    if (lower.endsWith(".epub")) return "application/epub+zip";
    if (lower.endsWith(".apk"))  return "application/vnd.android.package-archive";
    if (lower.endsWith(".cer"))  return "application/x-x509-ca-cert";
    if (lower.endsWith(".zip"))  return "application/zip";
    if (lower.endsWith(".rar"))  return "application/x-rar-compressed";
    if (lower.endsWith(".7z"))   return "application/x-7z-compressed";
    if (lower.endsWith(".vcf"))  return "text/x-vcard";
    if (lower.endsWith(".mp3"))  return "audio/mpeg";
    if (lower.endsWith(".flac")) return "audio/flac";
    if (lower.endsWith(".m4a"))  return "audio/mp4";
    if (lower.endsWith(".bar"))  return "application/octet-stream";
    return "video/mp4";
}

static QString toLocalFilePath(const QString &pathOrUri)
{
    if (pathOrUri.startsWith("file://"))
        return pathOrUri.mid(7);
    return pathOrUri;
}

void ApplicationUI::invokeEmail(const QString &to, const QString &subject)
{
    InvokeRequest req;
    req.setTarget("sys.pim.uib.email.hybridcomposer");
    req.setAction("bb.action.COMPOSE");
    req.setMimeType("message/rfc822");
    req.setUri(QString("mailto:%1?subject=%2").arg(to).arg(subject));
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::createTodayEvent(const QString &subject, const QString &body, int durationMinutes)
{
    using namespace bb::pim::calendar;

    CalendarService calSvc;
    Result::Type folderResult = Result::Success;
    QPair<AccountId, FolderId> defFolder = calSvc.defaultCalendarFolder(&folderResult);
    if (folderResult != Result::Success) {
        qWarning() << "[App] createTodayEvent: defaultCalendarFolder() failed, result=" << folderResult;
        emit eventCreated(false, "Could not retrieve the device's default calendar");
        return;
    }

    QDateTime start = QDateTime::currentDateTime();
    int minutes = (durationMinutes > 0) ? durationMinutes : 30;
    QDateTime end = start.addSecs(minutes * 60);

    CalendarEvent ev;
    ev.setSubject(subject);
    ev.setBody(body);
    ev.setStartTime(start);
    ev.setEndTime(end);
    ev.setAccountId(defFolder.first);
    ev.setFolderId(defFolder.second);

    Result::Type createResult = calSvc.createEvent(ev);
    bool ok = (createResult == Result::Success);
    emit eventCreated(ok, ok ? QString() : QString("Error creating event, error code: %1").arg((int)createResult));
}

void ApplicationUI::copyToClipboard(const QString &text)
{
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert("text/plain", text.toUtf8());
}

bool ApplicationUI::copyImageToClipboard(const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "[App] copyImageToClipboard: cannot open" << path;
        return false;
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        return false;
    }

    QString mime = mimeTypeForImagePath(path);
    bb::system::Clipboard clipboard;
    clipboard.clear();
    clipboard.insert(mime, bytes);
    return true;
}

void ApplicationUI::queryShareTargets(const QString &text)
{
    Q_UNUSED(text);
    m_pendingShareMimeType = "text/plain";
    InvokeQueryTargetsRequest req;
    req.setAction("bb.action.SHARE");
    req.setMimeType("text/plain");
    InvokeQueryTargetsReply *reply = m_pInvokeManager->queryTargets(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onQueryTargetsFinished()));
}

void ApplicationUI::queryShareTargetsForImage(const QString &localPath)
{
    m_pendingShareMimeType = mimeTypeForImagePath(toLocalFilePath(localPath));
    InvokeQueryTargetsRequest req;
    req.setAction("bb.action.SHARE");
    req.setMimeType(m_pendingShareMimeType);
    InvokeQueryTargetsReply *reply = m_pInvokeManager->queryTargets(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onQueryTargetsFinished()));
}

void ApplicationUI::onQueryTargetsFinished()
{
    InvokeQueryTargetsReply *reply = qobject_cast<InvokeQueryTargetsReply*>(sender());
    if (!reply) return;

    QString tmpDir = QDir::homePath() + "/tmp/icons/";
    QDir().mkpath(tmpDir);

    QStringList nativePfx;
    nativePfx << "sys." << "com.rim.";

    QVariantList bbmMain, bbmGroup, bbmChannel;
    QVariantList textList, emailList, meetingList, connList, rememberList, otherNatList, thirdList;

    foreach (const InvokeAction &action, reply->actions()) {
        foreach (const InvokeTarget &tgt, action.targets()) {
            QVariantMap m;
            m["label"]  = tgt.label();
            m["target"] = tgt.name();
            m["action"] = action.name();

            QString src = tgt.icon().toLocalFile();
            if (!src.isEmpty() && QFile::exists(src)) {
                QString dst = tmpDir + tgt.name().replace("/", "_") + ".png";
                if (!QFile::exists(dst)) QFile::copy(src, dst);
                m["icon"] = "file://" + dst;
            } else {
                m["icon"] = "";
            }

            bool isNat = false;
            QString name = tgt.name().toLower();
            foreach (const QString &pfx, nativePfx) {
                if (name.startsWith(pfx)) { isNat = true; break; }
            }
            m["isNative"] = isNat;

            if (isNat) {
                if (name.contains("bbgroups") || (name.contains("bbm") && name.contains("group")))
                    bbmGroup.append(m);
                else if (name.contains("channel") || name.contains("channels"))
                    bbmChannel.append(m);
                else if (name.contains("bbm"))
                    bbmMain.append(m);
                else if (name.contains("text") || name.contains("sms") || name.contains("mms"))
                    textList.append(m);
                else if (name.contains("email"))
                    emailList.append(m);
                else if (name.contains("meeting") || name.contains("calendar"))
                    meetingList.append(m);
                else if (name.contains("bluetooth") || name.contains("nfc"))
                    connList.append(m);
                else if (name.contains("remember"))
                    rememberList.append(m);
                else
                    otherNatList.append(m);
            } else {
                thirdList.append(m);
            }
        }
    }
    reply->deleteLater();

    QVariantList result;
    QList<QVariantList*> ordered;
    ordered << &bbmMain << &bbmGroup << &bbmChannel << &textList << &emailList
            << &meetingList << &connList << &rememberList << &otherNatList << &thirdList;
    foreach (QVariantList *lst, ordered)
        foreach (const QVariant &v, *lst)
            result.append(v);

    emit shareTargetsReady(result);
}

void ApplicationUI::invokeShareTarget(const QString &target, const QString &action, const QString &text)
{
    InvokeRequest req;
    req.setTarget(target);
    req.setAction(action.isEmpty() ? "bb.action.SHARE" : action);
    req.setMimeType("text/plain");
    req.setData(text.toUtf8());
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::invokeShareTargetForImage(const QString &target, const QString &action, const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "[App] invokeShareTargetForImage: cannot open" << path;
        return;
    }
    QByteArray bytes = file.readAll();
    file.close();

    InvokeRequest req;
    req.setTarget(target);
    req.setAction(action.isEmpty() ? "bb.action.SHARE" : action);
    req.setMimeType(mimeTypeForImagePath(path));
    req.setData(bytes);
    m_pInvokeManager->invoke(req);
}

void ApplicationUI::openLocalFile(const QString &localPath)
{
    QString path = toLocalFilePath(localPath);
    if (!QFile::exists(path)) {
        return;
    }

    InvokeRequest req;
    req.setTarget("");
    req.setAction("bb.action.OPEN");
    req.setMimeType(mimeTypeForVideoPath(path));
    req.setUri("file://" + path);
    m_pInvokeManager->invoke(req);
}
