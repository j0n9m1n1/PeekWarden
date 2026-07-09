#pragma once

#include <QAbstractNativeEventFilter>
#include <QString>

class QuickWindow;

class WinHotkey : public QAbstractNativeEventFilter
{
public:
    explicit WinHotkey(QuickWindow* window);
    ~WinHotkey() override;

    bool registerHotkey();
    void unregisterHotkey();

    QString lastError() const;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QuickWindow* m_window = nullptr;
    bool m_registered = false;
    QString m_lastError;
};
