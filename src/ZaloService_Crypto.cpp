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

    // QUAN TRỌNG: AES_cbc_encrypt() (OpenSSL) ghi đúng cipher.size() byte vào
    // out — nhưng CBC là block cipher, chỉ đúng/an toàn khi cipher.size() là
    // BỘI SỐ của AES_BLOCK_SIZE (16). Nếu cipherB64 bị lệch block (ví dụ do
    // percent-decode/base64-decode phía trên trả về số byte lẻ vì dữ liệu
    // server không như mong đợi), gọi thẳng AES_cbc_encrypt với size lẻ có
    // thể ghi TRÀN ra ngoài buffer `out` (heap buffer overflow) — bug này
    // KHÔNG crash ngay tại đây, mà thường crash sau đó ở 1 allocation khác
    // hoàn toàn không liên quan, rất khó trace nếu không kiểm tra ở đây.
    if (cipher.size() % AES_BLOCK_SIZE != 0) {
        qDebug() << "[Zalo Error] aesDecryptBase64_256: cipher size" << cipher.size()
                  << "khong chia het cho" << AES_BLOCK_SIZE << "(AES block) - du lieu loi/lech, bo qua de tranh buffer overflow";
        return QString();
    }

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
    qDebug() << "[Zalo] aesDecryptBase64 input length:" << cipherB64.length();
    // Log rải ra từng bước nhỏ — lần crash gần nhất, log dừng đột ngột ngay
    // sau dòng "input length" phía trên, TRƯỚC cả dòng "cipher size after
    // base64 decode" ở dưới — nghĩa là crash nằm đâu đó trong resolveKey()/
    // fromPercentEncoding()/fromBase64() bên dưới, không phải ở
    // AES_cbc_encrypt như nghi ngờ ban đầu. Thêm log xen kẽ để lần sau biết
    // chính xác dòng nào chết, thay vì đoán qua khoảng trống trong log.
    QByteArray key    = resolveKey(keyStr);
    qDebug() << "[Zalo] aesDecryptBase64 resolveKey done, keyBytes:" << key.size();
    QString decoded   = QUrl::fromPercentEncoding(cipherB64.toUtf8());
    qDebug() << "[Zalo] aesDecryptBase64 percent-decode done, length:" << decoded.length();
    int keyBits = key.size() * 8;
    QByteArray cipher = QByteArray::fromBase64(decoded.toUtf8());
    if (cipher.isEmpty()) return QString();
    qDebug() << "[Zalo] aesDecryptBase64 cipher size after base64 decode:" << cipher.size() << "bytes";

    // Sanity cap: 1 lần fetch bình thường (kể cả friend list/group list lớn)
    // không thể vượt vài MB sau khi mã hoá. Nếu vượt ngưỡng này, gần như chắc
    // chắn cipherB64 bị parse sai (garbage) chứ không phải payload thật —
    // chặn ở đây để tránh AES_cbc_encrypt cấp phát/ghi vào buffer khổng lồ
    // (nguồn gốc "bad allocation" quan sát được trong log) thay vì để crash.
    const int kMaxCipherBytes = 20 * 1024 * 1024; // 20MB
    if (cipher.size() > kMaxCipherBytes) {
        qDebug() << "[Zalo Error] aesDecryptBase64: cipher size" << cipher.size()
                  << "vuot nguong an toan (" << kMaxCipherBytes << ") - cipherB64.length()="
                  << cipherB64.length() << "- bo qua de tranh bad_alloc";
        return QString();
    }

    // QUAN TRỌNG (xem giải thích chi tiết ở aesDecryptBase64_256 phía trên):
    // AES_cbc_encrypt() chỉ an toàn khi cipher.size() là bội số của
    // AES_BLOCK_SIZE (16) — không kiểm tra thì có thể ghi tràn ra ngoài
    // buffer `out`, gây heap corruption âm thầm, crash trễ ở 1 chỗ khác
    // không liên quan (rất khó trace).
    if (cipher.size() % AES_BLOCK_SIZE != 0) {
        qDebug() << "[Zalo Error] aesDecryptBase64: cipher size" << cipher.size()
                  << "khong chia het cho" << AES_BLOCK_SIZE << "(AES block) - du lieu loi/lech, bo qua de tranh buffer overflow";
        return QString();
    }

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
    qDebug() << "[Zalo] aesDecryptBase64 decrypted output size:" << out.size() << "bytes";
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

