#include "core/PhoneSyncCore.hpp"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XZ_USE_CRC64
extern "C" {
#include "third_party/xz/xz.h"
}

namespace PhoneSyncCore {

static const size_t CHUNK_SIZE = 64 * 1024;
static const unsigned char TLV_MARKER[8] = { 0, 0, 0, 2, 0, 0, 0, 0x20 };
static const unsigned char XZ_MAGIC[6]   = { 0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00 };
static const char SQLITE_MAGIC[16]       = { 'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0' };

static long findBytes(const std::string &hay, const unsigned char *needle, size_t n, size_t from)
{
    if (hay.size() < n || from > hay.size() - n) return -1;
    for (size_t i = from; i + n <= hay.size(); ++i) {
        if (memcmp(hay.data() + i, needle, n) == 0) return (long)i;
    }
    return -1;
}

std::string toHex(const std::string &bytes)
{
    static const char *H = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned char c = (unsigned char)bytes[i];
        out += H[c >> 4];
        out += H[c & 15];
    }
    return out;
}

bool deriveAesKey(const std::string &keyHex, unsigned char key[32])
{
    if (keyHex.size() < 32) return false;
    for (int i = 0; i < 32; ++i) {
        char c = keyHex[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        key[i] = (unsigned char)c;
    }
    return true;
}

bool decryptDbCrypt(const std::string &data, const std::string &aesKeyHex,
                    std::string &plain, bool &footerFound, std::string &err)
{
    unsigned char key[32];
    if (!deriveAesKey(aesKeyHex, key)) { err = "aes_key_hex quá ngắn"; return false; }

    size_t tailStart = data.size() > 4096 ? data.size() - 4096 : 0;
    long tlv = -1;
    {
        std::string tail = data.substr(tailStart);
        tlv = findBytes(tail, TLV_MARKER, sizeof(TLV_MARKER), 0);
    }
    footerFound = (tlv >= 0);
    size_t encLen = footerFound ? ((tailStart + (size_t)tlv) / 16) * 16 : (data.size() / 16) * 16;

    plain.assign(encLen, '\0');
    unsigned char iv[16];
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { err = "EVP_CIPHER_CTX_new thất bại"; return false; }

    size_t off = 0;
    while (off < encLen) {
        size_t end = off + CHUNK_SIZE;
        if (end > encLen) end = encLen;
        size_t realEnd = off + ((end - off) / 16) * 16;
        if (realEnd == off) break;

        memset(iv, 0, sizeof(iv));
        int outl = 0, finl = 0;
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1) { err = "DecryptInit thất bại"; EVP_CIPHER_CTX_free(ctx); return false; }
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        if (EVP_DecryptUpdate(ctx, (unsigned char *)&plain[off], &outl,
                              (const unsigned char *)data.data() + off, (int)(realEnd - off)) != 1) {
            err = "DecryptUpdate thất bại"; EVP_CIPHER_CTX_free(ctx); return false;
        }
        EVP_DecryptFinal_ex(ctx, (unsigned char *)&plain[off] + outl, &finl);
        off = realEnd;
    }
    EVP_CIPHER_CTX_free(ctx);
    return true;
}

static bool xzDecodeOne(const unsigned char *in, size_t inSize, std::string &out, std::string &err)
{
    struct xz_dec *s = xz_dec_init(XZ_DYNALLOC, 64u << 20);
    if (!s) { err = "xz_dec_init thất bại (hết bộ nhớ)"; return false; }

    struct xz_buf b;
    b.in = in; b.in_pos = 0; b.in_size = inSize;
    std::vector<unsigned char> buf(1u << 20);
    b.out = &buf[0]; b.out_pos = 0; b.out_size = buf.size();

    bool ok = false;
    for (;;) {
        size_t inBefore = b.in_pos;
        enum xz_ret r = xz_dec_run(s, &b);
        size_t produced = b.out_pos;
        if (produced > 0) { out.append((const char *)&buf[0], produced); b.out_pos = 0; }
        if (r == XZ_STREAM_END) { ok = true; break; }
        if (r == XZ_OK || r == XZ_UNSUPPORTED_CHECK) {
            if (b.in_pos == inBefore && produced == 0) { err = "stream XZ bị cắt cụt hoặc không tiến triển"; break; }
            continue;
        }
        char tmp[48];
        sprintf(tmp, "xz lỗi mã %d", (int)r);
        err = tmp;
        break;
    }
    xz_dec_end(s);
    return ok;
}

bool xzDecodeAll(const std::string &plain, std::string &payload, size_t &xzOffset, std::string &err)
{
    static bool inited = false;
    if (!inited) { xz_crc32_init(); xz_crc64_init(); inited = true; }

    std::vector<size_t> starts;
    long p = findBytes(plain, XZ_MAGIC, sizeof(XZ_MAGIC), 0);
    while (p >= 0) { starts.push_back((size_t)p); p = findBytes(plain, XZ_MAGIC, sizeof(XZ_MAGIC), (size_t)p + 1); }
    if (starts.empty()) { err = "không thấy magic XZ trong ZDB4.0"; return false; }

    payload.clear();
    xzOffset = starts[0];
    bool any = false;
    std::string lastErr;
    for (size_t i = 0; i < starts.size(); ++i) {
        size_t s = starts[i];
        size_t e = (i + 1 < starts.size()) ? starts[i + 1] : plain.size();
        std::string one, e1;
        if (xzDecodeOne((const unsigned char *)plain.data() + s, e - s, one, e1)) {
            payload += one;
            any = true;
        } else {
            lastErr = e1;
        }
    }
    if (!any) { err = "không giải nén được stream XZ nào: " + lastErr; return false; }
    return true;
}

static unsigned be16(const std::string &s, size_t o) { return ((unsigned char)s[o] << 8) | (unsigned char)s[o + 1]; }
static unsigned be32(const std::string &s, size_t o)
{
    return ((unsigned)(unsigned char)s[o] << 24) | ((unsigned)(unsigned char)s[o + 1] << 16)
         | ((unsigned)(unsigned char)s[o + 2] << 8) | (unsigned)(unsigned char)s[o + 3];
}

std::vector<Span> splitSqlite(const std::string &buf)
{
    std::vector<Span> parts;
    size_t pos = 0;
    bool have = false;
    {
        for (size_t i = 0; i + sizeof(SQLITE_MAGIC) <= buf.size(); ++i) {
            if (memcmp(buf.data() + i, SQLITE_MAGIC, sizeof(SQLITE_MAGIC)) == 0) { pos = i; have = true; break; }
        }
    }
    while (have && pos < buf.size()) {
        size_t size = 0;
        if (pos + 100 <= buf.size()) {
            unsigned ps = be16(buf, pos + 16);
            if (ps == 1) ps = 65536;
            unsigned pc = be32(buf, pos + 28);
            bool okPs = (ps >= 512 && ps <= 65536 && (ps & (ps - 1)) == 0);
            if (okPs && pc > 0 && pos + (size_t)ps * pc <= buf.size()) size = (size_t)ps * pc;
        }
        size_t next = size ? pos + size : 0;
        bool magicAtNext = size && next + sizeof(SQLITE_MAGIC) <= buf.size()
                        && memcmp(buf.data() + next, SQLITE_MAGIC, sizeof(SQLITE_MAGIC)) == 0;
        if (!size || (!magicAtNext && next != buf.size())) {
            // Header is untrusted: cut at the next SQLite magic or end of buffer.
            size_t n = buf.size();
            for (size_t i = pos + 1; i + sizeof(SQLITE_MAGIC) <= buf.size(); ++i) {
                if (memcmp(buf.data() + i, SQLITE_MAGIC, sizeof(SQLITE_MAGIC)) == 0) { n = i; break; }
            }
            next = n;
            size = next - pos;
        }
        Span sp; sp.offset = pos; sp.size = size;
        parts.push_back(sp);
        have = false;
        if (next < buf.size()) {
            for (size_t i = next; i + sizeof(SQLITE_MAGIC) <= buf.size(); ++i) {
                if (memcmp(buf.data() + i, SQLITE_MAGIC, sizeof(SQLITE_MAGIC)) == 0) { pos = i; have = true; break; }
            }
        }
    }
    return parts;
}

std::vector<std::string> parseTocNames(const std::string &plain, size_t xzStart)
{
    std::vector<std::string> names;
    size_t end = xzStart < plain.size() ? xzStart : plain.size();
    size_t i = 0;
    while (i + 3 <= end) {
        if (plain[i] == '.' && plain[i + 1] == 'd' && plain[i + 2] == 'b') {
            size_t j = i;
            while (j > 0 && plain[j - 1] >= '0' && plain[j - 1] <= '9') --j;
            if (j < i) {
                size_t k = j;
                if (k >= 6 && plain.compare(k - 6, 6, "group_") == 0) k -= 6;
                names.push_back(plain.substr(k, i + 3 - k));
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return names;
}

void *rsaGenerate(int bits)
{
    RSA *rsa = RSA_new();
    BIGNUM *e = BN_new();
    if (!rsa || !e) { if (rsa) RSA_free(rsa); if (e) BN_free(e); return NULL; }
    BN_set_word(e, 65537);
    int ok = RSA_generate_key_ex(rsa, bits, e, NULL);
    BN_free(e);
    if (ok != 1) { RSA_free(rsa); return NULL; }
    return rsa;
}

bool rsaPublicKeyDer(void *r, std::string &der)
{
    RSA *rsa = (RSA *)r;
    if (!rsa) return false;
    int len = i2d_RSA_PUBKEY(rsa, NULL);
    if (len <= 0) return false;
    der.assign((size_t)len, '\0');
    unsigned char *p = (unsigned char *)&der[0];
    return i2d_RSA_PUBKEY(rsa, &p) == len;
}

bool rsaPrivateDecrypt(void *r, const std::string &cipher, std::string &plain)
{
    RSA *rsa = (RSA *)r;
    if (!rsa || cipher.empty()) return false;
    std::string out((size_t)RSA_size(rsa), '\0');
    int n = RSA_private_decrypt((int)cipher.size(), (const unsigned char *)cipher.data(),
                                (unsigned char *)&out[0], rsa, RSA_PKCS1_PADDING);
    if (n < 0) return false;
    plain.assign(out, 0, (size_t)n);
    return true;
}

void rsaFree(void *r)
{
    if (r) RSA_free((RSA *)r);
}

static bool inflateAuto(const std::string &in, std::string &out)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 32) != Z_OK) return false;
    zs.next_in  = (Bytef *)in.data();
    zs.avail_in = (uInt)in.size();
    out.clear();
    char buf[16384];
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out  = (Bytef *)buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
        out.append(buf, sizeof(buf) - zs.avail_out);
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

static bool waitFd(int fd, bool forWrite, int ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = select(fd + 1, forWrite ? NULL : &set, forWrite ? &set : NULL, NULL, &tv);
    return r > 0;
}

static std::string lowerStr(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
    return s;
}

static std::string lastSslError()
{
    char buf[200];
    unsigned long e = ERR_get_error();
    if (!e) return std::string("(no OpenSSL error)");
    ERR_error_string_n(e, buf, sizeof(buf));
    return std::string(buf);
}

bool httpsGet(const std::string &host, int port, const std::string &path, const std::string &extraHeaders,
              int timeoutSec, std::string &body, int &status, std::string &info)
{
    body.clear();
    status = 0;
    info.clear();

    static bool inited = false;
    if (!inited) { SSL_library_init(); SSL_load_error_strings(); OpenSSL_add_all_algorithms(); inited = true; }

    struct addrinfo hints, *res = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    sprintf(portStr, "%d", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) { info = "DNS failed"; return false; }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int c = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (c == 0 || (errno == EINPROGRESS && waitFd(fd, true, timeoutSec * 1000))) {
            int soerr = 0;
            socklen_t sl = sizeof(soerr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
            if (soerr == 0) break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { info = "connect failed"; return false; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    struct timeval tv;
    tv.tv_sec = timeoutSec; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL_CTX *ctx = SSL_CTX_new(TLSv1_2_client_method());
    if (!ctx) { info = "SSL_CTX_new failed: " + lastSslError(); close(fd); return false; }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    SSL *ssl = SSL_new(ctx);
    if (!ssl) { info = "SSL_new failed"; SSL_CTX_free(ctx); close(fd); return false; }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    if (SSL_connect(ssl) != 1) {
        info = "TLS handshake failed: " + lastSslError();
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return false;
    }
    info = std::string(SSL_get_version(ssl)) + " " + SSL_get_cipher(ssl);

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extraHeaders +
                      "Accept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < req.size()) {
        int n = SSL_write(ssl, req.data() + sent, (int)(req.size() - sent));
        if (n <= 0) { info += " | SSL_write failed: " + lastSslError(); SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return false; }
        sent += (size_t)n;
    }

    std::string resp;
    char buf[16384];
    for (;;) {
        int n = SSL_read(ssl, buf, sizeof(buf));
        if (n > 0) {
            resp.append(buf, (size_t)n);
            if (resp.size() > (size_t)(96u << 20)) break;
            continue;
        }
        break;
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);

    size_t he = resp.find("\r\n\r\n");
    if (he == std::string::npos || resp.compare(0, 5, "HTTP/") != 0) { info += " | no valid HTTP response"; return false; }
    size_t sp = resp.find(' ');
    status = (sp == std::string::npos) ? 0 : atoi(resp.c_str() + sp + 1);
    std::string head = lowerStr(resp.substr(0, he));
    std::string raw = resp.substr(he + 4);

    bool chunked = head.find("transfer-encoding: chunked") != std::string::npos;
    bool gz = head.find("content-encoding: gzip") != std::string::npos || head.find("content-encoding: deflate") != std::string::npos;
    if (chunked) {
        std::string out;
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t le = raw.find("\r\n", pos);
            if (le == std::string::npos) break;
            unsigned long sz = strtoul(raw.substr(pos, le - pos).c_str(), NULL, 16);
            pos = le + 2;
            if (sz == 0) break;
            if (pos + sz > raw.size()) { out.append(raw, pos, std::string::npos); break; }
            out.append(raw, pos, sz);
            pos += sz + 2;
        }
        raw.swap(out);
    }
    if (gz) {
        std::string plain;
        if (inflateAuto(raw, plain)) raw.swap(plain); else info += " | gzip decode failed";
    }
    body.swap(raw);
    return true;
}

bool httpsRequest(const std::string &method, const std::string &host, int port, const std::string &path,
                  const std::string &extraHeaders, const std::string &reqBody, int timeoutSec,
                  std::string &body, int &status, std::string &respHead, std::string &info)
{
    body.clear();
    status = 0;
    respHead.clear();
    info.clear();

    static bool inited = false;
    if (!inited) { SSL_library_init(); SSL_load_error_strings(); OpenSSL_add_all_algorithms(); inited = true; }

    struct addrinfo hints, *res = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    sprintf(portStr, "%d", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) { info = "DNS failed"; return false; }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int c = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (c == 0 || (errno == EINPROGRESS && waitFd(fd, true, timeoutSec * 1000))) {
            int soerr = 0;
            socklen_t sl = sizeof(soerr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
            if (soerr == 0) break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { info = "connect failed"; return false; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    struct timeval tv;
    tv.tv_sec = timeoutSec; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    SSL_CTX *ctx = SSL_CTX_new(TLSv1_2_client_method());
    if (!ctx) { info = "SSL_CTX_new failed: " + lastSslError(); close(fd); return false; }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    SSL *ssl = SSL_new(ctx);
    if (!ssl) { info = "SSL_new failed"; SSL_CTX_free(ctx); close(fd); return false; }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    if (SSL_connect(ssl) != 1) {
        info = "TLS handshake failed: " + lastSslError();
        SSL_free(ssl); SSL_CTX_free(ctx); close(fd);
        return false;
    }
    info = std::string(SSL_get_version(ssl)) + " " + SSL_get_cipher(ssl);

    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extraHeaders;
    if (method != "GET") {
        char cl[48];
        sprintf(cl, "Content-Length: %lu\r\n", (unsigned long)reqBody.size());
        req += cl;
    }
    req += "Accept-Encoding: gzip, deflate\r\nConnection: close\r\n\r\n";
    req += reqBody;
    size_t sent = 0;
    while (sent < req.size()) {
        int n = SSL_write(ssl, req.data() + sent, (int)(req.size() - sent));
        if (n <= 0) { info += " | SSL_write failed: " + lastSslError(); SSL_free(ssl); SSL_CTX_free(ctx); close(fd); return false; }
        sent += (size_t)n;
    }

    std::string resp;
    char buf[16384];
    for (;;) {
        int n = SSL_read(ssl, buf, sizeof(buf));
        if (n > 0) {
            resp.append(buf, (size_t)n);
            if (resp.size() > (size_t)(96u << 20)) break;
            continue;
        }
        break;
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);

    size_t he = resp.find("\r\n\r\n");
    if (he == std::string::npos || resp.compare(0, 5, "HTTP/") != 0) { info += " | no valid HTTP response"; return false; }
    size_t sp = resp.find(' ');
    status = (sp == std::string::npos) ? 0 : atoi(resp.c_str() + sp + 1);
    respHead = resp.substr(0, he);
    std::string head = lowerStr(respHead);
    std::string raw = resp.substr(he + 4);

    bool chunked = head.find("transfer-encoding: chunked") != std::string::npos;
    bool gz = head.find("content-encoding: gzip") != std::string::npos || head.find("content-encoding: deflate") != std::string::npos;
    if (chunked) {
        std::string out;
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t le = raw.find("\r\n", pos);
            if (le == std::string::npos) break;
            unsigned long sz = strtoul(raw.substr(pos, le - pos).c_str(), NULL, 16);
            pos = le + 2;
            if (sz == 0) break;
            if (pos + sz > raw.size()) { out.append(raw, pos, std::string::npos); break; }
            out.append(raw, pos, sz);
            pos += sz + 2;
        }
        raw.swap(out);
    }
    if (gz) {
        std::string plain;
        if (inflateAuto(raw, plain)) raw.swap(plain); else info += " | gzip decode failed";
    }
    body.swap(raw);
    return true;
}

}
