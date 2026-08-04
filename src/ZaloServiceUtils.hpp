#ifndef ZALOSERVICEUTILS_HPP
#define ZALOSERVICEUTILS_HPP

// Helper JSON/crypto nhỏ dùng chung cho các file ZaloService_*.cpp.
// Qt4 không có QJson nên (de)serialize JSON qua QScriptEngine; các hàm
// wrapper ở đây gom logic đó lại 1 chỗ. Tất cả đều `inline` vì header này
// được include ở nhiều file .cpp.

#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QScriptEngine>
#include <QScriptValue>
#include <QDebug>

#include <openssl/evp.h>
#include <QRegExp>

// JSON của Zalo không nhất quán khi encode uid: có field luôn là chuỗi có
// quote, nhưng một số field khác (vd "ownerId" trong quote object, một số
// "globalMsgId"/"msgId") lại là số JSON trần không quote. Uid của Zalo dài
// ~19 chữ số, vượt quá 2^53 mà double (IEEE-754) biểu diễn chính xác được.
// Vì parse JSON ở đây đi qua QScriptEngine (1 JS engine thật), số lớn cỡ đó
// sẽ bị làm tròn ngay khi JSON.parse chạm vào (đã xác nhận qua thực tế),
// trước cả khi C++ đọc được.
//
// Fix: viết lại số nguyên trần từ 16 chữ số trở lên thành chuỗi có quote,
// TRƯỚC khi đưa cho JSON.parse, để giữ nguyên giá trị dạng string thay vì
// bị ép qua double. Chỉ khớp số ở vị trí VALUE (sau ':', '[' hoặc ',' và
// trước ',', ']' hoặc '}') để không lỡ tay sửa số nằm trong 1 chuỗi có sẵn.
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
        // Tiếp tục quét NGAY TRƯỚC delimiter cuối vừa dùng, để delimiter đó
        // có thể dùng lại làm delimiter ĐẦU cho số tiếp theo — cần cho
        // trường hợp 2 số lớn liền nhau kiểu "[bigIdA,bigIdB]".
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

// Chuyển đổi QVariant -> QScriptValue đệ quy tổng quát, khác mapToJson()
// ở trên (chỉ xử lý dữ liệu phẳng). Cần cho exportData()/importData(), có
// payload là object gốc chứa mảng message/quickMessage lồng nhau kèm
// metadata. Giữ tách riêng khỏi mapToJson() vì các nơi đang dùng
// mapToJson() thuộc wire protocol login/messaging, không nên đổi hành vi.
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
    // Indent 2-space để file export vẫn đọc được nếu ai đó mở lên xem.
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList()
                                              << val << QScriptValue() << QScriptValue(2));
    return result.toString().toUtf8();
}

// Bản compact (không indent) của variantToJsonPretty(), dùng cho payload
// wire-protocol sẽ bị AES-encrypt sau đó. Không dùng lại mapToJson() được
// vì nhánh QVariant::List của nó flatten mọi phần tử qua toString(), làm
// hỏng mảng object (vd "msgs": [ {cliMsgId, globalMsgId,...} ]).
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

    // Lấy caption TRƯỚC khi logic bên dưới bỏ qua mọi thứ trừ URL ảnh.
    // Zalo cho phép gửi ảnh kèm caption text, nằm trong "title" (hoặc
    // "description" ở vài dạng tin nhắn khác) của content object.
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
        // normalUrl có thể là blob protobuf thay vì URL, nên href/oriUrl
        // đáng tin hơn — chỉ dùng normalUrl khi nó thực sự là URL http.
        QString nu = cm["normalUrl"].toString();
        if (nu.startsWith("http")) {
            nUrl = nu;
        } else {
            // normalUrl là blob protobuf — dùng href/oriUrl làm URL CDN thật
            nUrl = cm["href"].toString();
            if (nUrl.isEmpty()) nUrl = cm["oriUrl"].toString();
            if (nUrl.isEmpty() && nu.startsWith("http")) nUrl = nu; // fallback
        }
        hUrl = cm["hdUrl"].toString();
        if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
        if (hUrl.isEmpty()) hUrl = cm["href"].toString();
        tUrl = cm["thumbUrl"].toString();
        if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
        // Nếu vẫn rỗng thì dùng tạm cái gì có sẵn
        if (nUrl.isEmpty()) nUrl = nu;
    }

    // 2. Thử field ở top-level của message map
    if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
    if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
    if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumb"].toString();

    // 3. Thử paramsExt JSON string (ảnh gửi real-time qua Zalo WS)
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


// Zalo còn gửi event "chat.delete" — echo/notification cho "xóa cho tôi"
// (onlyMe). Khác chat.undo (thu hồi): chat.delete chỉ nên ẩn tin nhắn ở
// phía người bấm xóa, KHÔNG phải broadcast "tin này không còn tồn tại cho
// ai cả" như undo. Nhưng WS của Zalo vẫn gửi chat.delete cho CẢ 2 phía
// trong thread. Nếu xử lý chat.delete giống hệt chat.undo thì người A xóa
// "chỉ cho mình" sẽ vô tình ẩn/gắn tag luôn ở màn hình người B. Nên: trích
// ra ai thực sự bấm xóa (content[].uidFrom), để caller tự quyết — chỉ ẩn
// local nếu khớp uid của mình, còn không thì bỏ qua hoàn toàn.
//
// content là QVariantList các object: [{type,actionType,uidFrom,uidTo,
// clientDelMsgId,globalDelMsgId,destId}]. Trả về true và điền outMsgId/
// outDeleterUid nếu m là event chat.delete; false nếu không phải.
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

// Quote chuỗi JSON tối giản (escape backslash/quote/ký tự điều khiển, bọc
// trong "..."). Dùng khi tự tay build 1 đoạn JSON dưới dạng QString thay vì
// đi qua mapToJson() — vd payload "message" trong reactMessage(), cần
// gMsgID/cMsgID là số nguyên trần, không qua cast LongLong -> double.
inline QString jsonQuote(const QString &s)
{
    QString out;
    out.reserve(s.size() + 2);
    out.append('"');
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s.at(i);
        switch (c.unicode()) {
        case '"':  out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\n': out.append("\\n");  break;
        case '\r': out.append("\\r");  break;
        case '\t': out.append("\\t");  break;
        default:
            if (c.unicode() < 0x20) {
                out.append(QString("\\u%1").arg(c.unicode(), 4, 16, QChar('0')));
            } else {
                out.append(c);
            }
        }
    }
    out.append('"');
    return out;
}

// Map icon-id <-> ký hiệu wire cho reaction — "like"/"heart"/"haha"/"wow"/
// "cry"/"angry" ở 1 phía, ký hiệu text ngắn Zalo dùng làm rIcon ở phía kia
// (vd "/-heart", ":>" — không phải emoji Unicode). Đã xác nhận qua log
// thiết bị thật khi nhận cmd=612 reaction, khớp với enum Reactions của
// zca-js. Bản mapping cũ dùng emoji Unicode tự đặt ở cả 2 phía nên không
// khớp gì cả — mọi reaction round-trip đều rỗng/không nhận diện được.
// Một nguồn duy nhất cho cả 2 chiều: reactMessage() (gửi đi, icon -> rIcon)
// và handler cmd=612 (nhận về, rIcon -> icon).
inline QString reactionIconToEmoji(const QString &icon)
{
    if (icon == "like")  return QString::fromUtf8("/-strong"); // Reactions.LIKE
    if (icon == "heart") return QString::fromUtf8("/-heart");  // Reactions.HEART
    if (icon == "haha")  return QString::fromUtf8(":>");       // Reactions.HAHA
    if (icon == "wow")   return QString::fromUtf8(":o");       // Reactions.WOW
    if (icon == "cry")   return QString::fromUtf8(":-((");     // Reactions.CRY
    if (icon == "angry") return QString::fromUtf8(":-h");      // Reactions.ANGRY
    return QString();
}
inline QString emojiToReactionIcon(const QString &emoji)
{
    if (emoji == reactionIconToEmoji("like"))  return "like";
    if (emoji == reactionIconToEmoji("heart")) return "heart";
    if (emoji == reactionIconToEmoji("haha"))  return "haha";
    if (emoji == reactionIconToEmoji("wow"))   return "wow";
    if (emoji == reactionIconToEmoji("cry"))   return "cry";
    if (emoji == reactionIconToEmoji("angry")) return "angry";
    return QString();
}

// rType companion to the icon<->rIcon mapping above — Zalo's numeric
// reaction-type index does NOT follow this app's own 0..5 slot order.
// Confirmed against zca-js's addReactionFactory() switch statement (the
// same source the rIcon symbols above were cross-checked against) and the
// Log thiết bị thật cho thấy rType:5 là reaction heart, khớp với rType=5
// của Zalo (Reactions.HEART). Quy ước cũ trong QML (like=0 heart=1 haha=2
// wow=3 cry=4 angry=5, tức thứ tự hiển thị) không phải số thật của Zalo —
// reactMessage() giờ lấy rType từ đây thay vì từ map đó.
inline int reactionIconToRType(const QString &icon)
{
    if (icon == "haha")  return 0;
    if (icon == "cry")   return 2;
    if (icon == "like")  return 3;
    if (icon == "heart") return 5;
    if (icon == "angry") return 20;
    if (icon == "wow")   return 32;
    return -1;
}

// Tương tự extractDeleteInfo/extractRecalledMsgId, cho reaction push event.
// "chat.reaction" là tên tự đặt (chưa xác nhận qua traffic thật như
// "chat.undo"/"chat.delete"), theo quy ước "chat.<action>" Zalo đang dùng,
// tới khi có bằng chứng khác. Content dạng {globalMsgId hoặc msgId, uidFrom,
// rIcon, rType} (cũng là suy đoán). rType < 0 hoặc rIcon rỗng nghĩa là uid
// đó vừa gỡ reaction — outIcon để rỗng trong trường hợp đó. Trả về true và
// điền output nếu m giống 1 reaction event; false nếu không.
inline bool extractReactionInfo(const QVariantMap &m, QString &outMsgId, QString &outUid, QString &outIcon)
{
    QString msgTypeStr = m.value("msgType").toString();
    if (msgTypeStr.compare("chat.reaction", Qt::CaseInsensitive) != 0)
        return false;

    QVariantMap content = m.value("content").toMap();
    if (content.isEmpty()) {
        QString cs = m.value("content").toString();
        if (!cs.isEmpty() && cs.trimmed().startsWith("{"))
            content = jsonToMap(cs.toUtf8());
    }
    if (content.isEmpty()) return false;

    QString msgId = content.value("globalMsgId").toString();
    if (msgId.isEmpty()) msgId = content.value("msgId").toString();
    if (msgId.isEmpty()) return false;

    outMsgId = msgId;
    outUid   = content.value("uidFrom").toString();

    int rType = content.value("rType").toInt();
    QString rIcon = content.value("rIcon").toString();
    outIcon = (rType < 0 || rIcon.isEmpty()) ? QString() : emojiToReactionIcon(rIcon);
    return true;
}


// Content dạng {"globalMsgId":..., "cliMsgId":..., "deleteMsg":..., "srcId":...,
// "destId":...} trỏ tới msgId của tin nhắn GỐC — không phải tin nhắn mới.
// Trả về msgId gốc đang bị thu hồi, hoặc rỗng nếu m không phải recall event.
inline QString extractRecalledMsgId(const QVariantMap &m)
{
    QString msgTypeStr = m.value("msgType").toString();
    if (msgTypeStr.compare("chat.undo", Qt::CaseInsensitive) != 0)
        return QString();

    // content đến dạng QVariantMap lồng (QScriptEngine tự convert JSON
    // object), nhưng cũng có thể là chuỗi JSON tùy đường gọi.
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
