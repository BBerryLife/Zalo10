#include "core/contacts/ContactsCommon.hpp"

// Polls use the plain "group" service (m_groupServiceUrl), not group_board or group_poll;
// board listing/pin/note use group_board.
void ZaloService::createGroupPoll(const QString &groupId, const QString &question,
                                   const QStringList &optionsList, bool allowMultiChoices,
                                   bool allowAddNewOption, bool hideVotePreview,
                                   bool isAnonymous)
{
    if (!m_loggedIn || groupId.isEmpty() || question.trimmed().isEmpty() || optionsList.size() < 2) {
        emit createPollDone(false, "Poll needs a question and at least 2 options");
        return;
    }

    QVariantMap params;
    params["group_id"]             = groupId;
    params["question"]             = question;
    QVariantList optsVariant;
    foreach (const QString &opt, optionsList) optsVariant.append(opt);
    params["options"]               = optsVariant;
    params["expired_time"]          = 0;
    params["pinAct"]                = false;
    params["allow_multi_choices"]   = allowMultiChoices;
    params["allow_add_new_option"]  = allowAddNewOption;
    params["is_hide_vote_preview"]  = hideVotePreview;
    params["is_anonymous"]          = isAnonymous;
    params["poll_type"]             = 0;
    params["src"]                   = 1;
    params["imei"]                  = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/poll/create"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onCreateGroupPollDone()));
}

void ZaloService::onCreateGroupPollDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] createGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createPollDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit createPollDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] createGroupPoll error_code:" << ec << err;
        emit createPollDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createPollDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a poll", groupId, true);
}

// GET with encrypted params in the query string. Empty optionIds clears the vote.
void ZaloService::voteGroupPoll(const QString &groupId, const QString &pollId, const QVariantList &optionIds)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit votePollDone(false, pollId, QVariantList(), "Not ready to vote");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    QVariantList optIdsVariant;
    for (int oi = 0; oi < optionIds.size(); ++oi)
        optIdsVariant.append(optionIds[oi].toLongLong());
    params["option_ids"] = optIdsVariant;
    params["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QString urlStr = m_groupServiceUrl + "/api/poll/vote"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("pollId", pollId);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onVoteGroupPollDone()));
}

void ZaloService::onVoteGroupPollDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString pollId  = reply->property("pollId").toString();
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] voteGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit votePollDone(false, pollId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit votePollDone(false, pollId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] voteGroupPoll error_code:" << ec << err;
        emit votePollDone(false, pollId, QVariantList(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    if (!outer.contains("options") && outer["data"].type() == QVariant::Map)
        outer = outer["data"].toMap();
    QVariantList rawOpts = outer["options"].toList();

    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toLongLong();
        outOpts.append(oo);
    }
    emit votePollDone(true, pollId, outOpts, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You voted in a poll", groupId, true);
}

// POST with encrypted params in the body. Only the per-option "voters" are new, so they stay in
// the map under "options" instead of being merged into the existing model.
void ZaloService::getPollDetail(const QString &pollId)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit pollDetailReady(pollId, QVariantMap(), "Not ready");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    params["imei"]    = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/poll/detail"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("pollId", pollId);
    connect(reply, SIGNAL(finished()), this, SLOT(onGetPollDetailDone()));
}

void ZaloService::onGetPollDetailDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString pollId = reply->property("pollId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] getPollDetail HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pollDetailReady(pollId, QVariantMap(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit pollDetailReady(pollId, QVariantMap(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] getPollDetail error_code:" << ec << err;
        emit pollDetailReady(pollId, QVariantMap(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    if (!outer.contains("options") && outer["data"].type() == QVariant::Map)
        outer = outer["data"].toMap();

    QVariantList rawOpts = outer["options"].toList();
    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toLongLong();
        QVariantList votersList;
        QVariantList rawVoters = o["voters"].toList();
        for (int j = 0; j < rawVoters.size(); ++j) votersList.append(rawVoters[j].toString());
        oo["voters"] = votersList;
        outOpts.append(oo);
    }

    QVariantMap detail;
    detail["question"]           = outer["question"].toString();
    detail["creator"]            = outer["creator"].toString();
    detail["createTime"]         = outer["created_time"].toLongLong();
    detail["closed"]             = outer["closed"].toBool();
    detail["allowMultiChoices"]  = outer["allow_multi_choices"].toBool();
    detail["numVote"]            = outer["num_vote"].toInt();
    detail["options"]            = outOpts;

    emit pollDetailReady(pollId, detail, QString());
}
