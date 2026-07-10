#pragma once

#include <QByteArray>
#include <QMutex>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

struct VaultCardData
{
    QString cardholderName;
    QString brand;
    QString number;
    QString expirationMonth;
    QString expirationYear;
    QString securityCode;
};

struct VaultIdentityData
{
    QString title;
    QString firstName;
    QString middleName;
    QString lastName;
    QString company;
    QString email;
    QString phone;
    QString username;
    QString address1;
    QString address2;
    QString address3;
    QString city;
    QString state;
    QString postalCode;
    QString country;
    QString socialSecurityNumber;
    QString passportNumber;
    QString licenseNumber;
};

struct VaultSshKeyData
{
    QString privateKey;
    QString publicKey;
    QString fingerprint;
};

struct VaultCustomField
{
    QString name;
    QString value;
    int typeCode = 0;
};

struct VaultItem
{
    QString id;
    QString name;
    int typeCode = 0;
    QString type;
    QString username;
    QString password;
    QString totp;
    VaultCardData card;
    VaultIdentityData identity;
    VaultSshKeyData sshKey;
    QVector<VaultCustomField> customFields;
    QString notes;
    QString folderId;
    QString organizationId;
    QStringList collectionIds;
    QString revisionDate;
    QString creationDate;
    QString deletedDate;
    QStringList uris;
};

class BwClient
{
public:
    enum class VaultStatus
    {
        Unknown,
        Unauthenticated,
        Locked,
        Unlocked
    };

    struct StatusInfo
    {
        VaultStatus status = VaultStatus::Unknown;
        QString userEmail;
        QString serverUrl;
    };

    struct ItemDetails
    {
        QString id;
        QString name;
        int typeCode = 0;
        QString type;
        QString username;
        QString password;
        QString totp;
        VaultCardData card;
        VaultIdentityData identity;
        VaultSshKeyData sshKey;
        QVector<VaultCustomField> customFields;
        QString notes;
        QString folderId;
        QString organizationId;
        QString collectionIds;
        QString revisionDate;
        QString creationDate;
        QString deletedDate;
        QStringList uris;
    };

    BwClient();

    bool hasSession() const;
    QString program() const;
    void reloadSettings();
    void clearSession();

    bool isAvailable(QString* errorMessage = nullptr) const;
    StatusInfo statusInfo(QString* errorMessage = nullptr) const;
    VaultStatus status(QString* errorMessage = nullptr) const;
    bool configureServer(const QString& serverUrl, QString* errorMessage = nullptr);
    bool ensureSession(QWidget* parent, QString* errorMessage = nullptr);
    bool login(QWidget* parent, QString* errorMessage = nullptr);
    bool unlock(QWidget* parent, QString* errorMessage = nullptr);
    bool loginWithCredentials(const QString& serverUrl,
        const QString& email,
        const QString& password,
        const QString& method,
        const QString& code,
        QString* errorMessage = nullptr);
    bool unlockWithPassword(const QString& password, QString* errorMessage = nullptr);
    bool lock(QString* errorMessage = nullptr);
    bool logout(QString* errorMessage = nullptr);
    bool sync(QString* errorMessage = nullptr) const;

    QVector<VaultItem> listItems(QString* errorMessage = nullptr) const;
    QVector<VaultItem> search(const QString& keyword, QString* errorMessage = nullptr) const;
    QString getPassword(const QString& itemId, QString* errorMessage = nullptr) const;
    QString getUsername(const QString& itemId, QString* errorMessage = nullptr) const;
    QString getTotp(const QString& itemId, QString* errorMessage = nullptr) const;
    ItemDetails getItemDetails(const QString& itemId, QString* errorMessage = nullptr) const;
    static ItemDetails detailsFromVaultItem(const VaultItem& item);

private:
    struct CommandResult
    {
        bool started = false;
        bool timedOut = false;
        bool crashed = false;
        int exitCode = -1;
        QByteArray standardOutput;
        QByteArray standardError;
        QString errorString;

        bool ok() const;
    };

    static QString findBwProgram();
    static QString describeCommandError(const QString& program,
        const QStringList& arguments,
        const CommandResult& result);
    static QString describeCommandError(const QString& program,
        const QString& displayCommand,
        const CommandResult& result);
    CommandResult run(const QStringList& arguments,
        const QByteArray& standardInput = {},
        int timeoutMs = 15000,
        const QProcessEnvironment& extraEnvironment = QProcessEnvironment()) const;

    mutable QRecursiveMutex m_commandMutex;
    mutable QMutex m_stateMutex;
    QString m_program;
    QString m_session;
};
