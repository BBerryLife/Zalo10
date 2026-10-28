#ifndef ZALOSERVICEUTILS_HPP
#define ZALOSERVICEUTILS_HPP

// Small JSON/crypto helpers shared by ZaloService_*.cpp. Qt4 has no QJson, so JSON goes through
// QScriptEngine. Everything is inline (header included from many .cpp files).

#include "utils/json/ZJson.hpp"
#include "utils/json/ZaloMessageJson.hpp"

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