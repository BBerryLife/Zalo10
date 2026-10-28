#ifndef ZALO10_MESSAGE_JSON_HPP
#define ZALO10_MESSAGE_JSON_HPP

#include "utils/json/ZJson.hpp"

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

#endif
