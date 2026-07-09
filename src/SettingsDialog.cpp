#include "SettingsDialog.h"

#include "AppSettings.h"
#include "I18n.h"

#include <QColor>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtGlobal>

namespace
{
QString uiText(const char* sourceText, const char* koreanFallback = nullptr)
{
    return I18n::translate("SettingsDialog", sourceText, koreanFallback);
}

QString recommendedFontFamily(const QString& languageCode)
{
    const QString normalized = languageCode.trimmed().toLower();
    const QStringList candidates = normalized.startsWith(QStringLiteral("ko"))
        ? QStringList { QStringLiteral("맑은 고딕"), QStringLiteral("Malgun Gothic"), QStringLiteral("Noto Sans CJK KR"), QStringLiteral("Noto Sans KR") }
        : QStringList { QStringLiteral("Segoe UI"), QStringLiteral("Arial") };
    const QStringList families = QFontDatabase::families();

    for (const QString& candidate : candidates) {
        for (const QString& family : families) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0)
                return family;
        }
    }

    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
}

int recommendedFontSize(const QString& languageCode)
{
    return languageCode.trimmed().toLower().startsWith(QStringLiteral("ko")) ? 13 : 12;
}
}

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(uiText("PeekWarden Settings", "PeekWarden 설정"));
    setModal(true);
    resize(640, 560);

    m_languageCombo = new QComboBox(this);
    const QString currentLanguage = AppSettings::language();
    const QStringList languageCodes = I18n::availableLanguageCodes();
    for (const QString& code : languageCodes)
        m_languageCombo->addItem(I18n::languageName(code), code);
    const int languageIndex = m_languageCombo->findData(currentLanguage);
    m_languageCombo->setCurrentIndex(languageIndex >= 0 ? languageIndex : m_languageCombo->findData(QStringLiteral("en")));
    m_languageCombo->setMaximumWidth(260);

    m_hotkeyEdit = new QKeySequenceEdit(AppSettings::showHotkey(), this);
    m_hotkeyEdit->setMaximumSequenceLength(1);
    m_hotkeyEdit->setMaximumWidth(260);

    m_shortcutPresetCombo = new QComboBox(this);
    m_shortcutPresetCombo->addItem(uiText("1Password style", "1Password 스타일"), "1password");
    m_shortcutPresetCombo->addItem(uiText("Bitwarden style", "Bitwarden 스타일"), "bitwarden");
    const int shortcutPresetIndex = m_shortcutPresetCombo->findData(AppSettings::shortcutPreset());
    m_shortcutPresetCombo->setCurrentIndex(shortcutPresetIndex >= 0 ? shortcutPresetIndex : 0);
    m_shortcutPresetCombo->setMaximumWidth(260);

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(360, 1200);
    m_widthSpin->setSuffix(" px");
    m_widthSpin->setValue(AppSettings::windowWidth());
    m_widthSpin->setFixedWidth(92);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(180, 900);
    m_heightSpin->setSuffix(" px");
    m_heightSpin->setValue(AppSettings::windowHeight());
    m_heightSpin->setFixedWidth(92);

    m_resultLimitSpin = new QSpinBox(this);
    m_resultLimitSpin->setRange(1, 20);
    m_resultLimitSpin->setSuffix(uiText(" rows", " 개"));
    m_resultLimitSpin->setValue(AppSettings::searchResultLimit());
    m_resultLimitSpin->setFixedWidth(92);

    m_showShortcutHintsCheck = new QCheckBox(uiText("Shortcut hints", "단축키 안내"), this);
    m_showShortcutHintsCheck->setChecked(AppSettings::showShortcutHints());

    m_showResultIconsCheck = new QCheckBox(uiText("Show icons", "아이콘 표시"), this);
    m_showResultIconsCheck->setChecked(AppSettings::showResultIcons());

    m_fetchFaviconsFromWebCheck = new QCheckBox(uiText("Web favicons", "웹 favicon"), this);
    m_fetchFaviconsFromWebCheck->setChecked(AppSettings::fetchFaviconsFromWeb());

    m_showResultUsernameCheck = new QCheckBox(uiText("Username", "사용자 이름"), this);
    m_showResultUsernameCheck->setChecked(AppSettings::showResultUsername());

    m_showResultUriCheck = new QCheckBox(uiText("Website", "웹 사이트"), this);
    m_showResultUriCheck->setChecked(AppSettings::showResultUri());

    m_showResultTypeCheck = new QCheckBox(uiText("Item type", "항목 유형"), this);
    m_showResultTypeCheck->setChecked(AppSettings::showResultType());

    auto* resultFieldsLayout = new QHBoxLayout;
    resultFieldsLayout->setContentsMargins(0, 0, 0, 0);
    resultFieldsLayout->setSpacing(12);
    resultFieldsLayout->addWidget(m_showResultUsernameCheck);
    resultFieldsLayout->addWidget(m_showResultUriCheck);
    resultFieldsLayout->addWidget(m_showResultTypeCheck);
    resultFieldsLayout->addStretch(1);

    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setCurrentFont(QFont(AppSettings::quickFontFamily()));
    m_fontCombo->setMaximumWidth(180);

    m_fontSizeSpin = new QSpinBox(this);
    m_fontSizeSpin->setRange(9, 22);
    m_fontSizeSpin->setSuffix(" pt");
    m_fontSizeSpin->setValue(AppSettings::quickFontSize());
    m_fontSizeSpin->setFixedWidth(82);

    m_fontBoldCheck = new QCheckBox(uiText("Bold", "굵게"), this);
    m_fontBoldCheck->setChecked(AppSettings::quickFontBold());

    auto* fontLayout = new QHBoxLayout;
    fontLayout->setContentsMargins(0, 0, 0, 0);
    fontLayout->setSpacing(8);
    fontLayout->addWidget(m_fontCombo, 1);
    fontLayout->addWidget(m_fontSizeSpin);
    fontLayout->addWidget(m_fontBoldCheck);

    m_customPositionCheck = new QCheckBox(uiText("Custom", "직접 지정"), this);
    m_customPositionCheck->setChecked(AppSettings::customWindowPositionEnabled());

    m_positionXSpin = new QSpinBox(this);
    m_positionXSpin->setRange(-20000, 20000);
    m_positionXSpin->setPrefix("X ");
    m_positionXSpin->setSuffix(" px");
    m_positionXSpin->setValue(AppSettings::windowPositionX());
    m_positionXSpin->setFixedWidth(92);

    m_positionYSpin = new QSpinBox(this);
    m_positionYSpin->setRange(-20000, 20000);
    m_positionYSpin->setPrefix("Y ");
    m_positionYSpin->setSuffix(" px");
    m_positionYSpin->setValue(AppSettings::windowPositionY());
    m_positionYSpin->setFixedWidth(92);

    auto* positionLayout = new QHBoxLayout;
    positionLayout->setContentsMargins(0, 0, 0, 0);
    positionLayout->addWidget(m_customPositionCheck);
    positionLayout->addWidget(m_positionXSpin);
    positionLayout->addWidget(m_positionYSpin);

    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(70, 100);
    m_opacitySlider->setValue(qRound(AppSettings::windowOpacity() * 100.0));
    m_opacitySlider->setMaximumWidth(150);

    m_opacityValue = new QLabel(this);
    m_opacityValue->setFixedWidth(42);
    updateOpacityLabel();

    auto* opacityLayout = new QHBoxLayout;
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacityValue);

    m_cornerCombo = new QComboBox(this);
    m_cornerCombo->addItem(uiText("Square", "사각"), false);
    m_cornerCombo->addItem(uiText("Rounded", "둥근 모서리"), true);
    m_cornerCombo->setCurrentIndex(AppSettings::roundedCorners() ? 1 : 0);
    m_cornerCombo->setMaximumWidth(120);

    m_backgroundColorButton = new QPushButton(this);
    m_backgroundColorButton->setMaximumWidth(110);
    updateBackgroundColorButton();

    auto* shortcutsTitle = new QLabel(uiText("Shortcuts", "단축키"), this);
    shortcutsTitle->setObjectName("settingsSectionTitle");

    m_shortcutsLabel = new QLabel(this);
    m_shortcutsLabel->setObjectName("settingsShortcutText");
    m_shortcutsLabel->setWordWrap(true);
    m_shortcutsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_reopenRetentionSpin = new QSpinBox(this);
    m_reopenRetentionSpin->setRange(0, 120);
    m_reopenRetentionSpin->setSuffix(uiText(" sec", " 초"));
    m_reopenRetentionSpin->setSpecialValueText(uiText("Disabled", "사용 안 함"));
    m_reopenRetentionSpin->setValue(AppSettings::reopenRetentionSeconds());
    m_reopenRetentionSpin->setFixedWidth(94);

    m_autoSyncIntervalSpin = new QSpinBox(this);
    m_autoSyncIntervalSpin->setRange(0, 1440);
    m_autoSyncIntervalSpin->setSuffix(uiText(" min", " 분"));
    m_autoSyncIntervalSpin->setSpecialValueText(uiText("Disabled", "사용 안 함"));
    m_autoSyncIntervalSpin->setValue(AppSettings::autoSyncIntervalMinutes());
    m_autoSyncIntervalSpin->setFixedWidth(94);

    m_closeOnFocusLossCheck = new QCheckBox(uiText("Close on focus loss", "포커스 해제 시 닫기"), this);
    m_closeOnFocusLossCheck->setChecked(AppSettings::closeOnFocusLoss());

    m_credentialSessionStorageCheck = new QCheckBox(
        uiText("Secure session storage", "보안 저장소에 세션 저장"),
        this);
    m_credentialSessionStorageCheck->setChecked(AppSettings::credentialSessionStorageEnabled());

    m_startOnLoginCheck = new QCheckBox(uiText("Start with Windows", "Windows 자동 시작"), this);
    m_startOnLoginCheck->setChecked(AppSettings::startOnLoginEnabled());

    m_bwPathEdit = new QLineEdit(AppSettings::bwProgramOverride(), this);
    m_bwPathEdit->setMinimumWidth(120);
    m_bwPathEdit->setPlaceholderText(
        uiText("Auto: app folder bw.exe, then PATH bw", "자동: 앱 폴더의 bw.exe, 그 다음 PATH의 bw"));

    auto* browseButton = new QPushButton(uiText("Browse...", "찾기..."), this);
    auto* autoButton = new QPushButton(uiText("Auto", "자동"), this);

    auto* bwLayout = new QHBoxLayout;
    bwLayout->setContentsMargins(0, 0, 0, 0);
    bwLayout->addWidget(m_bwPathEdit, 1);
    bwLayout->addWidget(browseButton);
    bwLayout->addWidget(autoButton);

    auto* sizeLayout = new QHBoxLayout;
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(8);
    sizeLayout->addWidget(m_widthSpin);
    sizeLayout->addWidget(m_heightSpin);
    sizeLayout->addStretch(1);

    auto* searchDisplayLayout = new QGridLayout;
    searchDisplayLayout->setContentsMargins(0, 0, 0, 0);
    searchDisplayLayout->setHorizontalSpacing(10);
    searchDisplayLayout->setVerticalSpacing(4);
    searchDisplayLayout->addWidget(m_resultLimitSpin, 0, 0);
    searchDisplayLayout->addWidget(m_showShortcutHintsCheck, 0, 1);
    searchDisplayLayout->addWidget(m_showResultIconsCheck, 1, 0);
    searchDisplayLayout->addWidget(m_fetchFaviconsFromWebCheck, 1, 1);
    searchDisplayLayout->setColumnStretch(1, 1);

    auto* appearanceLayout = new QHBoxLayout;
    appearanceLayout->setContentsMargins(0, 0, 0, 0);
    appearanceLayout->setSpacing(8);
    appearanceLayout->addLayout(opacityLayout, 1);
    appearanceLayout->addWidget(m_cornerCombo);
    appearanceLayout->addWidget(m_backgroundColorButton);

    auto* behaviorLayout = new QGridLayout;
    behaviorLayout->setContentsMargins(0, 0, 0, 0);
    behaviorLayout->setHorizontalSpacing(8);
    behaviorLayout->setVerticalSpacing(4);
    behaviorLayout->addWidget(new QLabel(uiText("Keep search", "검색 유지"), this), 0, 0);
    behaviorLayout->addWidget(m_reopenRetentionSpin, 0, 1);
    behaviorLayout->addWidget(new QLabel(uiText("Auto sync", "자동 동기화"), this), 0, 2);
    behaviorLayout->addWidget(m_autoSyncIntervalSpin, 0, 3);
    behaviorLayout->addWidget(m_closeOnFocusLossCheck, 1, 0, 1, 4);
    behaviorLayout->setColumnStretch(4, 1);

    auto* securityLayout = new QVBoxLayout;
    securityLayout->setContentsMargins(0, 0, 0, 0);
    securityLayout->setSpacing(4);
    securityLayout->addWidget(m_credentialSessionStorageCheck);
    securityLayout->addWidget(m_startOnLoginCheck);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->addRow(uiText("Language", "언어"), m_languageCombo);
    form->addRow(uiText("Quick access hotkey", "빠른 액세스 단축키"), m_hotkeyEdit);
    form->addRow(uiText("Shortcut set", "단축키 세트"), m_shortcutPresetCombo);
    form->addRow(uiText("Window size", "창 크기"), sizeLayout);
    form->addRow(uiText("Search/display", "검색/표시"), searchDisplayLayout);
    form->addRow(uiText("Result row fields", "결과 행 표시"), resultFieldsLayout);
    form->addRow(uiText("Quick window font", "빠른 창 폰트"), fontLayout);
    form->addRow(uiText("Window position", "창 위치"), positionLayout);
    form->addRow(uiText("Appearance", "모양"), appearanceLayout);
    form->addRow(uiText("Behavior", "동작"), behaviorLayout);
    form->addRow(uiText("Security/startup", "보안/시작"), securityLayout);
    form->addRow(uiText("bw.exe location", "bw.exe 위치"), bwLayout);

    auto* note = new QLabel(
        uiText("Leave bw.exe empty to use bw.exe beside PeekWarden first, then bw from PATH. Session storage does not store the master password.",
               "비워두면 PeekWarden 실행 파일 옆의 bw.exe를 먼저 사용하고, 없으면 PATH의 bw를 사용합니다. 세션 저장은 마스터 비밀번호를 저장하지 않습니다."),
        this);
    note->setWordWrap(true);
    note->setObjectName("settingsNote");

    auto* cliDocs = new QLabel(
        QString("%1 <a href=\"https://bitwarden.com/help/cli/\">https://bitwarden.com/help/cli/</a>")
            .arg(uiText("Bitwarden CLI download and setup:", "Bitwarden CLI 다운로드 및 설정:")),
        this);
    cliDocs->setObjectName("settingsLink");
    cliDocs->setTextFormat(Qt::RichText);
    cliDocs->setTextInteractionFlags(Qt::TextBrowserInteraction);
    cliDocs->setOpenExternalLinks(true);
    cliDocs->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto* formColumn = new QVBoxLayout;
    formColumn->setContentsMargins(0, 0, 0, 0);
    formColumn->setSpacing(9);
    formColumn->addLayout(form);
    formColumn->addWidget(note);
    formColumn->addWidget(cliDocs);
    formColumn->addStretch(1);

    auto* shortcutPanel = new QWidget(this);
    shortcutPanel->setObjectName("settingsShortcutPanel");
    shortcutPanel->setFixedWidth(188);
    auto* shortcutColumn = new QVBoxLayout(shortcutPanel);
    shortcutColumn->setContentsMargins(12, 12, 12, 12);
    shortcutColumn->setSpacing(8);
    shortcutColumn->addWidget(shortcutsTitle);
    shortcutColumn->addWidget(m_shortcutsLabel);
    shortcutColumn->addStretch(1);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addLayout(formColumn, 1);
    contentLayout->addWidget(shortcutPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    layout->addLayout(contentLayout);
    layout->addWidget(buttons);

    setStyleSheet(R"(
        QDialog {
            background: #3b2b40;
            color: #f0ecf5;
        }
        QLabel {
            color: #f0ecf5;
        }
        #settingsNote {
            color: #c9becf;
            font-size: 12px;
        }
        #settingsLink {
            color: #c9becf;
            font-size: 12px;
        }
        #settingsLink a {
            color: #7db7ff;
            text-decoration: none;
        }
        #settingsShortcutPanel {
            background: #21192a;
            border: 1px solid #594d64;
            border-radius: 8px;
        }
        #settingsSectionTitle {
            color: #ffffff;
            font-size: 13px;
            font-weight: 800;
        }
        #settingsShortcutText {
            color: #d8cedf;
            font-size: 11px;
            line-height: 140%;
        }
        QLineEdit, QSpinBox, QKeySequenceEdit, QComboBox {
            background: #191326;
            color: #f7f3ff;
            border: 1px solid #5f5270;
            border-radius: 6px;
            padding: 5px 8px;
            selection-background-color: #1f8cff;
            selection-color: #ffffff;
        }
        QCheckBox {
            color: #f0ecf5;
        }
        QPushButton {
            background: #5a5064;
            color: #f7f3ff;
            border: 1px solid #72667d;
            border-radius: 6px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background: #6a5e75;
        }
        QPushButton:pressed {
            background: #4d4456;
        }
    )");

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this] {
        updateOpacityLabel();
    });

    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this] {
        applyRecommendedFontForLanguage(m_languageCombo->currentData().toString());
        updateShortcutGuide();
    });

    connect(m_customPositionCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_positionXSpin->setEnabled(enabled);
        m_positionYSpin->setEnabled(enabled);
    });
    m_positionXSpin->setEnabled(m_customPositionCheck->isChecked());
    m_positionYSpin->setEnabled(m_customPositionCheck->isChecked());

    connect(m_showResultIconsCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        m_fetchFaviconsFromWebCheck->setEnabled(enabled);
    });
    m_fetchFaviconsFromWebCheck->setEnabled(m_showResultIconsCheck->isChecked());

    connect(m_hotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this, [this] {
        updateShortcutGuide();
    });

    connect(m_shortcutPresetCombo, &QComboBox::currentIndexChanged, this, [this] {
        updateShortcutGuide();
    });

    connect(m_backgroundColorButton, &QPushButton::clicked, this, [this] {
        chooseBackgroundColor();
    });

    connect(browseButton, &QPushButton::clicked, this, [this] {
        chooseBwProgram();
    });

    connect(autoButton, &QPushButton::clicked, this, [this] {
        m_bwPathEdit->clear();
    });

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveAndAccept();
    });

    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        reject();
    });

    updateShortcutGuide();
}

void SettingsDialog::chooseBwProgram()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        uiText("Select Bitwarden CLI", "Bitwarden CLI 선택"),
        m_bwPathEdit->text(),
        uiText("Bitwarden CLI (bw.exe bw);;All files (*)", "Bitwarden CLI (bw.exe bw);;모든 파일 (*)"));

    if (!path.isEmpty())
        m_bwPathEdit->setText(path);
}

void SettingsDialog::chooseBackgroundColor()
{
    const QColor current(AppSettings::windowBackgroundColor());
    const QColor selected = QColorDialog::getColor(current, this, uiText("Background color", "배경 색상"));
    if (!selected.isValid())
        return;

    m_backgroundColorButton->setProperty("selectedColor", selected.name(QColor::HexRgb));
    updateBackgroundColorButton();
}

void SettingsDialog::saveAndAccept()
{
    if (m_hotkeyEdit->keySequence().isEmpty()) {
        QMessageBox::warning(
            this,
            uiText("Hotkey", "단축키"),
            uiText("Choose a quick access hotkey first.", "빠른 액세스 단축키를 먼저 선택하세요."));
        return;
    }

    AppSettings::setLanguage(m_languageCombo->currentData().toString());
    AppSettings::setShowHotkey(m_hotkeyEdit->keySequence());
    AppSettings::setShortcutPreset(m_shortcutPresetCombo->currentData().toString());
    AppSettings::setWindowWidth(m_widthSpin->value());
    AppSettings::setWindowHeight(m_heightSpin->value());
    AppSettings::setSearchResultLimit(m_resultLimitSpin->value());
    AppSettings::setShowShortcutHints(m_showShortcutHintsCheck->isChecked());
    AppSettings::setShowResultIcons(m_showResultIconsCheck->isChecked());
    AppSettings::setFetchFaviconsFromWeb(m_fetchFaviconsFromWebCheck->isChecked());
    AppSettings::setShowResultUsername(m_showResultUsernameCheck->isChecked());
    AppSettings::setShowResultUri(m_showResultUriCheck->isChecked());
    AppSettings::setShowResultType(m_showResultTypeCheck->isChecked());
    AppSettings::setQuickFontFamily(m_fontCombo->currentFont().family());
    AppSettings::setQuickFontSize(m_fontSizeSpin->value());
    AppSettings::setQuickFontBold(m_fontBoldCheck->isChecked());
    AppSettings::setCustomWindowPositionEnabled(m_customPositionCheck->isChecked());
    AppSettings::setWindowPositionX(m_positionXSpin->value());
    AppSettings::setWindowPositionY(m_positionYSpin->value());
    AppSettings::setWindowOpacity(m_opacitySlider->value() / 100.0);
    AppSettings::setRoundedCorners(m_cornerCombo->currentData().toBool());
    AppSettings::setWindowBackgroundColor(m_backgroundColorButton->property("selectedColor").toString());
    AppSettings::setReopenRetentionSeconds(m_reopenRetentionSpin->value());
    AppSettings::setAutoSyncIntervalMinutes(m_autoSyncIntervalSpin->value());
    AppSettings::setCloseOnFocusLoss(m_closeOnFocusLossCheck->isChecked());
    AppSettings::setCredentialSessionStorageEnabled(m_credentialSessionStorageCheck->isChecked());
    AppSettings::setStartOnLoginEnabled(m_startOnLoginCheck->isChecked());
    AppSettings::setBwProgramOverride(m_bwPathEdit->text());
    accept();
}

void SettingsDialog::updateBackgroundColorButton()
{
    QString color = m_backgroundColorButton->property("selectedColor").toString();
    if (color.isEmpty()) {
        color = AppSettings::windowBackgroundColor();
        m_backgroundColorButton->setProperty("selectedColor", color);
    }

    m_backgroundColorButton->setProperty("selectedColor", color);
    m_backgroundColorButton->setText(color);
    m_backgroundColorButton->setStyleSheet(QString(
        "QPushButton { background: %1; color: #f7f3ff; border: 1px solid #72667d; border-radius: 6px; padding: 6px 10px; }"
        "QPushButton:hover { border-color: #8d7d99; }")
        .arg(color));
}

void SettingsDialog::updateOpacityLabel()
{
    m_opacityValue->setText(QString("%1%").arg(m_opacitySlider->value()));
}

void SettingsDialog::applyRecommendedFontForLanguage(const QString& languageCode)
{
    if (!m_fontCombo || !m_fontSizeSpin || !m_fontBoldCheck)
        return;

    m_fontCombo->setCurrentFont(QFont(recommendedFontFamily(languageCode)));
    m_fontSizeSpin->setValue(recommendedFontSize(languageCode));
    m_fontBoldCheck->setChecked(true);
}

void SettingsDialog::updateShortcutGuide()
{
    if (!m_shortcutsLabel)
        return;

    const QString hotkey = m_hotkeyEdit->keySequence().isEmpty()
        ? uiText("Not set", "미지정")
        : m_hotkeyEdit->keySequence().toString(QKeySequence::NativeText);

    const bool bitwardenStyle = m_shortcutPresetCombo
        && m_shortcutPresetCombo->currentData().toString() == QStringLiteral("bitwarden");
    const QString guide = bitwardenStyle
        ? uiText("Shortcut set: Bitwarden\nQuick access: %1\nEnter: Copy password\nCtrl+Enter: Copy username\nAlt+T: Copy TOTP\nCtrl+Shift+O: Details\nCtrl+R: Sync\nCtrl+/: Settings\nEsc: Close",
                 "단축키 세트: Bitwarden\n빠른 액세스: %1\nEnter: 비밀번호 복사\nCtrl+Enter: 사용자 이름 복사\nAlt+T: TOTP 복사\nCtrl+Shift+O: 상세보기\nCtrl+R: 동기화\nCtrl+/: 설정\nEsc: 닫기")
        : uiText("Shortcut set: 1Password\nQuick access: %1\nCtrl+C: Copy username\nCtrl+Shift+C: Copy password\nCtrl+Alt+C: Copy TOTP\nCtrl+Shift+O: Details\nCtrl+R: Sync\nCtrl+/: Settings\nEsc: Close",
                 "단축키 세트: 1Password\n빠른 액세스: %1\nCtrl+C: 사용자 이름 복사\nCtrl+Shift+C: 비밀번호 복사\nCtrl+Alt+C: TOTP 복사\nCtrl+Shift+O: 상세보기\nCtrl+R: 동기화\nCtrl+/: 설정\nEsc: 닫기");

    m_shortcutsLabel->setText(guide.arg(hotkey));
}
