#ifndef ZALOSERVICEUTILS_HPP
#define ZALOSERVICEUTILS_HPP

// Small JSON/crypto helpers shared by ZaloService_*.cpp. Qt4 has no QJson, so JSON goes through
// QScriptEngine. Everything is inline (header included from many .cpp files).

#include <QString>
#include <QByteArray>
#include <QVariant>
#include <QScriptEngine>
#include <QScriptValue>
#include <QDebug>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

#include <openssl/evp.h>
#include <QRegExp>

// Zalo uids are ~19 digits (> 2^53) and sometimes sent as bare JSON numbers, which QScriptEngine
// rounds during JSON.parse. Integers of 16+ digits in value position are rewritten as quoted
// strings before parsing.
inline QByteArray quoteBigJsonInts(const QByteArray &raw)
{
    // Single left-to-right scan tracking string literals: a regex can't tell a big number inside a
    // string (e.g. the nested JSON of "group.poll") and corrupted it, dropping whole WS frames
    // (live poll votes never arrived).
    QString s = QString::fromUtf8(raw);
    const int n = s.length();
    QString out;
    out.reserve(n + 64);
    int i = 0;
    QChar prev = QLatin1Char('{');
    while (i < n) {
        QChar c = s.at(i);
        if (c == QLatin1Char('"')) {
            int j = i + 1;
            while (j < n) {
                QChar cj = s.at(j);
                if (cj == QLatin1Char('\\')) { j += 2; continue; }
                if (cj == QLatin1Char('"')) break;
                ++j;
            }
            if (j >= n) j = n - 1;
            out.append(s.mid(i, j - i + 1));
            i = j + 1;
            prev = QLatin1Char('"');
            continue;
        }
        if ((c == QLatin1Char('-') || c.isDigit())
            && (prev == QLatin1Char(':') || prev == QLatin1Char('[') || prev == QLatin1Char(','))) {
            int j = i;
            if (s.at(j) == QLatin1Char('-')) ++j;
            const int digitsStart = j;
            while (j < n && s.at(j).isDigit()) ++j;
            const int digits = j - digitsStart;
            int k = j;
            while (k < n && s.at(k).isSpace()) ++k;
            const bool endsInt = (k < n && (s.at(k) == QLatin1Char(',') || s.at(k) == QLatin1Char(']')
                                            || s.at(k) == QLatin1Char('}')));
            if (digits >= 16 && endsInt) {
                out.append(QLatin1Char('"'));
                out.append(s.mid(i, j - i));
                out.append(QLatin1Char('"'));
            } else {
                out.append(s.mid(i, j - i));
            }
            i = j;
            prev = QLatin1Char('0');
            continue;
        }
        out.append(c);
        if (!c.isSpace()) prev = c;
        ++i;
    }
    return out.toUtf8();
}

// Native JSON (no QtScript) for the headless service: QtScript segfaults on its first evaluate()
// in a headless bb::Application process on QNX. The service sets ZJson::nativeMode() = true in
// main(). Numbers become double, as with QScriptValue::toVariant().
namespace ZJson {

inline bool &nativeMode() { static bool v = false; return v; }

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

        // Fast path: most strings have no escapes, so one mid() avoids per-char appends
        // (which caused bad_alloc on long base64).
        int start = pos;
        int i = pos;
        while (i < len) {
            QChar c = s.at(i);
            if (c == '"') {
                QString result = s.mid(start, i - start);
                pos = i + 1;
                return result;
            }
            if (c == '\\') break;
            ++i;
        }

        QString out = s.mid(start, i - start);
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
        if (pos >= len) ok = false;
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

inline QString numberToJson(double d)
{
    if (d != d || d - d != 0) return QString("null");
    if (d > -1e15 && d < 1e15 && d == (double)(qlonglong)d)
        return QString::number((qlonglong)d);
    QString s = QString::number(d, 'g', 15);
    if (s.toDouble() != d) s = QString::number(d, 'g', 17);
    return s;
}

inline QString serialize(const QVariant &v, int indent, int depth)
{
    const QString nl  = indent >= 0 ? QString("\n") : QString();
    const QString pad = indent >= 0 ? QString(indent * (depth + 1), ' ') : QString();
    const QString padEnd = indent >= 0 ? QString(indent * depth, ' ') : QString();
    const QString colon = indent >= 0 ? QString(": ") : QString(":");

    switch (v.type()) {
    case QVariant::Map: {
        QVariantMap m = v.toMap();
        if (m.isEmpty()) return "{}";
        QStringList parts;
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it)
            parts << pad + "\"" + escape(it.key()) + "\"" + colon + serialize(it.value(), indent, depth + 1);
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
        return numberToJson(v.toDouble());
    case QVariant::Invalid:
        return "null";
    default:
        return "\"" + escape(v.toString()) + "\"";
    }
}

inline QByteArray mapToJsonNative(const QVariantMap &map)
{
    QVariantMap flat;
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        const QVariant &v = it.value();
        switch (v.type()) {
        case QVariant::List: {
            QVariantList lst = v.toList();
            QVariantList out;
            for (int i = 0; i < lst.size(); ++i) {
                const QVariant &e = lst.at(i);
                switch (e.type()) {
                case QVariant::Int: case QVariant::LongLong:
                case QVariant::UInt: case QVariant::ULongLong:
                case QVariant::Double: case QVariant::Bool:
                    out << e; break;
                default:
                    out << QVariant(e.toString()); break;
                }
            }
            flat[it.key()] = out;
            break;
        }
        case QVariant::Int: case QVariant::LongLong:
        case QVariant::UInt: case QVariant::ULongLong:
        case QVariant::Double: case QVariant::Bool: case QVariant::String:
            flat[it.key()] = v; break;
        default:
            flat[it.key()] = QVariant(v.toString()); break;
        }
    }
    return serialize(flat, -1, 0).toUtf8();
}

}

inline QVariantMap jsonToMap(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantMap();
    QByteArray safe = quoteBigJsonInts(trimmed);
    if (ZJson::nativeMode()) {
        bool ok = false;
        QString txt = QString::fromUtf8(safe);
        QVariant v = ZJson::parse(txt, &ok);
        return (ok && v.type() == QVariant::Map) ? v.toMap() : QVariantMap();
    }
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

inline bool isGroupPollNotice(const QVariantMap &m)
{
    return m.value("msgType").toString() == QLatin1String("group.poll");
}

inline QString extractPollNoticeId(const QVariantMap &m)
{
    QVariantList sources;
    QVariant c = m.value("content");
    QVariantMap cm;
    if (c.type() == QVariant::Map)         cm = c.toMap();
    else if (c.type() == QVariant::String) cm = jsonToMap(c.toString().toUtf8());
    sources.append(cm);
    QVariant pv = cm.value("params");
    if (pv.type() == QVariant::Map)         sources.append(pv.toMap());
    else if (pv.type() == QVariant::String) sources.append(jsonToMap(pv.toString().toUtf8()));

    for (int i = 0; i < sources.size(); ++i) {
        QVariantMap sm = sources[i].toMap();
        const char *keys[] = { "pollId", "poll_id" };
        for (int k = 0; k < 2; ++k) {
            QVariant v = sm.value(keys[k]);
            if (!v.isValid()) continue;
            QString id;
            if (v.type() == QVariant::String) id = v.toString();
            else id = QString::number(v.toLongLong());
            if (!id.isEmpty() && id != QLatin1String("0")) return id;
        }
    }
    return QString();
}

inline QVariantList jsonToList(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantList();
    QByteArray safe = quoteBigJsonInts(trimmed);
    if (ZJson::nativeMode()) {
        bool ok = false;
        QString txt = QString::fromUtf8(safe);
        QVariant v = ZJson::parse(txt, &ok);
        return (ok && v.type() == QVariant::List) ? v.toList() : QVariantList();
    }
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
    if (ZJson::nativeMode()) return ZJson::mapToJsonNative(map);
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

// Generic recursive QVariant -> QScriptValue for exportData()/importData(). mapToJson() is flat
// and used by the wire protocol, so it stays untouched.
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
    if (ZJson::nativeMode()) return ZJson::serialize(root, 2, 0).toUtf8();
    QScriptEngine eng;
    QScriptValue val = variantToScriptValue(eng, root);
    QScriptValue jsonStringify = eng.evaluate("JSON.stringify");
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList()
                                              << val << QScriptValue() << QScriptValue(2));
    return result.toString().toUtf8();
}

// Compact variantToJsonPretty() for wire payloads that get AES-encrypted. mapToJson() can't be
// reused: its List branch flattens elements via toString() and breaks arrays of objects.
inline QByteArray variantToJsonCompact(const QVariant &root)
{
    if (ZJson::nativeMode()) return ZJson::serialize(root, -1, 0).toUtf8();
    QScriptEngine eng;
    QScriptValue val = variantToScriptValue(eng, root);
    QScriptValue jsonStringify = eng.evaluate("JSON.stringify");
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList() << val);
    return result.toString().toUtf8();
}

inline QString normalizePhotoContent(const QVariantMap &m, const QString &rawContent)
{
    QString nUrl, hUrl, tUrl;

    QString caption;
    {
        QVariantMap rawCm = m["content"].toMap();
        if (rawCm.isEmpty() && !rawContent.isEmpty() && rawContent.trimmed().startsWith("{"))
            rawCm = jsonToMap(rawContent.toUtf8());
        caption = rawCm["title"].toString();
        if (caption.isEmpty()) caption = rawCm["description"].toString();
    }

    if (!rawContent.isEmpty() && rawContent.trimmed().startsWith("{")) {
        QVariantMap cm = jsonToMap(rawContent.toUtf8());
        // normalUrl may be a protobuf blob: prefer href/oriUrl, use normalUrl only if it is an http URL.
        QString nu = cm["normalUrl"].toString();
        if (nu.startsWith("http")) {
            nUrl = nu;
        } else {
            nUrl = cm["href"].toString();
            if (nUrl.isEmpty()) nUrl = cm["oriUrl"].toString();
            if (nUrl.isEmpty() && nu.startsWith("http")) nUrl = nu;
        }
        hUrl = cm["hdUrl"].toString();
        if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
        if (hUrl.isEmpty()) hUrl = cm["href"].toString();
        tUrl = cm["thumbUrl"].toString();
        if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
        if (nUrl.isEmpty()) nUrl = nu;
    }

    if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
    if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
    if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumb"].toString();

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

// chat.delete is echoed to BOTH sides for "delete for me", unlike chat.undo (recall). Extract who
// deleted (content[].uidFrom) so the caller hides locally only if it matches our uid.
// Returns true and fills outMsgId/outDeleterUid for chat.delete events.
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

// icon-id <-> wire symbol map for reactions (rIcon like "/-heart", ":>"; not Unicode emoji),
// confirmed on a real device and matching zca-js Reactions. Single source for sending
// (icon -> rIcon) and for cmd=612 (rIcon -> icon).
inline QString reactionIconToEmoji(const QString &icon)
{
    if (icon == "like")  return QString::fromUtf8("/-strong");
    if (icon == "heart") return QString::fromUtf8("/-heart");
    if (icon == "haha")  return QString::fromUtf8(":>");
    if (icon == "wow")   return QString::fromUtf8(":o");
    if (icon == "cry")   return QString::fromUtf8(":-((");
    if (icon == "angry") return QString::fromUtf8(":-h");
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

// Zalo's rType does NOT follow this app's 0..5 slot order (heart = 5, per zca-js
// addReactionFactory()); reactMessage() takes rType from here.
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

// Reaction push event. "chat.reaction" is an unconfirmed name following the chat.<action>
// convention. rType < 0 or empty rIcon means the reaction was removed (outIcon stays empty).
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

// content points at the ORIGINAL message being recalled; returns its msgId, or empty if m is not a recall.
inline QString extractRecalledMsgId(const QVariantMap &m)
{
    QString msgTypeStr = m.value("msgType").toString();
    if (msgTypeStr.compare("chat.undo", Qt::CaseInsensitive) != 0)
        return QString();

    // content is usually a nested QVariantMap but can be a JSON string depending on the call path.
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

inline QByteArray aesGcmDecrypt(const QByteArray &keyRaw, const QByteArray &cipherBytes)
{
    if (cipherBytes.size() < 48) return QByteArray();
    QByteArray key = keyRaw;
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) {
        key = QByteArray::fromBase64(keyRaw);
    }
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return QByteArray();

    const unsigned char *iv  = (const unsigned char*)cipherBytes.constData();
    const unsigned char *aad = (const unsigned char*)cipherBytes.constData() + 16;
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
    int len = 0;
    EVP_DecryptUpdate(ctx, NULL, &len, aad, 16);
    QByteArray out(cipherLen + 16, '\0');
    EVP_DecryptUpdate(ctx, (unsigned char*)out.data(), &len,
        cipher, cipherLen);
    int outLen = len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
    int ret = EVP_DecryptFinal_ex(ctx, (unsigned char*)out.data() + outLen, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) {
        qWarning() << "[Crypto] AES-GCM decrypt failed, ret=" << ret;
        return QByteArray();
    }
    out.resize(outLen + len);
    return out;
}

#endif
