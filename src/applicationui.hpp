#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>
#include <QString>
#include <QSettings>
#include <bb/system/InvokeManager>
#include <bb/cascades/ThemeSupport>

namespace bb { namespace cascades { class LocaleHandler; class AbstractPane; } }
class QTranslator;
class ZaloService;

class ApplicationUI : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationUI();
    virtual ~ApplicationUI() {}

public slots:
    void invokeEmail(const QString &to, const QString &subject);
    void minimizeApp();
    Q_INVOKABLE void setDarkTheme(bool dark);
    Q_INVOKABLE bool getDarkTheme();
    Q_INVOKABLE QString appVersion();

signals:
    void openThreadRequested(const QString &threadId, bool isGroup);

private slots:
    void onSystemLanguageChanged();
    void onManualExit();
    void onInvoked(const bb::system::InvokeRequest &request);

private:
    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
    ZaloService*                  m_zService;
};

#endif
