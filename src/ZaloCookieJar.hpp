#ifndef ZALOCOOKIEJAR_HPP
#define ZALOCOOKIEJAR_HPP

#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QList>

class ZaloCookieJar : public QNetworkCookieJar
{
public:
    explicit ZaloCookieJar(QObject *parent = 0) : QNetworkCookieJar(parent) {}
    QList<QNetworkCookie> exportAll() const { return allCookies(); }
    void importAll(const QList<QNetworkCookie> &c) { setAllCookies(c); }
};

#endif
