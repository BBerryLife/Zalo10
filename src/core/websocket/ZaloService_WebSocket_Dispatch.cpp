#include "core/websocket/WebSocketCommon.hpp"

void ZaloService::handleWsMessage(int , const QByteArray &payload)
{
    if (payload.size() < 4) return;

    quint8  version = (quint8)payload[0];
    qint32  cmd     = (qint32)((quint8)payload[1] | ((quint8)payload[2] << 8));
    qint32  subCmd  = (qint32)((quint8)payload[3]);
    QByteArray data = payload.mid(4);

    if (cmd == 1 && subCmd == 1) {
        handleWsHandshakeReply(data);
        return;
    }

    if ((cmd == 501 || cmd == 521) && subCmd == 0) {
        handleWsChatPush(cmd, data);
        return;
    }

    if (cmd == 510 && subCmd == 1) {
        handleWsHistoryReply(data);
    }

    if (cmd == 601 && subCmd == 0) {
        handleWsGroupEvent(data);
    }

    if ((cmd == 502 || cmd == 522) && subCmd == 0) {
        handleWsReceipts(cmd, data);
        return;
    }

    if (cmd == 612) {
        handleWsReactions(data);
    }
}
