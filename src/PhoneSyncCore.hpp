#ifndef PHONESYNCCORE_HPP
#define PHONESYNCCORE_HPP
/*
 * PhoneSyncCore — phần thuần C++ (không Qt) của "đồng bộ tin nhắn từ điện thoại":
 *   file .db.crypt --AES-256-CBC (64KB/chunk, IV=0)--> ZDB4.0 --XZ--> các SQLite nối nhau.
 * Tách riêng để kiểm thử độc lập (không cần thiết bị). Định dạng theo tài liệu docs-decrypt-db-zalo
 * và đã đối chiếu với file thật. Chỉ dùng C++03.
 */
#include <string>
#include <vector>
#include <stddef.h>

namespace PhoneSyncCore {

struct Span { size_t offset; size_t size; };

// Key AES = ASCII của 32 ký tự hex VIẾT HOA đầu tiên (không hex-decode). false nếu keyHex < 32 ký tự.
bool deriveAesKey(const std::string &keyHex, unsigned char key[32]);

// Cắt footer TLV rồi giải mã từng chunk 64KB (IV reset về 0 mỗi chunk).
bool decryptDbCrypt(const std::string &data, const std::string &aesKeyHex,
                    std::string &plain, bool &footerFound, std::string &err);

// Tìm magic XZ trong ZDB4.0, giải nén mọi stream và nối lại. xzOffset = offset stream đầu tiên.
bool xzDecodeAll(const std::string &plain, std::string &payload, size_t &xzOffset, std::string &err);

// Tách các SQLite nối liền nhau (kích thước = page_size * page_count ở header).
std::vector<Span> splitSqlite(const std::string &payload);

// Tên sub-DB ("204278670.db", "group_634862855.db") trong phần TOC trước stream XZ, theo thứ tự.
std::vector<std::string> parseTocNames(const std::string &plain, size_t xzStart);

// RSA 2048: tạo khóa, xuất public key dạng SubjectPublicKeyInfo DER, giải mã PKCS#1 v1.5.
// rsa là con trỏ opaque (RSA*) — giải phóng bằng rsaFree().
void *rsaGenerate(int bits);
bool  rsaPublicKeyDer(void *rsa, std::string &der);
bool  rsaPrivateDecrypt(void *rsa, const std::string &cipher, std::string &plain);
void  rsaFree(void *rsa);

std::string toHex(const std::string &bytes);

// GET qua HTTPS bằng OpenSSL thuần (ép TLS 1.2, socket POSIX chặn, không dùng QSslSocket vốn rất hạn chế trên BB10).
// extraHeaders: các dòng header, mỗi dòng kết thúc "\r\n" (không gồm Host/Connection/Accept-Encoding).
// Theo chunked + gzip/deflate. info nhận phiên bản TLS, cipher và lỗi (nếu có) để ghi log.
// Trả true nếu nhận được phản hồi HTTP hợp lệ (xem status); false nếu lỗi mạng/TLS.
bool httpsGet(const std::string &host, int port, const std::string &path, const std::string &extraHeaders,
              int timeoutSec, std::string &body, int &status, std::string &info);

} // namespace PhoneSyncCore

#endif
