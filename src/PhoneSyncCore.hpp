#ifndef PHONESYNCCORE_HPP
#define PHONESYNCCORE_HPP
#include <string>
#include <vector>
#include <stddef.h>

namespace PhoneSyncCore {

struct Span { size_t offset; size_t size; };

bool deriveAesKey(const std::string &keyHex, unsigned char key[32]);

bool decryptDbCrypt(const std::string &data, const std::string &aesKeyHex,
                    std::string &plain, bool &footerFound, std::string &err);

bool xzDecodeAll(const std::string &plain, std::string &payload, size_t &xzOffset, std::string &err);

std::vector<Span> splitSqlite(const std::string &payload);

std::vector<std::string> parseTocNames(const std::string &plain, size_t xzStart);

// rsa is an opaque RSA*; free it with rsaFree().
void *rsaGenerate(int bits);
bool  rsaPublicKeyDer(void *rsa, std::string &der);
bool  rsaPrivateDecrypt(void *rsa, const std::string &cipher, std::string &plain);
void  rsaFree(void *rsa);

std::string toHex(const std::string &bytes);

// Plain-OpenSSL HTTPS GET forced to TLS 1.2 (QSslSocket is too limited on BB10).
// extraHeaders lines must each end with \r\n.
bool httpsGet(const std::string &host, int port, const std::string &path, const std::string &extraHeaders,
              int timeoutSec, std::string &body, int &status, std::string &info);

// Same, for any method. respHead is the raw status line + headers; reqBody is sent for non-GET.
bool httpsRequest(const std::string &method, const std::string &host, int port, const std::string &path,
                  const std::string &extraHeaders, const std::string &reqBody, int timeoutSec,
                  std::string &body, int &status, std::string &respHead, std::string &info);

}

#endif
