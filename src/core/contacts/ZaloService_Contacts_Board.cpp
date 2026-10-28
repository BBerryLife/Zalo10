#include "core/contacts/ContactsCommon.hpp"

// Group board = pinned message + note + poll in one list (BoardType: Note=1, PinnedMessage=2,
// Poll=3). board_type=0 fetches everything; the sheet's tabs filter client-side.
// zpw_service_map_v3 has separate "group_poll" and "group_board" hosts: boards use group_board
// (using the poll host returned 404 on every fetch); poll actions use the plain group service.
void ZaloService::fetchGroupBoard(const QString &groupId, int page, int count)
{
    if (!m_loggedIn || groupId.isEmpty()) return;

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap innerParams;
    innerParams["group_id"]   = groupId;
    innerParams["board_type"] = 0;
    innerParams["page"]       = (page > 0) ? page : 1;
    innerParams["count"]      = (count > 0) ? count : 50;
    innerParams["last_id"]    = 0;
    innerParams["last_type"]  = 0;
    innerParams["imei"]       = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = base + "/api/board/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchGroupBoardDone()));
}

void ZaloService::onFetchGroupBoardDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    // Same false-success risk as onPinGroupMessageDone(): check the HTTP status before trusting error_code.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] fetchGroupBoard HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit groupBoardReady(groupId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit groupBoardReady(groupId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] fetchGroupBoard error_code:" << ec << root["error_message"].toString();
        emit groupBoardReady(groupId, QVariantList(), root["error_message"].toString().isEmpty()
                              ? QString("Error %1").arg(ec) : root["error_message"].toString());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap boardData = (outer.contains("data") && outer["data"].type() == QVariant::Map)
                             ? outer["data"].toMap() : outer;
    QVariantList rawItems = boardData["items"].toList();

    QVariantList items;
    for (int i = 0; i < rawItems.size(); ++i) {
        QVariantMap raw_ = rawItems[i].toMap();
        int boardType = raw_["boardType"].toInt();
        QVariantMap data = raw_["data"].toMap();

        QVariantMap item;
        if (boardType == 1) {
            item["boardType"] = "note";
        } else if (boardType == 2) {
            item["boardType"] = "pin";
        } else if (boardType == 3) {
            item["boardType"] = "poll";
        } else {
            continue;
        }

        if (boardType == 1 || boardType == 2) {
            item["id"]         = data["id"].toString();
            item["creatorId"]  = data["creatorId"].toString();
            item["createTime"] = data["createTime"].toLongLong();
            QVariant paramsV = data["params"];
            QVariantMap params_;
            if (paramsV.type() == QVariant::String) {
                params_ = jsonToMap(paramsV.toString().toUtf8());
            } else {
                params_ = paramsV.toMap();
            }
            item["title"] = params_["title"].toString();
            item["extra"] = params_["extra"].toString();
            if (boardType == 2) item["msgId"] = params_["global_msg_id"].toString();
        } else {
            item["id"]              = QString::number(data["poll_id"].toLongLong());
            item["creatorId"]       = data["creator"].toString();
            item["createTime"]      = data["created_time"].toLongLong();
            item["question"]        = data["question"].toString();
            item["title"]           = data["question"].toString();
            item["closed"]          = data["closed"].toBool();
            item["numVote"]         = data["num_vote"].toInt();
            item["allowMultiChoices"] = data["allow_multi_choices"].toBool();
            QVariantList opts = data["options"].toList();
            QVariantList outOpts;
            for (int j = 0; j < opts.size(); ++j) {
                QVariantMap o = opts[j].toMap();
                QVariantMap oo;
                oo["content"]  = o["content"].toString();
                oo["votes"]    = o["votes"].toInt();
                oo["voted"]    = o["voted"].toBool();
                oo["optionId"] = o["option_id"].toLongLong();
                outOpts.append(oo);
            }
            item["options"] = outOpts;
        }
        items.append(item);
    }
    emit groupBoardReady(groupId, items, QString());
}

// Only text ("webchat") and "chat.photo" can be pinned. msgType is Zalo's code (1=webchat,
// 32=chat.photo), converted by QML from the internal 1/2. params.params is a JSON string
// describing the pinned message.
void ZaloService::pinGroupMessage(const QString &groupId, const QString &msgId,
                                   const QString &cliMsgId, const QString &senderId,
                                   const QString &senderName, const QString &content,
                                   int msgType)
{
    if (!m_loggedIn || groupId.isEmpty() || msgId.isEmpty()) {
        emit pinMessageDone(false, "Not ready to pin");
        return;
    }

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap pinParams;
    pinParams["client_msg_id"] = cliMsgId;
    pinParams["global_msg_id"] = msgId;
    pinParams["senderUid"]     = senderId.isEmpty() ? m_uid : senderId;
    pinParams["senderName"]    = senderName;
    pinParams["title"]         = content;
    pinParams["msg_type"]      = (msgType == 32) ? 32 : 1;

    QVariantMap innerParams;
    innerParams["grid"]      = groupId;
    innerParams["type"]      = 2;
    innerParams["color"]     = -14540254;
    innerParams["emoji"]     = QString::fromUtf8("\xF0\x9F\x93\x8C");
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = -1;
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = 1;
    innerParams["params"]    = QString::fromUtf8(mapToJson(pinParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onPinGroupMessageDone()));
}

void ZaloService::onPinGroupMessageDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    // Non-2xx HTTP or network errors never reached the API logic: jsonToMap() on an HTML error page
    // returns an empty map, so error_code silently defaulted to 0 and a 404 was reported as
    // success. Guard explicitly.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] pinGroupMessage HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pinMessageDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit pinMessageDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] pinGroupMessage error_code:" << ec << err;
        emit pinMessageDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit pinMessageDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You pinned a message", groupId, true);
}

void ZaloService::createGroupNote(const QString &groupId, const QString &title, bool pinAct)
{
    if (!m_loggedIn || groupId.isEmpty() || title.trimmed().isEmpty()) {
        emit createNoteDone(false, "Not ready to create note");
        return;
    }

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap noteParams;
    noteParams["title"] = title;

    QVariantMap innerParams;
    innerParams["grid"]      = groupId;
    innerParams["type"]      = 0;
    innerParams["color"]     = -16777216;
    innerParams["emoji"]     = QString();
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = 1;
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = pinAct ? 1 : 0;
    innerParams["params"]    = QString::fromUtf8(mapToJson(noteParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onCreateGroupNoteDone()));
}

void ZaloService::onCreateGroupNoteDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] createGroupNote HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createNoteDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit createNoteDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] createGroupNote error_code:" << ec << err;
        emit createNoteDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createNoteDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a note", groupId, true);
}
