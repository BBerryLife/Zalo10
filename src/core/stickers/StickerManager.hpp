#ifndef ZALO10_STICKER_MANAGER_HPP
#define ZALO10_STICKER_MANAGER_HPP

#include <QObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QSslError>

class QNetworkAccessManager;
class QNetworkReply;

// Sticker packs hosted in BBerryLife.github.io (Data/Zalo10/<pack>/*, listed by
// Data/Zalo10-stickers.json). The Shop downloads a pack to <home>/stickers/<pack>/; the chat sticker
// bar only ever reads those local files. Exposed to QML as "stickerManager".
//
// Qt4 / C++03 on purpose (same as the rest of the app): no lambdas, no range-for.
class StickerManager : public QObject
{
    Q_OBJECT
public:
    explicit StickerManager(QObject *parent = 0);

    // Shop. catalog() works offline once a manifest has been fetched at least once.
    Q_INVOKABLE void fetchCatalog();
    Q_INVOKABLE QVariantList catalog() const;
    Q_INVOKABLE bool catalogLoaded() const;
    Q_INVOKABLE void installPack(const QString &packId);
    Q_INVOKABLE void removePack(const QString &packId);
    Q_INVOKABLE bool isInstalled(const QString &packId) const;
    Q_INVOKABLE bool isInstalling(const QString &packId) const;

    // Chat sticker bar + settings page.
    Q_INVOKABLE QVariantList installedPacks() const;   // user order, enabled or not
    Q_INVOKABLE QVariantList enabledPacks() const;     // user order, enabled only
    Q_INVOKABLE QVariantList stickersOf(const QString &packId) const;
    Q_INVOKABLE void setPackEnabled(const QString &packId, bool enabled);
    Q_INVOKABLE void savePackOrder(const QVariantList &orderedIds);

signals:
    void catalogReady();
    void catalogFailed(const QString &error);
    void previewReady(const QString &packId, const QString &localPath);
    void packProgress(const QString &packId, int done, int total);
    void packInstalled(const QString &packId);
    void packFailed(const QString &packId, const QString &error);
    void packRemoved(const QString &packId);
    // Anything that changes the installed list, its order or the enabled flags.
    void installedChanged();

private slots:
    void onJobFinished();
    void onSslErrors(const QList<QSslError> &errors);

private:
    enum JobKind { JobCatalog = 0, JobInstall = 1, JobPreview = 2 };

    struct Job {
        int     kind;
        QString packId;
        QString file;
        QString dest;
        bool    mirror;   // true = already retrying on the jsDelivr mirror
    };

    static QString rootDir();
    static QString packDir(const QString &packId);
    static QString previewDir();
    static bool    isSafeName(const QString &name);
    static bool    isGifName(const QString &name);
    static QString urlFor(const QString &relPath, bool mirror);
    static void    removeDirRecursive(const QString &path);

    QStringList installedIds() const;                  // unordered
    QStringList orderedInstalledIds() const;           // saved order, then the rest A-Z
    QString     previewPathFor(const QString &packId) const;
    QString     firstStickerPath(const QString &packId) const;

    void loadCatalogFromCache();
    bool applyCatalogJson(const QByteArray &raw);
    void queuePreviews();
    void enqueueFront(const QList<Job> &jobs);
    void startNext();
    void startJob(const Job &job);
    void finishCurrent(bool ok, const QByteArray &data, const QString &error);
    void failPack(const QString &packId, const QString &error);

    QNetworkAccessManager *m_net;
    QNetworkReply         *m_reply;
    QList<Job>             m_queue;
    Job                    m_current;
    bool                   m_busy;

    QVariantList               m_catalog;      // [{id,name,count,size,animated}]
    QMap<QString, QStringList> m_files;        // packId -> file names in manifest order
    bool                       m_catalogLoaded;

    QSet<QString>        m_installing;
    QMap<QString, int>   m_installDone;
    QMap<QString, int>   m_installTotal;
};

#endif
