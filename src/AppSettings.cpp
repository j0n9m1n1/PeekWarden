#include "AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QSettings>

namespace
{
constexpr int DefaultWindowWidth = 540;
constexpr int DefaultWindowHeight = 360;
constexpr int DefaultWindowPosition = 0;
constexpr int DefaultSearchResultLimit = 8;
constexpr int DefaultReopenRetentionSeconds = 10;
constexpr int DefaultAutoSyncIntervalMinutes = 5;
constexpr int DefaultQuickFontSize = 13;
constexpr double DefaultWindowOpacity = 0.96;
const char* DefaultTheme = "dark";

const char* ShowHotkeyKey = "hotkeys/showQuick";
const char* WindowWidthKey = "window/width";
const char* WindowHeightKey = "window/height";
const char* SearchResultLimitKey = "search/resultLimit";
const char* ShowShortcutHintsKey = "ui/showShortcutHints";
const char* ShortcutPresetKey = "shortcuts/preset";
const char* ShowResultIconsKey = "ui/showResultIcons";
const char* FetchFaviconsFromWebKey = "ui/fetchFaviconsFromWeb";
const char* ShowResultUsernameKey = "ui/showResultUsername";
const char* ShowResultUriKey = "ui/showResultUri";
const char* ShowResultTypeKey = "ui/showResultType";
const char* QuickFontFamilyKey = "ui/quickFontFamily";
const char* QuickFontSizeKey = "ui/quickFontSize";
const char* QuickFontBoldKey = "ui/quickFontBold";
const char* CustomWindowPositionEnabledKey = "window/customPositionEnabled";
const char* WindowPositionXKey = "window/positionX";
const char* WindowPositionYKey = "window/positionY";
const char* WindowOpacityKey = "window/opacity";
const char* RoundedCornersKey = "window/roundedCorners";
const char* ThemeKey = "ui/theme";
const char* ReopenRetentionSecondsKey = "window/reopenRetentionSeconds";
const char* AutoSyncIntervalMinutesKey = "bitwarden/autoSyncIntervalMinutes";
const char* CloseOnFocusLossKey = "window/closeOnFocusLoss";
const char* CredentialSessionStorageEnabledKey = "security/credentialSessionStorageEnabled";
const char* StartOnLoginKey = "system/startOnLogin";
const char* StartOnLoginName = "PeekWarden";
const char* BwProgramOverrideKey = "bitwarden/bwProgram";
const char* BitwardenServerModeKey = "bitwarden/serverMode";
const char* CustomBitwardenServerUrlKey = "bitwarden/customServerUrl";
const char* LanguageKey = "ui/language";

QKeySequence defaultShowHotkey()
{
    return QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space);
}

QString quotedApplicationPath()
{
    return QString("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}
}

QKeySequence AppSettings::showHotkey()
{
    const QString value = QSettings().value(ShowHotkeyKey, defaultHotkeyText()).toString();
    const QKeySequence sequence(value, QKeySequence::PortableText);

    if (!sequence.isEmpty())
        return sequence;

    return defaultShowHotkey();
}

void AppSettings::setShowHotkey(const QKeySequence& sequence)
{
    QSettings().setValue(ShowHotkeyKey, sequence.toString(QKeySequence::PortableText));
}

QString AppSettings::showHotkeyText()
{
    return showHotkey().toString(QKeySequence::NativeText);
}

int AppSettings::windowWidth()
{
    return QSettings().value(WindowWidthKey, DefaultWindowWidth).toInt();
}

void AppSettings::setWindowWidth(int width)
{
    QSettings().setValue(WindowWidthKey, width);
}

int AppSettings::windowHeight()
{
    return QSettings().value(WindowHeightKey, DefaultWindowHeight).toInt();
}

void AppSettings::setWindowHeight(int height)
{
    QSettings().setValue(WindowHeightKey, height);
}

int AppSettings::searchResultLimit()
{
    return qBound(1, QSettings().value(SearchResultLimitKey, DefaultSearchResultLimit).toInt(), 20);
}

void AppSettings::setSearchResultLimit(int limit)
{
    QSettings().setValue(SearchResultLimitKey, qBound(1, limit, 20));
}

bool AppSettings::showShortcutHints()
{
    return QSettings().value(ShowShortcutHintsKey, true).toBool();
}

void AppSettings::setShowShortcutHints(bool enabled)
{
    QSettings().setValue(ShowShortcutHintsKey, enabled);
}

QString AppSettings::shortcutPreset()
{
    const QString value = QSettings().value(ShortcutPresetKey, "1password").toString().trimmed().toLower();
    if (value == QStringLiteral("bitwarden"))
        return QStringLiteral("bitwarden");

    return QStringLiteral("1password");
}

void AppSettings::setShortcutPreset(const QString& preset)
{
    const QString normalized = preset.trimmed().toLower();
    QSettings().setValue(ShortcutPresetKey,
        normalized == QStringLiteral("bitwarden") ? QStringLiteral("bitwarden") : QStringLiteral("1password"));
}

bool AppSettings::showResultIcons()
{
    return QSettings().value(ShowResultIconsKey, true).toBool();
}

void AppSettings::setShowResultIcons(bool enabled)
{
    QSettings().setValue(ShowResultIconsKey, enabled);
}

bool AppSettings::fetchFaviconsFromWeb()
{
    return QSettings().value(FetchFaviconsFromWebKey, true).toBool();
}

void AppSettings::setFetchFaviconsFromWeb(bool enabled)
{
    QSettings().setValue(FetchFaviconsFromWebKey, enabled);
}

bool AppSettings::showResultUsername()
{
    return QSettings().value(ShowResultUsernameKey, true).toBool();
}

void AppSettings::setShowResultUsername(bool enabled)
{
    QSettings().setValue(ShowResultUsernameKey, enabled);
}

bool AppSettings::showResultUri()
{
    return QSettings().value(ShowResultUriKey, false).toBool();
}

void AppSettings::setShowResultUri(bool enabled)
{
    QSettings().setValue(ShowResultUriKey, enabled);
}

bool AppSettings::showResultType()
{
    return QSettings().value(ShowResultTypeKey, false).toBool();
}

void AppSettings::setShowResultType(bool enabled)
{
    QSettings().setValue(ShowResultTypeKey, enabled);
}

QString AppSettings::quickFontFamily()
{
    const QString family = QSettings().value(QuickFontFamilyKey).toString().trimmed();
    if (!family.isEmpty())
        return family;

    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

void AppSettings::setQuickFontFamily(const QString& family)
{
    QSettings().setValue(QuickFontFamilyKey, family.trimmed());
}

int AppSettings::quickFontSize()
{
    return qBound(9, QSettings().value(QuickFontSizeKey, DefaultQuickFontSize).toInt(), 22);
}

void AppSettings::setQuickFontSize(int size)
{
    QSettings().setValue(QuickFontSizeKey, qBound(9, size, 22));
}

bool AppSettings::quickFontBold()
{
    return QSettings().value(QuickFontBoldKey, true).toBool();
}

void AppSettings::setQuickFontBold(bool enabled)
{
    QSettings().setValue(QuickFontBoldKey, enabled);
}

bool AppSettings::customWindowPositionEnabled()
{
    return QSettings().value(CustomWindowPositionEnabledKey, false).toBool();
}

void AppSettings::setCustomWindowPositionEnabled(bool enabled)
{
    QSettings().setValue(CustomWindowPositionEnabledKey, enabled);
}

int AppSettings::windowPositionX()
{
    return qBound(-20000, QSettings().value(WindowPositionXKey, DefaultWindowPosition).toInt(), 20000);
}

void AppSettings::setWindowPositionX(int x)
{
    QSettings().setValue(WindowPositionXKey, qBound(-20000, x, 20000));
}

int AppSettings::windowPositionY()
{
    return qBound(-20000, QSettings().value(WindowPositionYKey, DefaultWindowPosition).toInt(), 20000);
}

void AppSettings::setWindowPositionY(int y)
{
    QSettings().setValue(WindowPositionYKey, qBound(-20000, y, 20000));
}

double AppSettings::windowOpacity()
{
    return QSettings().value(WindowOpacityKey, DefaultWindowOpacity).toDouble();
}

void AppSettings::setWindowOpacity(double opacity)
{
    QSettings().setValue(WindowOpacityKey, opacity);
}

bool AppSettings::roundedCorners()
{
    return QSettings().value(RoundedCornersKey, true).toBool();
}

void AppSettings::setRoundedCorners(bool enabled)
{
    QSettings().setValue(RoundedCornersKey, enabled);
}

QString AppSettings::theme()
{
    const QString value = QSettings().value(ThemeKey, DefaultTheme).toString().trimmed().toLower();
    return value == QStringLiteral("light") ? QStringLiteral("light") : QStringLiteral("dark");
}

void AppSettings::setTheme(const QString& theme)
{
    const QString value = theme.trimmed().toLower();
    QSettings().setValue(ThemeKey, value == QStringLiteral("light") ? QStringLiteral("light") : QStringLiteral("dark"));
}

int AppSettings::reopenRetentionSeconds()
{
    return qBound(0, QSettings().value(ReopenRetentionSecondsKey, DefaultReopenRetentionSeconds).toInt(), 120);
}

void AppSettings::setReopenRetentionSeconds(int seconds)
{
    QSettings().setValue(ReopenRetentionSecondsKey, qBound(0, seconds, 120));
}

int AppSettings::autoSyncIntervalMinutes()
{
    return qBound(0, QSettings().value(AutoSyncIntervalMinutesKey, DefaultAutoSyncIntervalMinutes).toInt(), 1440);
}

void AppSettings::setAutoSyncIntervalMinutes(int minutes)
{
    QSettings().setValue(AutoSyncIntervalMinutesKey, qBound(0, minutes, 1440));
}

bool AppSettings::closeOnFocusLoss()
{
    return QSettings().value(CloseOnFocusLossKey, true).toBool();
}

void AppSettings::setCloseOnFocusLoss(bool enabled)
{
    QSettings().setValue(CloseOnFocusLossKey, enabled);
}

bool AppSettings::credentialSessionStorageEnabled()
{
    return QSettings().value(CredentialSessionStorageEnabledKey, true).toBool();
}

void AppSettings::setCredentialSessionStorageEnabled(bool enabled)
{
    QSettings().setValue(CredentialSessionStorageEnabledKey, enabled);
}

bool AppSettings::startOnLoginEnabled()
{
    return QSettings().value(StartOnLoginKey, true).toBool();
}

void AppSettings::setStartOnLoginEnabled(bool enabled)
{
    QSettings().setValue(StartOnLoginKey, enabled);
#ifdef Q_OS_WIN
    QSettings runKey(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        QSettings::NativeFormat);
    if (enabled)
        runKey.setValue(StartOnLoginName, quotedApplicationPath());
    else
        runKey.remove(StartOnLoginName);
#endif
}

void AppSettings::applyStartupPreference()
{
    setStartOnLoginEnabled(startOnLoginEnabled());
}

QString AppSettings::bwProgramOverride()
{
    return QSettings().value(BwProgramOverrideKey).toString().trimmed();
}

void AppSettings::setBwProgramOverride(const QString& path)
{
    QSettings().setValue(BwProgramOverrideKey, path.trimmed());
}

QString AppSettings::bitwardenServerMode()
{
    const QString value = QSettings().value(BitwardenServerModeKey, "com").toString().trimmed().toLower();

    if (value == "eu" || value == "custom")
        return value;

    return "com";
}

void AppSettings::setBitwardenServerMode(const QString& mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == "eu" || normalized == "custom") {
        QSettings().setValue(BitwardenServerModeKey, normalized);
        return;
    }

    QSettings().setValue(BitwardenServerModeKey, "com");
}

QString AppSettings::customBitwardenServerUrl()
{
    return QSettings().value(CustomBitwardenServerUrlKey).toString().trimmed();
}

void AppSettings::setCustomBitwardenServerUrl(const QString& url)
{
    QSettings().setValue(CustomBitwardenServerUrlKey, url.trimmed());
}

QString AppSettings::language()
{
    const QString value = QSettings().value(LanguageKey, "ko").toString().trimmed().toLower();
    return value.isEmpty() ? QStringLiteral("ko") : value;
}

void AppSettings::setLanguage(const QString& language)
{
    const QString normalized = language.trimmed().toLower();
    QSettings().setValue(LanguageKey, normalized.isEmpty() ? QStringLiteral("en") : normalized);
}

bool AppSettings::isKorean()
{
    return language().startsWith(QStringLiteral("ko"));
}

QString AppSettings::defaultHotkeyText()
{
    return defaultShowHotkey().toString(QKeySequence::PortableText);
}
