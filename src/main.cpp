#include "AppSettings.h"
#include "I18n.h"
#include "QuickWindow.h"
#include "WinHotkey.h"

#include <QApplication>
#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QString>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>

namespace
{
QString uiText(const char* ko, const char* en)
{
    return I18n::translate("Main", en, ko);
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("PeekWarden");
    QApplication::setOrganizationName("PeekWarden");
    QApplication::setQuitOnLastWindowClosed(false);
    AppSettings::applyStartupPreference();

    QTranslator appTranslator;
    QTranslator qtTranslator;
    const auto reloadTranslator = [&] {
        I18n::installTranslator(app, appTranslator, AppSettings::language());
        I18n::installQtTranslator(app, qtTranslator, AppSettings::language());
    };
    reloadTranslator();

    QuickWindow window;

    QMenu trayMenu;
    QAction* openAction = trayMenu.addAction(QString());
    QAction* lockAction = trayMenu.addAction(QString());
    trayMenu.addSeparator();
    QAction* settingsAction = trayMenu.addAction(QString());
    trayMenu.addSeparator();
    QAction* quitAction = trayMenu.addAction(QString());

    const auto updateTrayTexts = [&] {
        openAction->setText(uiText("열기", "Open"));
        lockAction->setText(uiText("잠금", "Lock"));
        settingsAction->setText(uiText("설정", "Settings"));
        quitAction->setText(uiText("종료", "Quit"));
    };
    updateTrayTexts();

    QSystemTrayIcon trayIcon;
    trayIcon.setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    trayIcon.setToolTip("PeekWarden");
    trayIcon.setContextMenu(&trayMenu);

    QObject::connect(openAction, &QAction::triggered, &window, [&window] {
        window.showQuick();
    });

    QObject::connect(lockAction, &QAction::triggered, &window, [&window, &trayIcon] {
        QString errorMessage;
        if (!window.lockVault(&errorMessage) && !errorMessage.isEmpty()) {
            QMessageBox::warning(&window, uiText("Bitwarden 잠금", "Bitwarden Lock"), errorMessage);
            return;
        }

        trayIcon.showMessage(
            "PeekWarden",
            uiText("Bitwarden vault를 잠갔습니다.", "Bitwarden vault locked."));
    });

    QObject::connect(settingsAction, &QAction::triggered, &window, [&window] {
        window.showSettings();
    });

    QObject::connect(quitAction, &QAction::triggered, &app, [&trayIcon] {
        trayIcon.hide();
        QApplication::quit();
    });

    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &window,
        [&window](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                window.showQuick();
        });

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon.show();
    } else {
        QMessageBox::warning(
            nullptr,
            uiText("시스템 트레이", "System Tray"),
            uiText("시스템 트레이를 사용할 수 없습니다. PeekWarden은 트레이 아이콘 없이 계속 실행됩니다.",
                   "System tray is not available. PeekWarden will keep running without a tray icon."));
    }

#ifdef Q_OS_WIN
    WinHotkey hotkey(&window);
    app.installNativeEventFilter(&hotkey);

    window.setSettingsChangedCallback([&] {
        reloadTranslator();
        updateTrayTexts();
        hotkey.unregisterHotkey();
        if (!hotkey.registerHotkey()) {
            QMessageBox::warning(
                &window,
                uiText("단축키", "Hotkey"),
                QString("%1\n\n%2\n\n%3")
                    .arg(uiText("설정된 단축키를 등록하지 못했습니다.",
                                "Failed to register the configured hotkey."),
                        hotkey.lastError(),
                        uiText("1Password 같은 다른 앱이 이미 이 단축키를 사용 중일 수 있습니다.",
                               "Another app, such as 1Password, may already be using this shortcut."))
            );
        }
    });

    if (!hotkey.registerHotkey()) {
        QMessageBox::warning(
            nullptr,
            uiText("단축키", "Hotkey"),
            QString("%1\n\n%2\n\n%3")
                .arg(uiText("설정된 단축키를 등록하지 못했습니다.",
                            "Failed to register the configured hotkey."),
                    hotkey.lastError(),
                    uiText("1Password 같은 다른 앱이 이미 이 단축키를 사용 중일 수 있습니다.",
                           "Another app, such as 1Password, may already be using this shortcut.")));
        trayIcon.hide();
        return 1;
    }
#else
    window.showQuick();
#endif

    if (trayIcon.isVisible()) {
        QTimer::singleShot(700, &trayIcon, [&trayIcon] {
            if (!trayIcon.isVisible())
                return;

            trayIcon.showMessage(
                "PeekWarden",
                uiText("PeekWarden이 실행 중입니다. %1 키로 빠른 액세스를 열 수 있습니다.",
                       "PeekWarden is running. Press %1 to open quick access.")
                    .arg(AppSettings::showHotkeyText()),
                QSystemTrayIcon::Information,
                3500);
        });
    }

    QTimer::singleShot(0, &window, [&window] {
        window.preloadVault();
    });

    return app.exec();
}
