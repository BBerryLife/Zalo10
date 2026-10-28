#include "core/messages/MessagesCommon.hpp"

static QByteArray makeJpegMarker(quint8 marker, const QByteArray &data)
{
    QByteArray r;
    r += (char)0xFF;
    r += (char)marker;
    quint16 len = (quint16)(data.size() + 2);
    r += (char)((len >> 8) & 0xFF);
    r += (char)(len & 0xFF);
    r += data;
    return r;
}

void ZaloService::downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId)
{
    if (url.isEmpty() || msgId.isEmpty()) return;

    // Inline base64 images (previewThumb from realtime photo messages) start with base64 data,
    // not "http".
    if (!url.startsWith("http")) {
        if (m_avatarCache.contains(url)) {
            QSize sz = imageDimensions(m_avatarCache[url]);
            emit imageMsgReady(msgId, m_avatarCache[url], sz.width(), sz.height());
            return;
        }
        QByteArray imgData = QByteArray::fromBase64(url.toUtf8());
        if (!imgData.isEmpty()) {
            QString ext;
            if (imgData.size() >= 2 &&
                (unsigned char)imgData[0] == 0xFF && (unsigned char)imgData[1] == 0xD8)
                ext = "jpg";
            else if (imgData.startsWith("\x89PNG"))
                ext = "png";
            else if (imgData.startsWith("GIF"))
                ext = "gif";
            else if (imgData.startsWith("RIFF") && imgData.size() > 12 &&
                     imgData.mid(8, 4) == "WEBP")
                ext = "webp";

            if (ext.isEmpty()) {
                if (imgData.size() > 6 &&
                    (unsigned char)imgData[0] == 0x03 &&
                    (unsigned char)imgData[1] == 0x00 &&
                    (unsigned char)imgData[5] == 0xFF &&
                    (unsigned char)imgData[6] == 0xDA)
                {
                    int W = (unsigned char)imgData[2];
                    int H = (unsigned char)imgData[4];
                    QByteArray sosData = imgData.mid(5);

                    static const quint8 lumaQ[64] = {
                        16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
                        14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
                        18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
                        49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
                    };
                    static const quint8 chromaQ[64] = {
                        17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
                        24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
                        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
                        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99
                    };
                    static const quint8 dcLumLen[16]  = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
                    static const quint8 dcLumVal[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
                    static const quint8 dcChrLen[16]  = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
                    static const quint8 dcChrVal[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
                    static const quint8 acLumLen[16]  = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
                    static const quint8 acLumVal[162] = {
                        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
                        0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,
                        0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,
                        0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
                        0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,
                        0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,
                        0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
                        0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
                        0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,
                        0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,
                        0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
                    };
                    static const quint8 acChrLen[16]  = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
                    static const quint8 acChrVal[162] = {
                        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
                        0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
                        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
                        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
                        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
                        0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
                        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
                        0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
                        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
                        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
                        0xf9,0xfa
                    };

                    QByteArray dqt0; dqt0 += (char)0x00;
                    dqt0 += QByteArray((const char*)lumaQ, 64);
                    QByteArray dqt1; dqt1 += (char)0x01;
                    dqt1 += QByteArray((const char*)chromaQ, 64);

                    // Build bytes with explicit (char) casts, NOT string literals like "\x01\x22\x00":
                    // QByteArray::operator+=(const char*) stops at the first \x00 and silently drops the Y
                    // component's qtable byte, corrupting every following marker.
                    QByteArray sof0Data;
                    sof0Data += (char)8;
                    sof0Data += (char)((H >> 8) & 0xFF);
                    sof0Data += (char)(H & 0xFF);
                    sof0Data += (char)((W >> 8) & 0xFF);
                    sof0Data += (char)(W & 0xFF);
                    sof0Data += (char)3;
                    sof0Data += (char)0x01;
                    sof0Data += (char)0x22;
                    sof0Data += (char)0x00;
                    sof0Data += (char)0x02;
                    sof0Data += (char)0x11;
                    sof0Data += (char)0x01;
                    sof0Data += (char)0x03;
                    sof0Data += (char)0x11;
                    sof0Data += (char)0x01;

                    QByteArray dhtDcLum; dhtDcLum += (char)0x00;
                    dhtDcLum += QByteArray((const char*)dcLumLen, 16);
                    dhtDcLum += QByteArray((const char*)dcLumVal, 12);
                    QByteArray dhtDcChr; dhtDcChr += (char)0x01;
                    dhtDcChr += QByteArray((const char*)dcChrLen, 16);
                    dhtDcChr += QByteArray((const char*)dcChrVal, 12);
                    QByteArray dhtAcLum; dhtAcLum += (char)0x10;
                    dhtAcLum += QByteArray((const char*)acLumLen, 16);
                    dhtAcLum += QByteArray((const char*)acLumVal, 162);
                    QByteArray dhtAcChr; dhtAcChr += (char)0x11;
                    dhtAcChr += QByteArray((const char*)acChrLen, 16);
                    dhtAcChr += QByteArray((const char*)acChrVal, 162);

                    QByteArray jpeg;
                    jpeg += "\xFF\xD8";

                    static const char app0Data[] = {
                        'J','F','I','F','\x00',
                        '\x01','\x01',
                        '\x00',
                        '\x00','\x01','\x00','\x01',
                        '\x00','\x00'
                    };
                    jpeg += makeJpegMarker(0xE0, QByteArray(app0Data, sizeof(app0Data)));

                    jpeg += makeJpegMarker(0xDB, dqt0);
                    jpeg += makeJpegMarker(0xDB, dqt1);
                    jpeg += makeJpegMarker(0xC0, sof0Data);
                    jpeg += makeJpegMarker(0xC4, dhtDcLum);
                    jpeg += makeJpegMarker(0xC4, dhtDcChr);
                    jpeg += makeJpegMarker(0xC4, dhtAcLum);
                    jpeg += makeJpegMarker(0xC4, dhtAcChr);
                    jpeg += sosData;
                    jpeg += "\xFF\xD9";

                    // msgId as file name: a unique path per message avoids BB10's stale image cache. Always PNG
                    // (BB10's ImageView is more reliable with it). Literal "/tmp/", not QDir::tempPath() (wiped
                    // on restart).
                    QString tmpPath = "/tmp/msgthumb_" +
                                      msgId + ".png";
                    QFile::remove(tmpPath);

                    // Byte-stuff scan data: every 0xFF in entropy-coded data must be followed by 0x00. Zalo's raw
                    // SOS data has bare 0xFF bytes that caused "Bogus marker length" in BB10's libjpeg.
                    QByteArray stuffedSos;
                    int sosHeaderLen = 0;
                    if (sosData.size() >= 4) {
                        int segLen = ((unsigned char)sosData[2] << 8) | (unsigned char)sosData[3];
                        sosHeaderLen = 2 + segLen;
                    }
                    stuffedSos += sosData.left(sosHeaderLen);
                    for (int si = sosHeaderLen; si < sosData.size(); ++si) {
                        unsigned char b = (unsigned char)sosData[si];
                        stuffedSos += (char)b;
                        if (b == 0xFF) {
                            // Skip stuffing if the next byte is already 0x00, a restart marker (D0-D7) or EOI (D9).
                            unsigned char next = (si + 1 < sosData.size())
                                                 ? (unsigned char)sosData[si + 1] : 0x00;
                            if (next != 0x00 &&
                                !(next >= 0xD0 && next <= 0xD7) &&
                                next != 0xD9) {
                                stuffedSos += (char)0x00;
                            }
                        }
                    }

                    QByteArray jpeg2;
                    jpeg2 += "\xFF\xD8";
                    jpeg2 += makeJpegMarker(0xE0, QByteArray(app0Data, sizeof(app0Data)));
                    jpeg2 += makeJpegMarker(0xDB, dqt0);
                    jpeg2 += makeJpegMarker(0xDB, dqt1);
                    jpeg2 += makeJpegMarker(0xC0, sof0Data);
                    jpeg2 += makeJpegMarker(0xC4, dhtDcLum);
                    jpeg2 += makeJpegMarker(0xC4, dhtDcChr);
                    jpeg2 += makeJpegMarker(0xC4, dhtAcLum);
                    jpeg2 += makeJpegMarker(0xC4, dhtAcChr);
                    jpeg2 += stuffedSos;
                    jpeg2 += "\xFF\xD9";

                    QImage qimg;
                    bool decoded = qimg.loadFromData(jpeg2, "JPEG");
                    if (!decoded) decoded = qimg.loadFromData(jpeg, "JPEG");

                    if (decoded) {
                        if (qimg.width() < 120 || qimg.height() < 120) {
                            qimg = qimg.scaled(240, 240,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
                        }
                        qimg.save(tmpPath, "PNG");
                    } else {
                        QImage placeholder(240, 240, QImage::Format_RGB32);
                        placeholder.fill(QColor(37, 117, 252));
                        placeholder.save(tmpPath, "PNG");
                        qWarning() << "[Msg] thumb decode failed, saved placeholder msgId=" << msgId;
                    }
                    QString filePath = "file://" + tmpPath;
                    m_avatarCache[url] = filePath;
                    int imgW = qimg.width();
                    int imgH = qimg.height();
                    if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
                        const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
                        sqlite3_stmt *stmt2 = 0;
                        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt2, 0) == SQLITE_OK) {
                            sqlite3_bind_text(stmt2, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt2,  2, imgW);
                            sqlite3_bind_int(stmt2,  3, imgH);
                            sqlite3_bind_text(stmt2, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt2);
                            sqlite3_finalize(stmt2);
                        }
                    }
                    emit imageMsgReady(msgId, filePath, imgW, imgH);
                    return;
                }
                qWarning() << "[Msg] image: unrecognised format, msgId=" << msgId;
                return;
            }

            QString tmpPath = "/tmp/msgimg_" +
                              QString::number(qHash(url)) + "." + ext;
            QFile f(tmpPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(imgData);
                f.close();
                QString filePath = "file://" + tmpPath;
                m_avatarCache[url] = filePath;
                QSize dim = imageDimensions(filePath);
                if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
                    const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
                    sqlite3_stmt *stmt = 0;
                    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                        sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt,  2, dim.width());
                        sqlite3_bind_int(stmt,  3, dim.height());
                        sqlite3_bind_text(stmt, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
                emit imageMsgReady(msgId, filePath, dim.width(), dim.height());
                return;
            }
        }
        qWarning() << "[Msg] downloadImageMessage: non-http url and base64 decode failed, msgId=" << msgId;
        return;
    }

    if (m_avatarCache.contains(url)) {
        QSize sz = imageDimensions(m_avatarCache[url]);
        emit imageMsgReady(msgId, m_avatarCache[url], sz.width(), sz.height());
        return;
    }

    // Reuse an image already on disk (from a previous session): downloadImageMessage() can be called
    // directly (resync, jump-to-message) without going through dbLoadMessages().
    if (!msgId.isEmpty() && m_db) {
        const char *sqlChk = "SELECT localImage, imgWidth, imgHeight FROM messages WHERE msgId=?";
        sqlite3_stmt *chk = 0;
        if (sqlite3_prepare_v2(m_db, sqlChk, -1, &chk, 0) == SQLITE_OK) {
            sqlite3_bind_text(chk, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(chk) == SQLITE_ROW) {
                QString existingPath = QString::fromUtf8((const char*)sqlite3_column_text(chk, 0));
                int existingW = sqlite3_column_int(chk, 1);
                int existingH = sqlite3_column_int(chk, 2);
                QString fsPath = existingPath;
                if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
                if (!fsPath.isEmpty() && QFile::exists(fsPath)) {
                    sqlite3_finalize(chk);
                    m_avatarCache[url] = existingPath;
                    emit imageMsgReady(msgId, existingPath, existingW, existingH);
                    return;
                }
            }
            sqlite3_finalize(chk);
        }
    }

    if (m_pendingAvatars.contains(url)) return;
    m_pendingAvatars.insert(url);

    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    req.setRawHeader("Accept", "image/webp,image/apng,image/*,*/*;q=0.8");
    QNetworkReply *reply = m_manager->get(req);
    reply->setProperty("msgId",    msgId);
    reply->setProperty("imgUrl",   url);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onImageMsgDownloaded()));
}

void ZaloService::onImageMsgDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString msgId   = reply->property("msgId").toString();
    QString url     = reply->property("imgUrl").toString();
    QString threadId = reply->property("threadId").toString();
    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_pendingAvatars.remove(url);

    if (data.isEmpty()) {
        return;
    }

    QString ext = "jpg";
    QByteArray ct = reply->rawHeader("Content-Type");
    if (ct.contains("png"))  ext = "png";
    if (ct.contains("gif"))  ext = "gif";
    if (ct.contains("webp")) ext = "webp";

    QByteArray finalData = data;
    if (ext == "webp" || ext == "jpg") {
        QImage img;
        if (img.loadFromData(data)) {
            QByteArray pngBuf;
            QBuffer buf(&pngBuf);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "PNG")) {
                finalData = pngBuf;
                ext = "png";
            }
        }
    }

    // File name per message (msgId, not md5(url)): Zalo's CDN URLs for the same image change between
    // fetches, and msgId makes refetches overwrite the same file and keeps the QFile::exists()
    // check working. Literal "/tmp/" (not QDir::tempPath()): the persistent root that survives
    // restart and relogin.
    QString stableKey = msgId.isEmpty() ? md5Hex(url) : msgId;
    QString tmpPath = "/tmp/zalo_img_" + stableKey + "." + ext;
    QFile f(tmpPath);
    if (f.open(QIODevice::WriteOnly)) { f.write(finalData); f.close(); }
    QString filePath = "file://" + tmpPath;
    m_avatarCache[url] = filePath;
    QSize dim = imageDimensions(filePath);

    if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
        const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,  2, dim.width());
            sqlite3_bind_int(stmt,  3, dim.height());
            sqlite3_bind_text(stmt, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    emit imageMsgReady(msgId, filePath, dim.width(), dim.height());
}
