#ifndef ZALOCOOKIEJAR_HPP
#define ZALOCOOKIEJAR_HPP

#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QList>

// Cookie jar mở public allCookies()/setAllCookies() để lưu/khôi phục
// xuống QSettings. Mặc định jar chỉ nằm trong RAM → mở lại app là mất hết
// cookie (kể cả cookie HttpOnly/domain-specific mà m_cookies không bắt đủ).
class ZaloCookieJar : public QNetworkCookieJar
{
public:
    explicit ZaloCookieJar(QObject *parent = 0) : QNetworkCookieJar(parent) {}
    QList<QNetworkCookie> exportAll() const { return allCookies(); }
    void importAll(const QList<QNetworkCookie> &c) { setAllCookies(c); }
};

#endif
