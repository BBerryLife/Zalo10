#ifndef ZALOSERVICEUTILS_HPP
#define ZALOSERVICEUTILS_HPP

// Small JSON/crypto helpers shared by the ZaloService_*.cpp translation units.
// Qt4 has no QJson, so JSON (de)serialization is done via QScriptEngine; these
// wrappers keep that detail in one place instead of repeating it at every call site.
// Everything here is `inline` since the header is included from multiple .cpp files.

#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QScriptEngine>
#include <QScriptValue>
#include <QDebug>

#include <openssl/evp.h>
#include <QRegExp>

// Zalo's wire JSON is inconsistent about how it encodes uids: some fields
// (e.g. top-level "uidFrom") are always quoted strings, but others — notably
// the nested quote object's "ownerId", and some "globalMsgId"/"msgId"
// fields — are emitted as BARE JSON numbers. Zalo uids are ~19-digit
// snowflake-style ints, which exceed 2^53 (~16 digits), the largest integer
// a JS/IEEE-754 double can represent exactly. Since JSON (de)serialization
// here goes through QScriptEngine (Qt4 has no QJson) — i.e. a REAL JS
// engine — any bare number that big silently gets rounded the moment
// JSON.parse touches it (confirmed: 860110201644973228 -> 860110201644973200),
// long before QVariant::toLongLong()/toString() downstream ever sees it. No
// amount of care in the C++ parsing code can recover the lost digits after
// that point.
//
// Fix: rewrite any bare (unquoted) integer literal of 16+ digits into a
// quoted JSON string BEFORE handing the text to JSON.parse, so it survives
// as an exact string instead of being coerced through a double. This only
// matches numbers in VALUE position (preceded by ':', '[' or ',' and
// followed by ',', ']' or '}', with only whitespace between) so it can't
// accidentally rewrite digits that happen to appear inside an existing
// quoted string.
inline QByteArray quoteBigJsonInts(const QByteArray &raw)
{
    QString s = QString::fromUtf8(raw);
    // (^|[:\[,])   - value position: start of a number after :, [ or ,
    // (\s*)        - optional whitespace before the number
    // (-?\d{16,})  - the big integer itself (16+ digits, i.e. large enough
    //                to risk exceeding 2^53's ~16 significant digits)
    // (\s*)        - optional whitespace after
    // ($|[,\]}])   - value position: end of a number before , ] or }
    QRegExp re("([:\\[,])(\\s*)(-?\\d{16,})(\\s*)([,\\]}])");
    re.setMinimal(true);
    int pos = 0;
    while ((pos = re.indexIn(s, pos)) != -1) {
        QString replacement = re.cap(1) + re.cap(2) + "\"" + re.cap(3) + "\"" + re.cap(4) + re.cap(5);
        s.replace(pos, re.matchedLength(), replacement);
        // Resume scanning right BEFORE the trailing delimiter we just
        // consumed, so that same delimiter can double as the LEADING
        // delimiter for a following number — needed for back-to-back big
        // ints like "[bigIdA,bigIdB]" where the middle "," is shared
        // between both matches.
        pos += replacement.length() - 1;
    }
    return s.toUtf8();
}

inline QVariantMap jsonToMap(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantMap();
    QByteArray safe = quoteBigJsonInts(trimmed);
    QScriptEngine eng;
    eng.evaluate("var __safeJSON = function(s){try{return JSON.parse(s);}catch(e){return null;}}");
    QScriptValue fn = eng.globalObject().property("__safeJSON");
    QScriptValue val = fn.call(QScriptValue(), QScriptValueList()
                               << eng.toScriptValue(QString::fromUtf8(safe)));
    if (!val.isValid() || val.isNull() || val.isUndefined() || val.isError())
        return QVariantMap();
    QVariant v = val.toVariant();
    if (v.type() == QVariant::Map)
        return v.toMap();
    return QVariantMap();
}

inline QVariantList jsonToList(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantList();
    QByteArray safe = quoteBigJsonInts(trimmed);
    QScriptEngine eng;
    eng.evaluate("var __safeJSON = function(s){try{return JSON.parse(s);}catch(e){return null;}}");
    QScriptValue fn = eng.globalObject().property("__safeJSON");
    QScriptValue val = fn.call(QScriptValue(), QScriptValueList()
                               << eng.toScriptValue(QString::fromUtf8(safe)));
    if (!val.isValid() || val.isNull() || val.isUndefined() || val.isError())
        return QVariantList();
    QVariant v = val.toVariant();
    if (v.type() == QVariant::List)
        return v.toList();
    return QVariantList();
}

inline QByteArray mapToJson(const QVariantMap &map)
{
    QScriptEngine eng;
    QScriptValue obj = eng.newObject();
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        QVariant v = it.value();
        switch (v.type()) {
        case QVariant::String:   obj.setProperty(it.key(), v.toString()); break;
        case QVariant::Int:
        case QVariant::LongLong: obj.setProperty(it.key(), (double)v.toLongLong()); break;
        case QVariant::UInt:
        case QVariant::ULongLong: obj.setProperty(it.key(), (double)v.toULongLong()); break;
        case QVariant::Bool:     obj.setProperty(it.key(), (bool)v.toBool()); break;
        case QVariant::Double:   obj.setProperty(it.key(), (double)v.toDouble()); break;
        case QVariant::List: {
            QVariantList lst = v.toList();
            QScriptValue arr = eng.newArray(lst.size());
            for (int i = 0; i < lst.size(); ++i) {
                QVariant elem = lst[i];
                switch (elem.type()) {
                case QVariant::Int:
                case QVariant::LongLong:
                    arr.setProperty(i, (double)elem.toLongLong());
                    break;
                case QVariant::UInt:
                case QVariant::ULongLong:
                    arr.setProperty(i, (double)elem.toULongLong());
                    break;
                case QVariant::Double:
                    arr.setProperty(i, elem.toDouble());
                    break;
                case QVariant::Bool:
                    arr.setProperty(i, elem.toBool());
                    break;
                default:
                    arr.setProperty(i, elem.toString());
                    break;
                }
            }
            obj.setProperty(it.key(), arr);
            break;
        }
        default: obj.setProperty(it.key(), v.toString()); break;
        }
    }
    QScriptValue jsonStringify = eng.evaluate("JSON.stringify");
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList() << obj);
    return result.toString().toUtf8();
}

// General-purpose recursive QVariant -> QScriptValue conversion, unlike the
// flat-only mapToJson() above. Needed for exportData()/importData(), whose
// payload is a root object containing arrays of (flat) message/quickMessage
// maps plus scalar metadata — e.g. {"exportedAt": "...", "messages": [ {...},
// {...} ], "quickMessages": [ {...} ]}. Kept separate from mapToJson() rather
// than rewriting it, since every existing call site of mapToJson() is part of
// the login/messaging wire protocol and shouldn't change behavior.
inline QScriptValue variantToScriptValue(QScriptEngine &eng, const QVariant &v)
{
    switch (v.type()) {
    case QVariant::Map: {
        QVariantMap m = v.toMap();
        QScriptValue obj = eng.newObject();
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it)
            obj.setProperty(it.key(), variantToScriptValue(eng, it.value()));
        return obj;
    }
    case QVariant::List: {
        QVariantList lst = v.toList();
        QScriptValue arr = eng.newArray(lst.size());
        for (int i = 0; i < lst.size(); ++i)
            arr.setProperty(i, variantToScriptValue(eng, lst[i]));
        return arr;
    }
    case QVariant::Int:
    case QVariant::LongLong:  return QScriptValue(eng.toScriptValue((double)v.toLongLong()));
    case QVariant::UInt:
    case QVariant::ULongLong: return QScriptValue(eng.toScriptValue((double)v.toULongLong()));
    case QVariant::Double:    return QScriptValue(eng.toScriptValue(v.toDouble()));
    case QVariant::Bool:      return QScriptValue(v.toBool());
    default:                  return QScriptValue(v.toString());
    }
}

inline QByteArray variantToJsonPretty(const QVariant &root)
{
    QScriptEngine eng;
    QScriptValue val = variantToScriptValue(eng, root);
    QScriptValue jsonStringify = eng.evaluate("JSON.stringify");
    // 2-space indent so an exported file is still human-readable if someone opens it.
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList()
                                              << val << QScriptValue() << QScriptValue(2));
    return result.toString().toUtf8();
}

// Compact (no indentation) counterpart of variantToJsonPretty(), for wire-protocol
// payloads that get AES-encrypted afterwards. mapToJson() above can't be reused
// here because its QVariant::List branch flattens every element via toString(),
// which corrupts arrays of objects (e.g. deleteMessage()'s "msgs": [ {cliMsgId,
// globalMsgId,...} ]) instead of recursing into them like this one does.
inline QByteArray variantToJsonCompact(const QVariant &root)
{
    QScriptEngine eng;
    QScriptValue val = variantToScriptValue(eng, root);
    QScriptValue jsonStringify = eng.evaluate("JSON.stringify");
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList() << val);
    return result.toString().toUtf8();
}

inline QString normalizePhotoContent(const QVariantMap &m, const QString &rawContent)
{
    QString nUrl, hUrl, tUrl;

    // Capture the caption BEFORE the logic below discards everything except
    // the photo URLs. Zalo's real client lets you attach a text caption to a
    // photo — it arrives as "title" inside the photo's content object (with
    // "description" used by a few message shapes instead). Without this, a
    // photo sent together with a caption shows only the image and silently
    // drops the text everywhere downstream.
    QString caption;
    {
        QVariantMap rawCm = m["content"].toMap();
        if (rawCm.isEmpty() && !rawContent.isEmpty() && rawContent.trimmed().startsWith("{"))
            rawCm = jsonToMap(rawContent.toUtf8());
        caption = rawCm["title"].toString();
        if (caption.isEmpty()) caption = rawCm["description"].toString();
    }

    // 1. Try content JSON field first
    if (!rawContent.isEmpty() && rawContent.trimmed().startsWith("{")) {
        QVariantMap cm = jsonToMap(rawContent.toUtf8());
        // normalUrl can be a protobuf blob instead of a URL, so href/oriUrl is the
        // more reliable CDN link — only trust normalUrl when it's actually an http URL.
        QString nu = cm["normalUrl"].toString();
        if (nu.startsWith("http")) {
            nUrl = nu;
        } else {
            // normalUrl is protobuf blob — use href/oriUrl as real CDN URL instead
            nUrl = cm["href"].toString();
            if (nUrl.isEmpty()) nUrl = cm["oriUrl"].toString();
            if (nUrl.isEmpty() && nu.startsWith("http")) nUrl = nu; // fallback
        }
        hUrl = cm["hdUrl"].toString();
        if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
        if (hUrl.isEmpty()) hUrl = cm["href"].toString();
        tUrl = cm["thumbUrl"].toString();
        if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
        // If still empty, use whatever we have
        if (nUrl.isEmpty()) nUrl = nu;
    }

    // 2. Try top-level fields on message map
    if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
    if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
    if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumb"].toString();

    // 3. Try paramsExt JSON string (Zalo WS real-time photo delivery)
    if (nUrl.isEmpty() || !nUrl.startsWith("http")) {
        QString pe = m["paramsExt"].toString();
        if (!pe.isEmpty() && pe.trimmed().startsWith("{")) {
            QVariantMap pm = jsonToMap(pe.toUtf8());
            QString pnu = pm["normalUrl"].toString();
            if (pnu.startsWith("http")) nUrl = pnu;
            if (hUrl.isEmpty()) hUrl = pm["hdUrl"].toString();
            if (tUrl.isEmpty()) tUrl = pm["thumbUrl"].toString();
            if (!nUrl.startsWith("http")) {
                if (!pm["href"].toString().isEmpty()) nUrl = pm["href"].toString();
                if (!pm["oriUrl"].toString().isEmpty() && nUrl.isEmpty())
                    nUrl = pm["oriUrl"].toString();
            }
            if (tUrl.isEmpty()) tUrl = pm["thumb"].toString();
        }
    }

    // 4. Try attach sub-object
    if (nUrl.isEmpty() || !nUrl.startsWith("http")) {
        QVariantMap att = m["attach"].toMap();
        if (att.isEmpty()) {
            QString attStr = m["attach"].toString();
            if (!attStr.isEmpty() && attStr.startsWith("{"))
                att = jsonToMap(attStr.toUtf8());
        }
        if (!att.isEmpty()) {
            QString anu = att["normalUrl"].toString();
            if (anu.startsWith("http")) nUrl = anu;
            if (hUrl.isEmpty()) hUrl = att["hdUrl"].toString();
            if (tUrl.isEmpty()) tUrl = att["thumbUrl"].toString();
            if (!nUrl.startsWith("http")) {
                if (!att["href"].toString().isEmpty()) nUrl = att["href"].toString();
            }
            if (tUrl.isEmpty()) tUrl = att["thumb"].toString();
        }
    }

    // 5. previewThumb as last resort (may be CDN URL or base64)
    if (nUrl.isEmpty()) {
        QString pt = m["previewThumb"].toString();
        if (!pt.isEmpty()) { nUrl = pt; tUrl = pt; }
    }

    if (nUrl.isEmpty()) nUrl = hUrl; if (nUrl.isEmpty()) nUrl = tUrl;
    if (tUrl.isEmpty()) tUrl = nUrl; if (hUrl.isEmpty()) hUrl = nUrl;
    if (nUrl.isEmpty()) return rawContent;
    if (!caption.isEmpty()) {
        QString esc = caption;
        esc.replace("\\", "\\\\").replace("\"", "\\\"")
           .replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t");
        return QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\",\"caption\":\"%4\"}")
               .arg(nUrl).arg(tUrl).arg(hUrl).arg(esc);
    }
    return QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
           .arg(nUrl).arg(tUrl).arg(hUrl);
}


// Zalo also sends a "chat.delete" event — the WS echo/notification for
// "delete for me" (deleteMessage.ts's onlyMe path). This is critically
// DIFFERENT from chat.undo (recall): chat.delete must only ever hide the
// message locally for whichever side actually pressed delete — it is NOT a
// broadcast "this message no longer exists for anyone" like undo is. But
// Zalo's WS still delivers a chat.delete notification to BOTH participants
// in the thread (confirmed from device logs: uidFrom=<deleter>,
// idTo=<thread>, delivered regardless of which side deleted). If we treated
// every chat.delete the same way we treat chat.undo (i.e. always call
// markMessageRecalled()), then person A deleting a message "for me only"
// would incorrectly also hide/tag it on person B's screen — the exact bug
// reported. So: extract who actually did the deleting (content[].uidFrom)
// and let the caller decide — only apply the local hide if that matches our
// own uid; otherwise this is someone else's "delete for me" and must be a
// complete no-op on our screen.
//
// content is a QVariantList of objects: [{type,actionType,uidFrom,uidTo,
// clientDelMsgId,globalDelMsgId,destId}]. Returns true and fills outMsgId /
// outDeleterUid if m is a chat.delete event; false (untouched outputs) otherwise.
inline bool extractDeleteInfo(const QVariantMap &m, QString &outMsgId, QString &outDeleterUid)
{
    QString msgTypeStr = m.value("msgType").toString();
    if (msgTypeStr.compare("chat.delete", Qt::CaseInsensitive) != 0)
        return false;

    QVariant contentVar = m.value("content");
    QVariantList items = contentVar.toList();
    if (items.isEmpty()) {
        QString cs = contentVar.toString();
        if (!cs.isEmpty() && cs.trimmed().startsWith("["))
            items = jsonToList(cs.toUtf8());
    }
    if (items.isEmpty()) return false;

    QVariantMap first = items.first().toMap();
    QString delId = first.value("globalDelMsgId").toString();
    if (delId.isEmpty()) delId = first.value("clientDelMsgId").toString();
    if (delId.isEmpty()) return false;

    outMsgId      = delId;
    outDeleterUid = first.value("uidFrom").toString();
    return true;
}

// Zalo sends a separate "chat.undo" event when a message is recalled/unsent.
// Its content is {"globalMsgId":..., "cliMsgId":..., "deleteMsg":..., "srcId":..., "destId":...}
// referencing the ORIGINAL message's msgId — it is not a message of its own.
// Returns the original msgId being recalled, or an empty string if m isn't a recall event.
inline QString extractRecalledMsgId(const QVariantMap &m)
{
    QString msgTypeStr = m.value("msgType").toString();
    if (msgTypeStr.compare("chat.undo", Qt::CaseInsensitive) != 0)
        return QString();

    // content arrives as a nested QVariantMap (QScriptEngine auto-converts JSON
    // objects), but may also already be a JSON string depending on the call path.
    QVariantMap c = m.value("content").toMap();
    if (c.isEmpty()) {
        QString cs = m.value("content").toString();
        if (!cs.isEmpty() && cs.trimmed().startsWith("{"))
            c = jsonToMap(cs.toUtf8());
    }
    QString recalledId = c.value("globalMsgId").toString();
    if (recalledId.isEmpty()) recalledId = c.value("cliMsgId").toString();
    return recalledId;
}

// AES-GCM decrypt cho WS event data (zca-js decodeEventData, encryptType=2/3)
// Layout: iv[0:16] + aad[16:32] + ciphertext[32:N-16] + tag[N-16:N]
// encryptType=2: base64(urlencoded(data)) → inflate(plaintext)
// encryptType=3: base64(data)            → plaintext trực tiếp (no inflate)
inline QByteArray aesGcmDecrypt(const QByteArray &keyRaw, const QByteArray &cipherBytes)
{
    if (cipherBytes.size() < 48) return QByteArray(); // iv(16)+aad(16)+tag(16) minimum
    QByteArray key = keyRaw;
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) {
        key = QByteArray::fromBase64(keyRaw);
    }
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return QByteArray();

    const unsigned char *iv  = (const unsigned char*)cipherBytes.constData();       // bytes 0-15
    const unsigned char *aad = (const unsigned char*)cipherBytes.constData() + 16;  // bytes 16-31
    int cipherLen = cipherBytes.size() - 32 - 16;
    if (cipherLen <= 0) return QByteArray();
    const unsigned char *cipher = (const unsigned char*)cipherBytes.constData() + 32;
    const unsigned char *tag    = (const unsigned char*)cipherBytes.constData() + 32 + cipherLen;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    const EVP_CIPHER *cipher_type = (key.size() == 16) ? EVP_aes_128_gcm()
                                  : (key.size() == 24) ? EVP_aes_192_gcm()
                                  :                      EVP_aes_256_gcm();
    EVP_DecryptInit_ex(ctx, cipher_type, NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL,
        (const unsigned char*)key.constData(), iv);
    // AAD
    int len = 0;
    EVP_DecryptUpdate(ctx, NULL, &len, aad, 16);
    // Decrypt
    QByteArray out(cipherLen + 16, '\0');
    EVP_DecryptUpdate(ctx, (unsigned char*)out.data(), &len,
        cipher, cipherLen);
    int outLen = len;
    // Set tag
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
    int ret = EVP_DecryptFinal_ex(ctx, (unsigned char*)out.data() + outLen, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) {
        qDebug() << "[Zalo WS] aesGcmDecrypt: EVP_DecryptFinal FAILED ret=" << ret
                 << "keyLen=" << key.size() << "cipherLen=" << cipherLen
                 << "tagHex=" << QByteArray((const char*)tag, 16).toHex();
        return QByteArray(); // tag mismatch
    }
    out.resize(outLen + len);
    return out;
}


#endif // ZALOSERVICEUTILS_HPP
