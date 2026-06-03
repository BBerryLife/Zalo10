#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>
#include <QString>
#include <QSettings>
#include <bb/system/InvokeManager>
#include <bb/cascades/ThemeSupport>

namespace bb { namespace cascades { class LocaleHandler; class AbstractPane; } }
class QTranslator;

class ApplicationUI : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationUI();
    virtual ~ApplicationUI() {}

public slots:
    // Dùng InvokeManager để mở email composer — học từ dự án cũ
    void invokeEmail(const QString &to, const QString &subject);
    void minimizeApp();
    // Lưu dark theme preference — áp dụng ngay và save vào QSettings
    Q_INVOKABLE void setDarkTheme(bool dark);
    Q_INVOKABLE bool getDarkTheme();

private slots:
    void onSystemLanguageChanged();

private:
    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
};

#endif
