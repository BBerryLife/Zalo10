#ifndef ZALO10_ZJSON_HPP
#define ZALO10_ZJSON_HPP

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

#endif
