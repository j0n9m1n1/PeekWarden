#pragma once

#include <QKeySequence>
#include <QString>

class AppSettings
{
public:
    static QKeySequence showHotkey();
    static void setShowHotkey(const QKeySequence& sequence);
    static QString showHotkeyText();

    static int windowWidth();
    static void setWindowWidth(int width);

    static int windowHeight();
    static void setWindowHeight(int height);

    static int searchResultLimit();
    static void setSearchResultLimit(int limit);

    static bool showShortcutHints();
    static void setShowShortcutHints(bool enabled);

    static QString shortcutPreset();
    static void setShortcutPreset(const QString& preset);

    static bool showResultIcons();
    static void setShowResultIcons(bool enabled);

    static bool fetchFaviconsFromWeb();
    static void setFetchFaviconsFromWeb(bool enabled);

    static bool showResultUsername();
    static void setShowResultUsername(bool enabled);

    static bool showResultUri();
    static void setShowResultUri(bool enabled);

    static bool showResultType();
    static void setShowResultType(bool enabled);

    static QString quickFontFamily();
    static void setQuickFontFamily(const QString& family);

    static int quickFontSize();
    static void setQuickFontSize(int size);

    static bool quickFontBold();
    static void setQuickFontBold(bool enabled);

    static bool customWindowPositionEnabled();
    static void setCustomWindowPositionEnabled(bool enabled);

    static int windowPositionX();
    static void setWindowPositionX(int x);

    static int windowPositionY();
    static void setWindowPositionY(int y);

    static double windowOpacity();
    static void setWindowOpacity(double opacity);

    static bool roundedCorners();
    static void setRoundedCorners(bool enabled);

    static QString windowBackgroundColor();
    static void setWindowBackgroundColor(const QString& color);

    static int reopenRetentionSeconds();
    static void setReopenRetentionSeconds(int seconds);

    static int autoSyncIntervalMinutes();
    static void setAutoSyncIntervalMinutes(int minutes);

    static bool closeOnFocusLoss();
    static void setCloseOnFocusLoss(bool enabled);

    static bool credentialSessionStorageEnabled();
    static void setCredentialSessionStorageEnabled(bool enabled);

    static bool startOnLoginEnabled();
    static void setStartOnLoginEnabled(bool enabled);
    static void applyStartupPreference();

    static QString bwProgramOverride();
    static void setBwProgramOverride(const QString& path);

    static QString bitwardenServerMode();
    static void setBitwardenServerMode(const QString& mode);

    static QString customBitwardenServerUrl();
    static void setCustomBitwardenServerUrl(const QString& url);

    static QString language();
    static void setLanguage(const QString& language);
    static bool isKorean();

    static QString defaultHotkeyText();
};
