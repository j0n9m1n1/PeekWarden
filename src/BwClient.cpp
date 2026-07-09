#include "BwClient.h"

#include "AppSettings.h"
#include "I18n.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineEdit>
#include <QLabel>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace
{
#ifdef Q_OS_WIN
constexpr wchar_t CredentialTargetName[] = L"PeekWarden.BitwardenSession";
#endif

void setError(QString* target, const QString& message)
{
    if (target)
        *target = message;
}

QString trimmedUtf8(const QByteArray& data)
{
    return QString::fromUtf8(data).trimmed();
}

QStringList jsonStringList(const QJsonArray& array)
{
    QStringList values;
    for (const QJsonValue& value : array) {
        if (value.isString())
            values.push_back(value.toString());
    }
    return values;
}

QString itemTypeName(int type)
{
    switch (type) {
    case 1:
        return I18n::translate("BwClient", "Login", "로그인");
    case 2:
        return I18n::translate("BwClient", "Secure note", "보안 메모");
    case 3:
        return I18n::translate("BwClient", "Card", "카드");
    case 4:
        return I18n::translate("BwClient", "Identity", "신원");
    case 5:
        return I18n::translate("BwClient", "SSH key", "SSH 키");
    default:
        return QString::number(type);
    }
}

QJsonObject redactedItemObject(const QJsonObject& object)
{
    QJsonObject redactedObject = object;
    QJsonObject redactedLogin = redactedObject.value("login").toObject();
    if (!redactedLogin.value("password").toString().isEmpty())
        redactedLogin.insert("password", "[redacted]");
    if (!redactedLogin.value("totp").toString().isEmpty())
        redactedLogin.insert("totp", "[redacted]");
    if (!redactedLogin.isEmpty())
        redactedObject.insert("login", redactedLogin);
    return redactedObject;
}

QString redactedJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(redactedItemObject(object)).toJson(QJsonDocument::Indented)).trimmed();
}

QStringList loginUris(const QJsonObject& login)
{
    QStringList uris;
    for (const QJsonValue& uriValue : login.value("uris").toArray()) {
        const QJsonObject uriObject = uriValue.toObject();
        const QString uri = uriObject.value("uri").toString();
        if (!uri.isEmpty())
            uris.push_back(uri);
    }
    return uris;
}

VaultItem vaultItemFromObject(const QJsonObject& object)
{
    const QJsonObject login = object.value("login").toObject();

    VaultItem item;
    item.id = object.value("id").toString();
    item.name = object.value("name").toString();
    item.typeCode = object.value("type").toInt();
    item.type = itemTypeName(item.typeCode);
    item.username = login.value("username").toString();
    item.password = login.value("password").toString();
    item.totp = login.value("totp").toString();
    item.notes = object.value("notes").toString();
    item.folderId = object.value("folderId").toString();
    item.organizationId = object.value("organizationId").toString();
    item.collectionIds = jsonStringList(object.value("collectionIds").toArray());
    item.revisionDate = object.value("revisionDate").toString();
    item.creationDate = object.value("creationDate").toString();
    item.deletedDate = object.value("deletedDate").toString();
    item.uris = loginUris(login);
    item.rawJson = redactedJson(object);
    return item;
}

#ifdef Q_OS_WIN
QString readStoredSession()
{
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(CredentialTargetName, CRED_TYPE_GENERIC, 0, &credential))
        return {};

    QByteArray data(reinterpret_cast<const char*>(credential->CredentialBlob),
        static_cast<int>(credential->CredentialBlobSize));
    CredFree(credential);
    return QString::fromUtf8(data).trimmed();
}

void writeStoredSession(const QString& session)
{
    const QByteArray data = session.toUtf8();
    if (data.isEmpty())
        return;

    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(CredentialTargetName);
    credential.CredentialBlobSize = static_cast<DWORD>(data.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(data.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"PeekWarden");
    CredWriteW(&credential, 0);
}

void deleteStoredSession()
{
    CredDeleteW(CredentialTargetName, CRED_TYPE_GENERIC, 0);
}
#else
QString readStoredSession()
{
    return {};
}

void writeStoredSession(const QString&)
{
}

void deleteStoredSession()
{
}
#endif

QString stripTerminalControls(const QString& value)
{
    QString output;
    output.reserve(value.size());

    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar ch = value.at(index);

        if (ch.unicode() == 0x1b && index + 1 < value.size() && value.at(index + 1) == QLatin1Char('[')) {
            index += 2;
            while (index < value.size()) {
                const ushort code = value.at(index).unicode();
                if (code >= 0x40 && code <= 0x7e)
                    break;
                ++index;
            }
            continue;
        }

        if (ch.unicode() == 0x9b) {
            while (index < value.size()) {
                const ushort code = value.at(index).unicode();
                if (code >= 0x40 && code <= 0x7e)
                    break;
                ++index;
            }
            continue;
        }

        if ((ch.unicode() == 0xfffd || ch.unicode() == 0x25a1) && index + 1 < value.size()
            && value.at(index + 1) == QLatin1Char('[')) {
            ++index;
            while (index < value.size()) {
                const ushort code = value.at(index).unicode();
                if (code >= 0x40 && code <= 0x7e)
                    break;
                ++index;
            }
            continue;
        }

        if (ch == QLatin1Char('\r')) {
            output.append(QLatin1Char('\n'));
            continue;
        }

        if (ch.unicode() < 0x20 && ch != QLatin1Char('\n') && ch != QLatin1Char('\t'))
            continue;

        output.append(ch);
    }

    return output;
}

QString readableCliText(const QByteArray& data)
{
    QString text = stripTerminalControls(QString::fromUtf8(data));
    text.remove(QRegularExpression(
        QStringLiteral("\\?\\s*Master password:\\s*\\[(?:input is hidden|hidden)\\]\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression(QStringLiteral("[ \\t]+\\n")), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));

    QStringList cleanedLines;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (!cleanedLines.isEmpty() && cleanedLines.constLast() == trimmed)
            continue;
        cleanedLines.push_back(trimmed);
    }

    return cleanedLines.join(QLatin1Char('\n')).trimmed();
}

QString uiText(const char* ko, const char* en)
{
    return I18n::translate("BwClient", en, ko);
}

QString loginDialogStyle()
{
    if (AppSettings::theme() == QStringLiteral("light")) {
        return QStringLiteral(R"(
            QDialog {
                background: #f8f9fa;
                color: #24292f;
            }
            QLabel {
                color: #24292f;
            }
            QLineEdit, QComboBox {
                background: #ffffff;
                color: #24292f;
                border: 1px solid #d0d7de;
                border-radius: 6px;
                padding: 6px 8px;
                selection-background-color: #0969da;
                selection-color: #ffffff;
                min-height: 24px;
            }
            QPushButton {
                background: #f3f4f6;
                color: #24292f;
                border: 1px solid #d0d7de;
                border-radius: 6px;
                padding: 6px 10px;
            }
            QPushButton:hover {
                background: #eaeef2;
            }
        )");
    }

    return QStringLiteral(R"(
        QDialog {
            background: #202124;
            color: #e8eaed;
        }
        QLabel {
            color: #e8eaed;
        }
        QLineEdit, QComboBox {
            background: #111317;
            color: #f1f3f4;
            border: 1px solid #3a3f46;
            border-radius: 6px;
            padding: 6px 8px;
            selection-background-color: #1f8cff;
            selection-color: #ffffff;
            min-height: 24px;
        }
        QPushButton {
            background: #2f343a;
            color: #f1f3f4;
            border: 1px solid #4b5563;
            border-radius: 6px;
            padding: 6px 10px;
        }
        QPushButton:hover {
            background: #3a4048;
        }
    )");
}

}

bool BwClient::CommandResult::ok() const
{
    return started && !timedOut && !crashed && exitCode == 0;
}

BwClient::BwClient()
    : m_program(findBwProgram())
{
    if (AppSettings::credentialSessionStorageEnabled())
        m_session = readStoredSession();
}

bool BwClient::hasSession() const
{
    return !m_session.isEmpty();
}

QString BwClient::program() const
{
    return m_program;
}

void BwClient::reloadSettings()
{
    m_program = findBwProgram();

    if (AppSettings::credentialSessionStorageEnabled()) {
        if (m_session.isEmpty())
            m_session = readStoredSession();
        else
            writeStoredSession(m_session);
    } else {
        deleteStoredSession();
    }
}

void BwClient::clearSession()
{
    m_session.clear();
    deleteStoredSession();
}

bool BwClient::isAvailable(QString* errorMessage) const
{
    const QStringList arguments = {"--version"};
    const CommandResult result = run(arguments, {}, 3000);

    if (result.ok()) {
        setError(errorMessage, {});
        return true;
    }

    setError(errorMessage,
        QString("%1\n\n%2")
            .arg(uiText("Bitwarden CLI를 찾지 못했거나 실행하지 못했습니다.",
                         "Bitwarden CLI was not found or could not run."),
                describeCommandError(m_program, arguments, result)));
    return false;
}

BwClient::StatusInfo BwClient::statusInfo(QString* errorMessage) const
{
    StatusInfo info;
    const QStringList arguments = {"status"};
    const CommandResult result = run(arguments, {}, 5000);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return info;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, uiText("Bitwarden 상태를 해석하지 못했습니다: %1",
                                      "Could not parse Bitwarden status: %1")
                .arg(parseError.errorString()));
        return info;
    }

    const QJsonObject object = document.object();
    const QString value = object.value("status").toString().toLower();
    info.userEmail = object.value("userEmail").toString();
    info.serverUrl = object.value("serverUrl").toString();
    setError(errorMessage, {});

    if (value == "unauthenticated")
        info.status = VaultStatus::Unauthenticated;
    else if (value == "locked")
        info.status = VaultStatus::Locked;
    else if (value == "unlocked")
        info.status = VaultStatus::Unlocked;
    else
        setError(errorMessage, uiText("알 수 없는 Bitwarden 상태입니다: %1",
                                      "Unknown Bitwarden status: %1")
                .arg(value));

    return info;
}

BwClient::VaultStatus BwClient::status(QString* errorMessage) const
{
    return statusInfo(errorMessage).status;
}

bool BwClient::configureServer(const QString& serverUrl, QString* errorMessage)
{
    const QString trimmedServerUrl = serverUrl.trimmed();
    if (trimmedServerUrl.isEmpty()) {
        setError(errorMessage, uiText("Bitwarden 서버 주소를 선택하세요.",
                                      "Choose a Bitwarden server."));
        return false;
    }

    const QStringList arguments = {"config", "server", trimmedServerUrl};
    const CommandResult result = run(arguments, {}, 10000);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return false;
    }

    setError(errorMessage, {});
    return true;
}

bool BwClient::ensureSession(QWidget* parent, QString* errorMessage)
{
    if (!isAvailable(errorMessage))
        return false;

    const VaultStatus currentStatus = status(errorMessage);

    switch (currentStatus) {
    case VaultStatus::Unauthenticated:
        return login(parent, errorMessage);
    case VaultStatus::Locked:
        return unlock(parent, errorMessage);
    case VaultStatus::Unlocked:
        setError(errorMessage, {});
        return true;
    case VaultStatus::Unknown:
        return false;
    }

    return false;
}

bool BwClient::loginWithCredentials(const QString& email,
    const QString& password,
    const QString& method,
    const QString& code,
    QString* errorMessage)
{
    const QString trimmedEmail = email.trimmed();
    QString passwordCopy = password;
    const QString trimmedCode = code.trimmed();
    const QString trimmedMethod = method.trimmed();

    if (trimmedEmail.isEmpty() || passwordCopy.isEmpty()) {
        passwordCopy.fill(QChar('\0'));
        setError(errorMessage, uiText("이메일과 마스터 비밀번호를 입력하세요.",
                                      "Enter your email and master password."));
        return false;
    }

    QStringList arguments = {"login", trimmedEmail, passwordCopy, "--raw", "--nointeraction"};
    QString displayCommand = QString("login %1 <password> --raw --nointeraction").arg(trimmedEmail);

    if (!trimmedCode.isEmpty()) {
        if (trimmedMethod.isEmpty()) {
            passwordCopy.fill(QChar('\0'));
            setError(errorMessage, uiText("2단계 코드를 입력한 경우 2단계 방식도 선택하세요.",
                                          "Choose a two-step method when entering a two-step code."));
            return false;
        }

        arguments << "--method" << trimmedMethod << "--code" << trimmedCode;
        displayCommand += " --method " + trimmedMethod + " --code <code>";
    }

    const CommandResult result = run(arguments, {}, 60000);
    arguments[2].fill(QChar('\0'));
    passwordCopy.fill(QChar('\0'));

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, displayCommand, result));
        return false;
    }

    m_session = trimmedUtf8(result.standardOutput);
    if (m_session.isEmpty()) {
        setError(errorMessage, uiText("Bitwarden CLI가 세션 키를 반환하지 않았습니다.",
                                      "Bitwarden CLI did not return a session key."));
        return false;
    }

    if (AppSettings::credentialSessionStorageEnabled())
        writeStoredSession(m_session);

    setError(errorMessage, {});
    return true;
}

bool BwClient::unlockWithPassword(const QString& password, QString* errorMessage)
{
    QString passwordCopy = password;
    if (passwordCopy.isEmpty()) {
        setError(errorMessage, {});
        return false;
    }

    QByteArray passwordInput = passwordCopy.toUtf8();
    passwordCopy.fill(QChar('\0'));

    QProcessEnvironment extraEnvironment;
    extraEnvironment.insert("BW_PASSWORD", QString::fromUtf8(passwordInput));

    const QStringList arguments = {"unlock", "--passwordenv", "BW_PASSWORD", "--raw", "--nointeraction"};
    const CommandResult result = run(arguments, {}, 30000, extraEnvironment);
    extraEnvironment.insert("BW_PASSWORD", QString());
    passwordInput.fill('\0');

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return false;
    }

    m_session = trimmedUtf8(result.standardOutput);
    if (m_session.isEmpty()) {
        setError(errorMessage, uiText("Bitwarden CLI가 세션 키를 반환하지 않았습니다.",
                                      "Bitwarden CLI did not return a session key."));
        return false;
    }

    if (AppSettings::credentialSessionStorageEnabled())
        writeStoredSession(m_session);

    setError(errorMessage, {});
    return true;
}

bool BwClient::login(QWidget* parent, QString* errorMessage)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(uiText("Bitwarden 로그인", "Bitwarden Login"));
    dialog.setModal(true);
    dialog.resize(460, 250);
    dialog.setMinimumWidth(460);
    dialog.setStyleSheet(loginDialogStyle());

    auto* emailEdit = new QLineEdit(&dialog);
    emailEdit->setPlaceholderText("name@example.com");
    emailEdit->setMinimumWidth(260);

    auto* passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setMinimumWidth(260);

    auto* codeEdit = new QLineEdit(&dialog);
    codeEdit->setPlaceholderText(uiText("필요한 경우에만 입력", "Only if required"));
    codeEdit->setMinimumWidth(260);

    auto* methodCombo = new QComboBox(&dialog);
    methodCombo->addItem(uiText("없음", "None"), QString());
    methodCombo->addItem(uiText("인증 앱", "Authenticator app"), "0");
    methodCombo->addItem(uiText("이메일", "Email"), "1");
    methodCombo->addItem(uiText("YubiKey OTP", "YubiKey OTP"), "3");

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    form->addRow(uiText("이메일", "Email"), emailEdit);
    form->addRow(uiText("마스터 비밀번호", "Master password"), passwordEdit);
    form->addRow(uiText("2단계 방식", "Two-step method"), methodCombo);
    form->addRow(uiText("2단계 코드", "Two-step code"), codeEdit);

    auto* note = new QLabel(
        uiText("처음 사용하는 경우 Bitwarden CLI 계정 로그인이 먼저 필요합니다.",
               "First use requires logging in to your Bitwarden CLI account."),
        &dialog);
    note->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 14);
    layout->setSpacing(12);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        setError(errorMessage, {});
        return false;
    }

    const QString email = emailEdit->text().trimmed();
    QString password = passwordEdit->text();
    const QString code = codeEdit->text().trimmed();
    const QString method = methodCombo->currentData().toString();

    if (email.isEmpty() || password.isEmpty()) {
        password.fill(QChar('\0'));
        setError(errorMessage, uiText("이메일과 마스터 비밀번호를 입력하세요.",
                                      "Enter your email and master password."));
        return false;
    }

    QStringList arguments = {"login", email, password, "--raw", "--nointeraction"};
    QString displayCommand = QString("login %1 <password> --raw --nointeraction").arg(email);

    if (!code.isEmpty()) {
        if (method.isEmpty()) {
            password.fill(QChar('\0'));
            setError(errorMessage, uiText("2단계 코드를 입력한 경우 2단계 방식도 선택하세요.",
                                          "Choose a two-step method when entering a two-step code."));
            return false;
        }

        arguments << "--method" << method << "--code" << code;
        displayCommand += " --method " + method + " --code <code>";
    }

    const CommandResult result = run(arguments, {}, 60000);
    arguments[2].fill(QChar('\0'));
    password.fill(QChar('\0'));

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, displayCommand, result));
        return false;
    }

    m_session = trimmedUtf8(result.standardOutput);
    if (m_session.isEmpty()) {
        setError(errorMessage, uiText("Bitwarden CLI가 세션 키를 반환하지 않았습니다.",
                                      "Bitwarden CLI did not return a session key."));
        return false;
    }

    if (AppSettings::credentialSessionStorageEnabled())
        writeStoredSession(m_session);

    setError(errorMessage, {});
    return true;
}

bool BwClient::unlock(QWidget* parent, QString* errorMessage)
{
    bool accepted = false;
    QString password = QInputDialog::getText(
        parent,
        "Bitwarden Unlock",
        "Master password:",
        QLineEdit::Password,
        {},
        &accepted);

    if (!accepted || password.isEmpty()) {
        setError(errorMessage, {});
        return false;
    }

    QByteArray passwordInput = password.toUtf8();
    password.fill(QChar('\0'));

    QProcessEnvironment extraEnvironment;
    extraEnvironment.insert("BW_PASSWORD", QString::fromUtf8(passwordInput));

    const QStringList arguments = {"unlock", "--passwordenv", "BW_PASSWORD", "--raw", "--nointeraction"};
    const CommandResult result = run(arguments, {}, 30000, extraEnvironment);
    extraEnvironment.insert("BW_PASSWORD", QString());
    passwordInput.fill('\0');

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return false;
    }

    m_session = trimmedUtf8(result.standardOutput);
    if (m_session.isEmpty()) {
        setError(errorMessage, "Bitwarden CLI did not return a session key.");
        return false;
    }

    if (AppSettings::credentialSessionStorageEnabled())
        writeStoredSession(m_session);

    setError(errorMessage, {});
    return true;
}

bool BwClient::lock(QString* errorMessage)
{
    if (m_session.isEmpty()) {
        setError(errorMessage, {});
        return true;
    }

    const QStringList arguments = {"lock"};
    const CommandResult result = run(arguments);
    m_session.clear();
    deleteStoredSession();

    if (!result.ok()) {
        QString ignoredError;
        if (statusInfo(&ignoredError).status == VaultStatus::Unauthenticated) {
            setError(errorMessage, {});
            return true;
        }

        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return false;
    }

    setError(errorMessage, {});
    return true;
}

bool BwClient::logout(QString* errorMessage)
{
    const QStringList arguments = {"logout", "--nointeraction"};
    const CommandResult result = run(arguments);
    m_session.clear();
    deleteStoredSession();

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return false;
    }

    setError(errorMessage, {});
    return true;
}

bool BwClient::sync(QString* errorMessage) const
{
    const QStringList arguments = {"sync"};
    const CommandResult result = run(arguments, {}, 30000);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return false;
    }

    setError(errorMessage, {});
    return true;
}

QVector<VaultItem> BwClient::listItems(QString* errorMessage) const
{
    QVector<VaultItem> items;

    const QStringList arguments = {"list", "items"};
    const CommandResult result = run(arguments, {}, 20000);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return items;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(errorMessage, uiText("Bitwarden 검색 결과를 해석하지 못했습니다: %1",
                                      "Could not parse Bitwarden search results: %1")
                .arg(parseError.errorString()));
        return items;
    }

    for (const QJsonValue& value : document.array()) {
        const VaultItem item = vaultItemFromObject(value.toObject());

        if (!item.id.isEmpty())
            items.push_back(item);
    }

    setError(errorMessage, {});
    return items;
}

QVector<VaultItem> BwClient::search(const QString& keyword, QString* errorMessage) const
{
    QVector<VaultItem> items;
    const QString trimmedKeyword = keyword.trimmed();

    if (trimmedKeyword.isEmpty()) {
        setError(errorMessage, {});
        return items;
    }

    const QStringList arguments = {"list", "items", "--search", trimmedKeyword};
    const CommandResult result = run(arguments, {}, 20000);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return items;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        setError(errorMessage, uiText("Bitwarden 검색 결과를 해석하지 못했습니다: %1",
                                      "Could not parse Bitwarden search results: %1")
                .arg(parseError.errorString()));
        return items;
    }

    for (const QJsonValue& value : document.array()) {
        const VaultItem item = vaultItemFromObject(value.toObject());

        if (!item.id.isEmpty())
            items.push_back(item);
    }

    setError(errorMessage, {});
    return items;
}

QString BwClient::getPassword(const QString& itemId, QString* errorMessage) const
{
    const QStringList arguments = {"get", "password", itemId};
    const CommandResult result = run(arguments);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return {};
    }

    setError(errorMessage, {});
    return trimmedUtf8(result.standardOutput);
}

QString BwClient::getUsername(const QString& itemId, QString* errorMessage) const
{
    const QStringList arguments = {"get", "item", itemId};
    const CommandResult result = run(arguments);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, uiText("Bitwarden 항목을 해석하지 못했습니다: %1",
                                      "Could not parse Bitwarden item: %1")
                .arg(parseError.errorString()));
        return {};
    }

    const QJsonObject login = document.object().value("login").toObject();

    setError(errorMessage, {});
    return login.value("username").toString();
}

QString BwClient::getTotp(const QString& itemId, QString* errorMessage) const
{
    const QStringList arguments = {"get", "totp", itemId};
    const CommandResult result = run(arguments);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return {};
    }

    setError(errorMessage, {});
    return trimmedUtf8(result.standardOutput);
}

BwClient::ItemDetails BwClient::getItemDetails(const QString& itemId, QString* errorMessage) const
{
    ItemDetails details;
    const QStringList arguments = {"get", "item", itemId};
    const CommandResult result = run(arguments, {}, 15000);

    if (!result.ok()) {
        setError(errorMessage, describeCommandError(m_program, arguments, result));
        return details;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, uiText("Bitwarden 항목을 해석하지 못했습니다: %1",
                                      "Could not parse Bitwarden item: %1")
                .arg(parseError.errorString()));
        return details;
    }

    details = detailsFromVaultItem(vaultItemFromObject(document.object()));

    setError(errorMessage, {});
    return details;
}

BwClient::ItemDetails BwClient::detailsFromVaultItem(const VaultItem& item)
{
    ItemDetails details;
    details.id = item.id;
    details.name = item.name;
    details.type = item.type;
    details.username = item.username;
    details.password = item.password;
    details.totp = item.totp;
    details.notes = item.notes;
    details.folderId = item.folderId;
    details.organizationId = item.organizationId;
    details.collectionIds = item.collectionIds.join(QStringLiteral(", "));
    details.revisionDate = item.revisionDate;
    details.creationDate = item.creationDate;
    details.deletedDate = item.deletedDate;
    details.uris = item.uris;
    details.rawJson = item.rawJson;
    return details;
}

QString BwClient::findBwProgram()
{
    const QString configuredProgram = AppSettings::bwProgramOverride();
    if (!configuredProgram.isEmpty())
        return configuredProgram;

#ifdef Q_OS_WIN
    const QString localProgram = QDir(QCoreApplication::applicationDirPath()).filePath("bw.exe");
#else
    const QString localProgram = QDir(QCoreApplication::applicationDirPath()).filePath("bw");
#endif

    if (QFileInfo::exists(localProgram) && QFileInfo(localProgram).isFile())
        return localProgram;

    return "bw";
}

QString BwClient::describeCommandError(const QString& program,
    const QStringList& arguments,
    const CommandResult& result)
{
    return describeCommandError(program, arguments.join(' '), result);
}

QString BwClient::describeCommandError(const QString& program,
    const QString& displayCommand,
    const CommandResult& result)
{
    const QString command = displayCommand.isEmpty() ? program : program + " " + displayCommand;

    if (!result.started)
        return uiText("%1을(를) 시작하지 못했습니다: %2",
                      "Could not start %1: %2")
            .arg(command, result.errorString);

    if (result.timedOut)
        return uiText("%1 실행 시간이 초과되었습니다.",
                      "Timed out while running %1")
            .arg(command);

    if (result.crashed)
        return uiText("%1이(가) 비정상 종료되었습니다.",
                      "%1 crashed.")
            .arg(command);

    const QString stderrText = readableCliText(result.standardError);
    if (!stderrText.isEmpty())
        return stderrText;

    const QString stdoutText = readableCliText(result.standardOutput);
    if (!stdoutText.isEmpty())
        return stdoutText;

    return uiText("%1이(가) 종료 코드 %2로 실패했습니다.",
                  "%1 failed with exit code %2.")
        .arg(command)
        .arg(result.exitCode);
}

BwClient::CommandResult BwClient::run(const QStringList& arguments,
    const QByteArray& standardInput,
    int timeoutMs,
    const QProcessEnvironment& extraEnvironment) const
{
    CommandResult result;
    QProcess process;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!m_session.isEmpty())
        environment.insert("BW_SESSION", m_session);

    for (const QString& name : extraEnvironment.keys())
        environment.insert(name, extraEnvironment.value(name));

    process.setProcessEnvironment(environment);
    process.setProgram(m_program);
    process.setArguments(arguments);

    process.start();

    if (!process.waitForStarted(3000)) {
        result.errorString = process.errorString();
        return result;
    }

    result.started = true;

    if (!standardInput.isEmpty())
        process.write(standardInput);
    process.closeWriteChannel();

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(1000);
    }

    result.crashed = process.exitStatus() == QProcess::CrashExit;
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    result.errorString = process.errorString();

    return result;
}
