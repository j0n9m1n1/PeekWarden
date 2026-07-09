#pragma once

#include "BwClient.h"

#include <functional>

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QWidget>

class QLabel;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QResizeEvent;

class QuickWindow : public QWidget
{
public:
    explicit QuickWindow(QWidget* parent = nullptr);

    void preloadVault();
    void showQuick();
    void showSettings();
    bool lockVault(QString* errorMessage = nullptr);
    void applySettings();
    void setSettingsChangedCallback(std::function<void()> callback);

private:
    enum class AuthMode
    {
        None,
        Login,
        Unlock
    };

    struct PrepareResult
    {
        bool available = false;
        BwClient::StatusInfo statusInfo;
        QString errorMessage;
    };

    struct AuthResult
    {
        bool ok = false;
        bool loginMode = false;
        QString errorMessage;
    };

    struct SearchResult
    {
        QString keyword;
        QVector<VaultItem> items;
        QString errorMessage;
    };

    struct ItemsResult
    {
        QVector<VaultItem> items;
        QString errorMessage;
    };

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void openSettings();
    void applyStyle();
    void updateWindowMask();
    void updateFooter();
    void updateAuthTexts();
    void updateAuthHints();
    void updateWindowSize();
    void reload();
    void startSearch(const QString& keyword);
    void handleSearchFinished();
    void startItemLoad(bool forceRefresh = false, bool quiet = false);
    void handleItemsFinished();
    QVector<VaultItem> filterCachedItems(const QString& keyword) const;
    void restorePreservedSearch();
    bool shouldRestorePreservedSearch() const;
    void preserveSearchState();
    void recordRecentItem(const QString& itemId);
    int recentItemRank(const QString& itemId) const;
    const VaultItem* cachedItemById(const QString& itemId) const;
    QString totpCodeForItem(const QString& itemId, QString* errorMessage = nullptr) const;
    void selectItemById(const QString& itemId);
    void moveSelection(int delta);
    void prepareVault(bool interactive = true);
    void handlePrepareFinished();
    void showAuthPanel(AuthMode mode);
    void hideAuthPanel();
    void submitAuth();
    void handleAuthFinished();
    QString selectedAuthServerUrl() const;
    void setBusy(bool busy, const QString& message = {});
    void setAuthInputsEnabled(bool enabled);
    void syncVault();
    void updateAutoSyncTimer();
    void requestFaviconForRow(QListWidgetItem* row, const VaultItem& item);
    void fetchFavicon(const QString& host, const QUrl& url, bool retriedHttp);
    void handleFaviconFinished(QNetworkReply* reply);
    void applyFaviconToVisibleRows(const QString& host, const QIcon& icon);
    void scheduleFocusLossCheck();
    void hideIfFocusLost();
    void copyPassword(QListWidgetItem* item);
    void copyUsername(QListWidgetItem* item);
    void copyTotp(QListWidgetItem* item);
    void showItemDetails(QListWidgetItem* item);
    void copyTextAndHide(const QString& text, bool clearAfterDelay);
    void setStatus(const QString& message);
    QListWidgetItem* currentItem() const;
    void moveToConfiguredPosition();

    QLineEdit* m_search = nullptr;
    QProgressBar* m_busyBar = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_authPanel = nullptr;
    QLabel* m_authTitle = nullptr;
    QLabel* m_authNote = nullptr;
    QLabel* m_authAccountLabel = nullptr;
    QLabel* m_authServerLabel = nullptr;
    QLabel* m_authEmailLabel = nullptr;
    QLabel* m_authPasswordLabel = nullptr;
    QLabel* m_authMethodLabel = nullptr;
    QLabel* m_authCodeLabel = nullptr;
    QLabel* m_authAccount = nullptr;
    QComboBox* m_authServer = nullptr;
    QLineEdit* m_authCustomServer = nullptr;
    QLineEdit* m_authEmail = nullptr;
    QLineEdit* m_authPassword = nullptr;
    QComboBox* m_authMethod = nullptr;
    QLineEdit* m_authCode = nullptr;
    QWidget* m_authHintBar = nullptr;
    QHBoxLayout* m_authHintLayout = nullptr;
    QLabel* m_authBusyLabel = nullptr;
    QProgressBar* m_authBusyBar = nullptr;
    QPushButton* m_authSubmit = nullptr;
    QPushButton* m_authCancel = nullptr;
    QWidget* m_footer = nullptr;
    QGridLayout* m_footerLayout = nullptr;
    QNetworkAccessManager* m_faviconManager = nullptr;
    QHash<QString, QIcon> m_faviconCache;
    QSet<QString> m_pendingFavicons;
    QTimer m_debounce;
    QTimer m_refreshTimer;
    BwClient m_bw;
    QFutureWatcher<PrepareResult> m_prepareWatcher;
    QFutureWatcher<AuthResult> m_authWatcher;
    QFutureWatcher<SearchResult> m_searchWatcher;
    QFutureWatcher<ItemsResult> m_itemsWatcher;
    AuthMode m_authMode = AuthMode::None;
    QString m_queuedSearchText;
    QVector<VaultItem> m_cachedItems;
    QStringList m_recentItemIds;
    bool m_itemsReady = false;
    bool m_preloadingVault = false;
    bool m_quietItemLoad = false;
    bool m_syncing = false;
    QElapsedTimer m_preservedSearchTimer;
    QString m_preservedSearchText;
    QString m_preservedItemId;
    QString m_currentUserEmail;
    QString m_currentServerUrl;
    std::function<void()> m_settingsChanged;
};
