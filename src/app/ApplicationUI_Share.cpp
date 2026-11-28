#include "app/ApplicationUICommon.hpp"

#include <QImage>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <screen/screen.h>

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

void ApplicationUI::captureCameraPhoto()
{
    InvokeRequest req;
    req.setTarget("sys.camera.card");
    req.setAction("bb.action.CAPTURE");
    req.setData("photo");
    m_waitingCameraPhoto = true;
    m_pInvokeManager->invoke(req);
}

// One attempt to read the display into a pixmap buffer with the given pixel format, buffer usage and rectangle mode.
// On failure `stage` names the failing libscreen call and `errnum` is its errno.
static bool readDisplayOnce(screen_context_t ctx, screen_display_t disp, int *size, int format, int usage,
                            bool useRect, QImage &out, const char *&stage, int &errnum)
{
    stage = 0;
    errnum = 0;
    screen_pixmap_t pix = 0;
    if (screen_create_pixmap(&pix, ctx) != 0) { stage = "create_pixmap"; errnum = errno; return false; }

    bool ok = false;
    screen_buffer_t buf = 0;
    if (screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_FORMAT, &format) != 0
        || screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_USAGE, &usage) != 0
        || screen_set_pixmap_property_iv(pix, SCREEN_PROPERTY_BUFFER_SIZE, size) != 0) {
        stage = "pixmap_props"; errnum = errno;
    } else if (screen_create_pixmap_buffer(pix) != 0) {
        stage = "pixmap_buffer"; errnum = errno;
    } else if (screen_get_pixmap_property_pv(pix, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&buf) != 0 || !buf) {
        stage = "render_buffer"; errnum = errno;
    } else {
        int rect[4] = { 0, 0, size[0], size[1] };
        if (screen_read_display(disp, buf, useRect ? 1 : 0, useRect ? rect : NULL, 0) != 0) {
            stage = "read_display"; errnum = errno;
        } else {
            screen_flush_blits(ctx, SCREEN_WAIT_IDLE);
            void *ptr = 0;
            int stride = 0;
            screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, &ptr);
            screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
            if (!ptr || stride <= 0) {
                stage = "buffer_pointer";
            } else {
                // copy() detaches from the screen buffer, which is freed below.
                out = QImage((const uchar *)ptr, size[0], size[1], stride, QImage::Format_RGB32).copy();
                ok = !out.isNull();
                if (!ok) stage = "empty_image";
            }
        }
    }
    screen_destroy_pixmap(pix);
    return ok;
}

// Reads the primary display into a QImage through libscreen. Whatever is on screen is captured (this app's own
// window included), which is why the caller waits for menus to close first. Needs use_camera_desktop in
// bar-descriptor.xml. Pixel format, buffer usage and rectangle mode are not documented well, so a few combinations
// are tried; every failure is logged with its errno, and `err` carries the first one ("stage:errno").
static bool grabDisplay(QImage &out, QString &err)
{
    screen_context_t ctx = 0;
    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        err = QString("create_context:%1").arg(errno);
        return false;
    }

    bool ok = false;
    screen_display_t *displays = 0;
    int count = 0;
    int size[2] = { 0, 0 };

    screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count);
    if (count < 1) {
        err = "no_display";
    } else if (!(displays = (screen_display_t *)calloc(count, sizeof(screen_display_t)))) {
        err = "no_memory";
    } else if (screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS, (void **)displays) != 0) {
        err = QString("get_displays:%1").arg(errno);
    } else if (screen_get_display_property_iv(displays[0], SCREEN_PROPERTY_SIZE, size) != 0
               || size[0] <= 0 || size[1] <= 0) {
        err = "display_size";
    } else {
        const int formats[2] = { SCREEN_FORMAT_RGBX8888, SCREEN_FORMAT_RGBA8888 };
        const int usages[2]  = { SCREEN_USAGE_READ | SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE,
                                 SCREEN_USAGE_READ | SCREEN_USAGE_NATIVE };
        for (int f = 0; f < 2 && !ok; ++f) {
            for (int u = 0; u < 2 && !ok; ++u) {
                for (int r = 0; r < 2 && !ok; ++r) {
                    const char *stage = 0;
                    int e = 0;
                    ok = readDisplayOnce(ctx, displays[0], size, formats[f], usages[u], r == 0, out, stage, e);
                    if (!ok) {
                        qWarning() << "[Capture] attempt fmt" << f << "usage" << u << "rect" << (r == 0)
                                   << "failed at" << (stage ? stage : "?") << "errno" << e << strerror(e);
                        if (err.isEmpty()) err = QString("%1:%2").arg(stage ? stage : "?").arg(e);
                    }
                }
            }
        }
        if (ok) err.clear();
    }

    free(displays);
    screen_destroy_context(ctx);
    return ok;
}

void ApplicationUI::captureScreen()
{
    captureScreenRegion(0, 0, 0, 0);
}

void ApplicationUI::captureScreenRegion(int x, int y, int w, int h)
{
    QImage img;
    QString err;
    if (!grabDisplay(img, err)) {
        qWarning() << "[Capture] failed:" << err;
        emit screenCaptureFailed(err);
        return;
    }

    if (w > 0 && h > 0) {
        QRect r = QRect(x, y, w, h).intersected(QRect(0, 0, img.width(), img.height()));
        if (!r.isEmpty()) img = img.copy(r);
    }

    const QString path = QString("/tmp/zalo10_capture_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    if (!img.save(path, "PNG")) {
        qWarning() << "[Capture] save failed:" << path;
        emit screenCaptureFailed("save");
        return;
    }
    qDebug() << "[Capture] saved" << img.width() << "x" << img.height();
    emit screenCaptured(path, img.width(), img.height());
}

void ApplicationUI::onChildCardDone(const bb::system::CardDoneMessage &message)
{
    if (m_hubTapCard) {
        // The reply card of a Hub tap was closed without "Open" (Open resets the flag in onInvoked()): the user came
        // from the Hub, so go back to it instead of leaving them in the app.
        m_hubTapCard = false;
        qDebug() << "[App] Hub reply card closed (" << message.reason() << "): back to the Hub";
        minimizeApp();
        return;
    }
    if (!m_waitingCameraPhoto) return;   // some other card (share, ...) ended
    // The camera card answers with the saved file's path (reason "save"), nothing when it is cancelled.
    QString path = message.data();
    if (path.startsWith("file://")) path = path.mid(7);
    const QString lower = path.toLower();
    const bool isImage = lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png");
    if (path.isEmpty() || !isImage || !QFile::exists(path)) {
        if (message.reason() != "save") m_waitingCameraPhoto = false;   // cancelled / closed
        return;
    }
    m_waitingCameraPhoto = false;
    emit cameraPhotoCaptured(path);
}
