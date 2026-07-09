#pragma once

#include <QDialog>

class QLabel;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    bool logoutRequested() const;

private:
    void chooseBwProgram();
    void chooseBackgroundColor();
    void requestLogout();
    void saveAndAccept();
    void updateBackgroundColorButton();
    void updateOpacityLabel();
    void updateShortcutGuide();
    void applyRecommendedFontForLanguage(const QString& languageCode);

    QComboBox* m_languageCombo = nullptr;
    QKeySequenceEdit* m_hotkeyEdit = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QSpinBox* m_resultLimitSpin = nullptr;
    QCheckBox* m_showShortcutHintsCheck = nullptr;
    QComboBox* m_shortcutPresetCombo = nullptr;
    QCheckBox* m_showResultIconsCheck = nullptr;
    QCheckBox* m_fetchFaviconsFromWebCheck = nullptr;
    QCheckBox* m_showResultUsernameCheck = nullptr;
    QCheckBox* m_showResultUriCheck = nullptr;
    QCheckBox* m_showResultTypeCheck = nullptr;
    QFontComboBox* m_fontCombo = nullptr;
    QSpinBox* m_fontSizeSpin = nullptr;
    QCheckBox* m_fontBoldCheck = nullptr;
    QCheckBox* m_customPositionCheck = nullptr;
    QSpinBox* m_positionXSpin = nullptr;
    QSpinBox* m_positionYSpin = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityValue = nullptr;
    QComboBox* m_cornerCombo = nullptr;
    QPushButton* m_backgroundColorButton = nullptr;
    QLabel* m_shortcutsLabel = nullptr;
    QSpinBox* m_reopenRetentionSpin = nullptr;
    QSpinBox* m_autoSyncIntervalSpin = nullptr;
    QCheckBox* m_closeOnFocusLossCheck = nullptr;
    QCheckBox* m_credentialSessionStorageCheck = nullptr;
    QCheckBox* m_startOnLoginCheck = nullptr;
    QPushButton* m_logoutButton = nullptr;
    QLineEdit* m_bwPathEdit = nullptr;
    bool m_logoutRequested = false;
};
