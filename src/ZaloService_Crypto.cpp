#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeManager>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QByteArray>
#include <QScriptEngine>
#include <QScriptValue>
#include <QUuid>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegExp>
#include <QStringList>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSslError>
#include <sqlite3.h>

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <zlib.h>
#include <string.h>

// AES/MD5 helpers and the request-signing/identity generation used by the
// login and API-request pipeline (IMEI, UUID, user-agent strings, etc).

static QByteArray resolveKeyUtf8(const QString &keyStr)
{
    QByteArray k = keyStr.toUtf8();
    while (k.size() < 32) k.append('\0');
    return k.left(32);
}

static QByteArray resolveKeyBase64(const QString &keyStr)
{
    QByteArray decoded = QByteArray::fromBase64(keyStr.toUtf8());
    int sz = decoded.size();
    if (sz <= 16) { while (decoded.size() < 16) decoded.append('\0'); return decoded.left(16); }
    if (sz <= 24) { while (decoded.size() < 24) decoded.append('\0'); return decoded.left(24); }
    while (decoded.size() < 32) decoded.append('\0');
    return decoded.left(32);
}

static QByteArray resolveKey(const QString &keyStr)
{
    return resolveKeyBase64(keyStr);
}

QString ZaloService::aesEncryptHex(const QString &keyHex32, const QString &plainText)
{
    QByteArray key  = resolveKeyUtf8(keyHex32);
    QByteArray data = plainText.toUtf8();
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    int pad = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
    data.append(QByteArray(pad, (char)pad));

    QByteArray out(data.size(), '\0');
    AES_KEY k;
    AES_set_encrypt_key((const unsigned char*)key.constData(), 256, &k);
    AES_cbc_encrypt((const unsigned char*)data.constData(), (unsigned char*)out.data(), data.size(), &k, iv, AES_ENCRYPT);
    return out.toHex().toUpper();
}

QString ZaloService::aesDecryptBase64_256(const QString &keyStr, const QString &cipherB64)
{
    QByteArray key    = resolveKeyUtf8(keyStr);
    QByteArray cipher = QByteArray::fromBase64(
        QUrl::fromPercentEncoding(cipherB64.toUtf8()).toUtf8());
    if (cipher.isEmpty()) return QString();

    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    QByteArray out(cipher.size(), '\0');
    AES_KEY k;
    AES_set_decrypt_key((const unsigned char*)key.constData(), 256, &k);
    AES_cbc_encrypt((const unsigned char*)cipher.constData(),
                    (unsigned char*)out.data(), cipher.size(), &k, iv, AES_DECRYPT);

    if (!out.isEmpty()) {
        int pad = (unsigned char)out[out.size()-1];
        if (pad > 0 && pad <= AES_BLOCK_SIZE) out.chop(pad);
    }
    return QString::fromUtf8(out);
}

QString ZaloService::aesEncryptBase64(const QString &keyStr, const QString &plainText)
{
    QByteArray key  = resolveKeyBase64(keyStr);
    int keyBits = key.size() * 8;
    QByteArray data = plainText.toUtf8();
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    int pad = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
    data.append(QByteArray(pad, (char)pad));

    QByteArray out(data.size(), '\0');
    AES_KEY k;
    AES_set_encrypt_key((const unsigned char*)key.constData(), keyBits, &k);
    AES_cbc_encrypt((const unsigned char*)data.constData(), (unsigned char*)out.data(), data.size(), &k, iv, AES_ENCRYPT);
    return out.toBase64();
}

QString ZaloService::aesEncryptBase64_256(const QString &keyStr, const QString &plainText)
{
    QByteArray key  = resolveKeyUtf8(keyStr);
    qDebug() << "[Zalo] aesEncryptBase64_256(params) keyBytes=" << key.toHex() << "bits=" << (key.size()*8);
    QByteArray data = plainText.toUtf8();
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    int pad = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
    data.append(QByteArray(pad, (char)pad));

    QByteArray out(data.size(), '\0');
    AES_KEY k;
    AES_set_encrypt_key((const unsigned char*)key.constData(), 256, &k);
    AES_cbc_encrypt((const unsigned char*)data.constData(), (unsigned char*)out.data(), data.size(), &k, iv, AES_ENCRYPT);
    return out.toBase64();
}


QString ZaloService::aesDecryptBase64(const QString &keyStr, const QString &cipherB64)
{
    if (cipherB64.isEmpty()) return QString();
    QByteArray key    = resolveKey(keyStr);
    int keyBits = key.size() * 8;
    QString decoded   = QUrl::fromPercentEncoding(cipherB64.toUtf8());
    QByteArray cipher = QByteArray::fromBase64(decoded.toUtf8());
    if (cipher.isEmpty()) return QString();

    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    QByteArray out(cipher.size(), '\0');
    AES_KEY k;
    AES_set_decrypt_key((const unsigned char*)key.constData(), keyBits, &k);
    AES_cbc_encrypt((const unsigned char*)cipher.constData(), (unsigned char*)out.data(), cipher.size(), &k, iv, AES_DECRYPT);

    if (!out.isEmpty()) {
        unsigned char pad = (unsigned char)out.at(out.size() - 1);
        if (pad > 0 && pad <= AES_BLOCK_SIZE) out.chop(pad);
    }
    return QString::fromUtf8(out);
}

QString ZaloService::md5Hex(const QString &input)
{
    return QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Md5).toHex().toLower();
}

QString ZaloService::md5Hex(const QByteArray &input)
{
    return QCryptographicHash::hash(input, QCryptographicHash::Md5).toHex().toLower();
}

QString ZaloService::randomHexString(int len)
{
    QString r;
    while (r.length() < len) r += QString::number((quint32)qrand(), 16);
    return r.left(len);
}

ZaloService::EncryptedParams ZaloService::buildEncryptedParams(const QVariantMap &data)
{
    EncryptedParams ep;
    ep.enc_ver = "v2";
    qDebug() << "[Zalo] buildEncryptedParams enc_ver=v2 API=" << API_VERSION;

    QString ts = data.contains("ts") ? data["ts"].toString()
                                     : QString::number(QDateTime::currentMSecsSinceEpoch());
    QString firstLaunchTime = QString::number(QDateTime::currentMSecsSinceEpoch());

    QString zcidMsg = QString("%1,%2,%3").arg(API_TYPE).arg(m_imei).arg(firstLaunchTime);
    ep.zcid     = aesEncryptHex(AES_FIXED_KEY, zcidMsg);
    ep.zcid_ext = randomHexString(8).toLower();

    QString n = md5Hex(ep.zcid_ext).toUpper();

    QStringList nEven;
    for (int i = 0; i < n.length(); i += 2) nEven << QString(n[i]);

    QStringList zEven, zOdd;
    for (int i = 0; i < ep.zcid.length(); ++i) {
        if (i % 2 == 0) zEven << QString(ep.zcid[i]);
        else             zOdd  << QString(ep.zcid[i]);
    }
    for (int i = 0, j = zOdd.size() - 1; i < j; ++i, --j) zOdd.swap(i, j);

    QStringList nEven8(nEven.mid(0, 8));
    QStringList zEven12(zEven.mid(0, 12));
    QStringList zOdd12(zOdd.mid(0, 12));
    ep.encryptKey = nEven8.join("") + zEven12.join("") + zOdd12.join("");

    ep.encryptedData = aesEncryptBase64_256(ep.encryptKey, QString::fromUtf8(mapToJson(data)));
    qDebug() << "[Zalo] buildEncryptedParams encryptKey=" << ep.encryptKey << "zcid_ext=" << ep.zcid_ext;
    return ep;
}

QString ZaloService::buildSignKey(const QString &type, const QVariantMap &params)
{
    QStringList keys = params.keys();
    keys.sort();
    QString a = "zsecure" + type;
    for (int i = 0; i < keys.size(); ++i) a += params[keys[i]].toString();
    return md5Hex(a);
}

QString ZaloService::generateIMEI()
{
    return generateUUIDv4() + "-" + md5Hex(m_userAgent);
}

QString ZaloService::generateUUIDv4()
{
    return QUuid::createUuid().toString().remove('{').remove('}').toLower();
}

QString ZaloService::generateRandomUserAgent()
{
    static const int chromeMajors[] = { 118, 119, 120, 121, 122, 123, 124, 125 };
    static const int majorCount = 8;

    // Windows versions
    static const char* winVersions[] = {
        "Windows NT 10.0; Win64; x64",
        "Windows NT 10.0; WOW64",
        "Windows NT 6.1; Win64; x64",
        "Windows NT 6.3; Win64; x64"
    };
    static const int winCount = 4;

    int major    = chromeMajors[qrand() % majorCount];
    int minor    = qrand() % 10;
    int build    = 4000 + qrand() % 2000;
    int patch    = qrand() % 150;
    QString win  = QString::fromLatin1(winVersions[qrand() % winCount]);

    return QString("Mozilla/5.0 (%1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/%2.%3.%4.%5 Safari/537.36")
           .arg(win).arg(major).arg(minor).arg(build).arg(patch);
}

