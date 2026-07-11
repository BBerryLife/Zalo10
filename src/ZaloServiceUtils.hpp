#ifndef ZALOSERVICEUTILS_HPP
#define ZALOSERVICEUTILS_HPP

// Small JSON/crypto helpers shared by the ZaloService_*.cpp translation units.
// Qt4 has no QJson. JSON (de)serialization used to go through QScriptEngine,
// but QtScript's JIT can't reserve executable memory inside a headless
// (non-Cascades) bb::Application process on QNX — the first evaluate() call
// segfaults (SIGSEGV inside libQtScript.so, "Could not reserve register file
// memory"). Since HeadlessService is exactly that kind of process, every
// (de)serialization helper below is now a hand-rolled recursive-descent
// parser/serializer with no script engine involved. Everything here stays
// `inline` since the header is included from multiple .cpp files.

#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QDebug>

#include <openssl/evp.h>

// ---------------------------------------------------------------------------
// Minimal hand-written JSON parser (no QScriptEngine/JIT). Supports objects,
// arrays, strings (with standard escapes incl. \uXXXX), numbers, true/false/
// null — enough for every payload this app sends/receives. Numbers are always
// stored as double in the resulting QVariant (matching what QScriptEngine
// used to produce); callers already go through toInt()/toString() etc.
// ---------------------------------------------------------------------------
namespace ZJson {

class Parser
{
public:
    Parser(const QString &text) : s(text), pos(0), len(text.length()), ok(true) {}

    QVariant parseValue()
    {
        skipWs();
        if (pos >= len) { ok = false; return QVariant(); }
        QChar c = s.at(pos);
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || c.isDigit()) return parseNumber();
        ok = false;
        return QVariant();
    }

    bool isOk() const { return ok; }

private:
    const QString &s;
    int pos;
    int len;
    bool ok;

    void skipWs()
    {
        while (pos < len) {
            QChar c = s.at(pos);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    bool expect(QChar c)
    {
        skipWs();
        if (pos >= len || s.at(pos) != c) { ok = false; return false; }
        ++pos;
        return true;
    }

    QVariant parseObject()
    {
        QVariantMap map;
        if (!expect('{')) return map;
        skipWs();
        if (pos < len && s.at(pos) == '}') { ++pos; return map; }
        while (ok) {
            skipWs();
            if (pos >= len || s.at(pos) != '"') { ok = false; break; }
            QString key = parseString().toString();
            if (!ok) break;
            if (!expect(':')) break;
            QVariant val = parseValue();
            if (!ok) break;
            map[key] = val;
            skipWs();
            if (pos < len && s.at(pos) == ',') { ++pos; continue; }
            if (pos < len && s.at(pos) == '}') { ++pos; break; }
            ok = false;
            break;
        }
        return map;
    }

    QVariant parseArray()
    {
        QVariantList list;
        if (!expect('[')) return list;
        skipWs();
        if (pos < len && s.at(pos) == ']') { ++pos; return list; }
        while (ok) {
            QVariant val = parseValue();
            if (!ok) break;
            list << val;
            skipWs();
            if (pos < len && s.at(pos) == ',') { ++pos; continue; }
            if (pos < len && s.at(pos) == ']') { ++pos; break; }
            ok = false;
            break;
        }
        return list;
    }

    QVariant parseString()
    {
        if (!expect('"')) return QString();

        // Fast path: hầu hết chuỗi trong payload của app này (đặc biệt là
        // field "data" chứa base64 dài, có thể vài chục KB với fetchFriends
        // count=20000) không có ký tự escape nào. Quét thẳng tới dấu " đóng
        // rồi cắt 1 lần bằng QString::mid() — 1 allocation duy nhất, thay vì
        // nối từng ký tự (out += c) vốn có thể gây cấp phát lại liên tục và
        // dồn ép bộ nhớ trên môi trường giới hạn RAM như simulator BB10
        // (đây chính là nguồn gốc "bad allocation" quan sát được khi parse
        // outer JSON của fetchFriends).
        int start = pos;
        int i = pos;
        while (i < len) {
            QChar c = s.at(i);
            if (c == '"') {
                QString result = s.mid(start, i - start);
                pos = i + 1;
                return result;
            }
            if (c == '\\') break; // cần xử lý escape -> rơi xuống slow path
            ++i;
        }

        // Slow path: có escape sequence trong chuỗi (hoặc chuỗi không đóng
        // đúng) — decode từng ký tự như trước, chỉ áp dụng cho phần còn lại
        // kể từ vị trí start (giữ nguyên hành vi cũ, chỉ ảnh hưởng phần nhỏ
        // của payload thường chứa escape, ví dụ tên có ký tự đặc biệt).
        QString out = s.mid(start, i - start); // phần escape-free đã quét được ở trên
        pos = i;
        while (pos < len) {
            QChar c = s.at(pos);
            if (c == '"') { ++pos; return out; }
            if (c == '\\') {
                ++pos;
                if (pos >= len) { ok = false; break; }
                QChar e = s.at(pos);
                switch (e.toLatin1()) {
                case '"':  out += '"';  ++pos; break;
                case '\\': out += '\\'; ++pos; break;
                case '/':  out += '/';  ++pos; break;
                case 'b':  out += '\b'; ++pos; break;
                case 'f':  out += '\f'; ++pos; break;
                case 'n':  out += '\n'; ++pos; break;
                case 'r':  out += '\r'; ++pos; break;
                case 't':  out += '\t'; ++pos; break;
                case 'u': {
                    if (pos + 4 >= len) { ok = false; break; }
                    QString hex = s.mid(pos + 1, 4);
                    bool okHex = false;
                    ushort code = hex.toUShort(&okHex, 16);
                    if (!okHex) { ok = false; break; }
                    out += QChar(code);
                    pos += 5;
                    break;
                }
                default:
                    ok = false;
                    break;
                }
                if (!ok) break;
            } else {
                out += c;
                ++pos;
            }
        }
        if (pos >= len) ok = false; // unterminated string
        return out;
    }

    QVariant parseBool()
    {
        if (s.mid(pos, 4) == "true")  { pos += 4; return true; }
        if (s.mid(pos, 5) == "false") { pos += 5; return false; }
        ok = false;
        return QVariant();
    }

    QVariant parseNull()
    {
        if (s.mid(pos, 4) == "null") { pos += 4; return QVariant(); }
        ok = false;
        return QVariant();
    }

    QVariant parseNumber()
    {
        int start = pos;
        if (pos < len && s.at(pos) == '-') ++pos;
        while (pos < len && s.at(pos).isDigit()) ++pos;
        if (pos < len && s.at(pos) == '.') {
            ++pos;
            while (pos < len && s.at(pos).isDigit()) ++pos;
        }
        if (pos < len && (s.at(pos) == 'e' || s.at(pos) == 'E')) {
            ++pos;
            if (pos < len && (s.at(pos) == '+' || s.at(pos) == '-')) ++pos;
            while (pos < len && s.at(pos).isDigit()) ++pos;
        }
        if (pos == start) { ok = false; return QVariant(); }
        bool convOk = false;
        double d = s.mid(start, pos - start).toDouble(&convOk);
        if (!convOk) { ok = false; return QVariant(); }
        return d;
    }
};

inline QVariant parse(const QString &text, bool *okOut = 0)
{
    Parser p(text);
    QVariant v = p.parseValue();
    if (okOut) *okOut = p.isOk();
    return p.isOk() ? v : QVariant();
}

inline QString escape(const QString &s)
{
    QString out;
    out.reserve(s.length() + 8);
    for (int i = 0; i < s.length(); ++i) {
        QChar c = s.at(i);
        switch (c.toLatin1()) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c.unicode() < 0x20) {
                out += QString("\\u%1").arg((int)c.unicode(), 4, 16, QChar('0'));
            } else {
                out += c;
            }
        }
    }
    return out;
}

// Recursive QVariant -> JSON string serializer. indent<0 = compact (no
// whitespace), indent>=0 = pretty-print with that many spaces per level.
inline QString serialize(const QVariant &v, int indent, int depth)
{
    const QString nl  = indent >= 0 ? "\n" : "";
    const QString pad = indent >= 0 ? QString(indent * (depth + 1), ' ') : QString();
    const QString padEnd = indent >= 0 ? QString(indent * depth, ' ') : QString();
    const QString colon = indent >= 0 ? ": " : ":";

    switch (v.type()) {
    case QVariant::Map: {
        QVariantMap m = v.toMap();
        if (m.isEmpty()) return "{}";
        QStringList parts;
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it) {
            parts << pad + "\"" + escape(it.key()) + "\"" + colon + serialize(it.value(), indent, depth + 1);
        }
        return "{" + nl + parts.join("," + nl) + nl + padEnd + "}";
    }
    case QVariant::List: {
        QVariantList lst = v.toList();
        if (lst.isEmpty()) return "[]";
        QStringList parts;
        for (int i = 0; i < lst.size(); ++i)
            parts << pad + serialize(lst.at(i), indent, depth + 1);
        return "[" + nl + parts.join("," + nl) + nl + padEnd + "]";
    }
    case QVariant::StringList: {
        QStringList lst = v.toStringList();
        if (lst.isEmpty()) return "[]";
        QStringList parts;
        for (int i = 0; i < lst.size(); ++i)
            parts << pad + "\"" + escape(lst.at(i)) + "\"";
        return "[" + nl + parts.join("," + nl) + nl + padEnd + "]";
    }
    case QVariant::Bool:
        return v.toBool() ? "true" : "false";
    case QVariant::Int:
    case QVariant::LongLong:
        return QString::number(v.toLongLong());
    case QVariant::UInt:
    case QVariant::ULongLong:
        return QString::number(v.toULongLong());
    case QVariant::Double:
        return QString::number(v.toDouble(), 'g', 17);
    case QVariant::Invalid:
        return "null";
    default:
        return "\"" + escape(v.toString()) + "\"";
    }
}

} // namespace ZJson

inline QVariantMap jsonToMap(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('<')) return QVariantMap();
    bool ok = false;
    QVariant v = ZJson::parse(trimmed, &ok);
    if (!ok || v.type() != QVariant::Map) return QVariantMap();
    return v.toMap();
}

inline QVariantMap jsonToMap(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantMap();
    return jsonToMap(QString::fromUtf8(trimmed));
}

inline QVariantList jsonToList(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('<')) return QVariantList();
    bool ok = false;
    QVariant v = ZJson::parse(trimmed, &ok);
    if (!ok || v.type() != QVariant::List) return QVariantList();
    return v.toList();
}

inline QVariantList jsonToList(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantList();
    return jsonToList(QString::fromUtf8(trimmed));
}

inline QByteArray mapToJson(const QVariantMap &map)
{
    // Historically flattened List values via toString() (matching the old
    // QScriptEngine-based behavior of this specific helper); keep that quirk
    // so existing wire-protocol call sites don't change shape. Callers that
    // need real nested arrays/objects use variantToJsonCompact() instead.
    QVariantMap flat;
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        QVariant v = it.value();
        if (v.type() == QVariant::List) {
            QVariantList lst = v.toList();
            QStringList asStrings;
            for (int i = 0; i < lst.size(); ++i) asStrings << lst[i].toString();
            flat[it.key()] = asStrings;
        } else {
            flat[it.key()] = v;
        }
    }
    return ZJson::serialize(flat, -1, 0).toUtf8();
}

// General-purpose recursive QVariant JSON serialization, unlike the
// flat-only mapToJson() above. Needed for exportData()/importData(), whose
// payload is a root object containing arrays of (flat) message/quickMessage
// maps plus scalar metadata — e.g. {"exportedAt": "...", "messages": [ {...},
// {...} ], "quickMessages": [ {...} ]}. Kept separate from mapToJson() rather
// than rewriting it, since every existing call site of mapToJson() is part of
// the login/messaging wire protocol and shouldn't change behavior.
inline QByteArray variantToJsonPretty(const QVariant &root)
{
    // 2-space indent so an exported file is still human-readable if someone opens it.
    return ZJson::serialize(root, 2, 0).toUtf8();
}

// Compact (no indentation) counterpart of variantToJsonPretty(), for wire-protocol
// payloads that get AES-encrypted afterwards. mapToJson() above can't be reused
// here because its QVariant::List branch flattens every element via toString(),
// which corrupts arrays of objects (e.g. deleteMessage()'s "msgs": [ {cliMsgId,
// globalMsgId,...} ]) instead of recursing into them like this one does.
inline QByteArray variantToJsonCompact(const QVariant &root)
{
    return ZJson::serialize(root, -1, 0).toUtf8();
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

    // content arrives as a nested QVariantMap (the JSON parser converts JSON
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
