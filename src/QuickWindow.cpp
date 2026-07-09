#include "QuickWindow.h"

#include "AppSettings.h"
#include "I18n.h"
#include "SettingsDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMessageAuthenticationCode>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace
{
constexpr int ItemIdRole = Qt::UserRole;
constexpr int UsernameRole = Qt::UserRole + 1;
constexpr int PasswordRole = Qt::UserRole + 2;
constexpr int TotpRole = Qt::UserRole + 3;
constexpr int FaviconHostRole = Qt::UserRole + 4;
constexpr int MaxRecentItems = 30;

struct TotpSpec
{
    QByteArray secret;
    int digits = 6;
    int period = 30;
    QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha1;
};

QLabel* makeKeycap(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("keycapLabel");
    label->setAlignment(Qt::AlignCenter);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return label;
}

QStringList keySequenceParts(const QKeySequence& sequence)
{
    const QString text = sequence.toString(QKeySequence::NativeText);
    QStringList parts = text.split('+', Qt::SkipEmptyParts);

    if (parts.isEmpty())
        parts.push_back(text);

    for (QString& part : parts)
        part = part.trimmed();

    return parts;
}

QString uiText(const char* ko, const char* en)
{
    return I18n::translate("QuickWindow", en, ko);
}

class DraggableDialog : public QDialog
{
public:
    explicit DraggableDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }

        QDialog::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && event->buttons().testFlag(Qt::LeftButton)) {
            move(event->globalPosition().toPoint() - m_dragOffset);
            event->accept();
            return;
        }

        QDialog::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_dragging = false;
        QDialog::mouseReleaseEvent(event);
    }

private:
    bool m_dragging = false;
    QPoint m_dragOffset;
};

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

QString cleanStatusText(const QString& message)
{
    QString text = stripTerminalControls(message);
    text.remove(QRegularExpression(
        QStringLiteral("\\?\\s*Master password:\\s*\\[(?:input is hidden|hidden)\\]\\s*"),
        QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression(QStringLiteral("[ \\t]*\\n[ \\t]*")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s{2,}")), QStringLiteral(" "));
    text = text.trimmed();

    return text;
}

QString compactStatusText(const QString& message)
{
    QString text = cleanStatusText(message);

    constexpr qsizetype MaxStatusLength = 180;
    if (text.size() > MaxStatusLength)
        text = text.left(MaxStatusLength - 3).trimmed() + QStringLiteral("...");

    return text;
}

QString defaultServerUrlForMode(const QString& mode)
{
    if (mode == "eu")
        return "https://vault.bitwarden.eu";
    if (mode == "custom")
        return AppSettings::customBitwardenServerUrl();

    return "https://vault.bitwarden.com";
}

QString displayName(const VaultItem& item)
{
    const QString name = item.name.isEmpty() ? uiText("(이름 없는 항목)", "(unnamed item)") : item.name;
    QStringList fields;

    if (AppSettings::showResultUsername() && !item.username.isEmpty())
        fields.push_back(item.username);

    if (AppSettings::showResultUri() && !item.uris.isEmpty()) {
        QUrl url(item.uris.constFirst());
        QString host = url.host();
        if (host.isEmpty())
            host = item.uris.constFirst();
        fields.push_back(host);
    }

    if (AppSettings::showResultType() && !item.type.isEmpty())
        fields.push_back(item.type);

    if (fields.isEmpty())
        return name;

    return QString("%1 - %2").arg(name, fields.join(QStringLiteral(" · ")));
}

QUrl normalizedItemUrl(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return {};

    QUrl url(trimmed);
    if (url.scheme().isEmpty() || url.host().isEmpty())
        url = QUrl(QStringLiteral("https://") + trimmed);

    return url;
}

QUrl webUrlForItemUri(const QString& value)
{
    const QUrl url = normalizedItemUrl(value);
    const QString scheme = url.scheme().toLower();
    if ((scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) || url.host().isEmpty())
        return {};

    return url;
}

QString linkedWebsiteText(const QStringList& uris)
{
    QStringList lines;
    for (const QString& uri : uris) {
        const QString displayText = uri.trimmed();
        if (displayText.isEmpty())
            continue;

        const QUrl url = webUrlForItemUri(displayText);
        if (!url.isValid()) {
            lines.push_back(displayText.toHtmlEscaped());
            continue;
        }

        const QString href = QString::fromUtf8(url.toEncoded()).toHtmlEscaped();
        lines.push_back(QStringLiteral("<a style=\"color:#6bb6ff; text-decoration:none;\" href=\"%1\">%2</a>")
            .arg(href, displayText.toHtmlEscaped()));
    }

    return lines.join(QStringLiteral("<br>"));
}

QString faviconHostForItem(const VaultItem& item)
{
    for (const QString& uri : item.uris) {
        const QUrl url = normalizedItemUrl(uri);
        const QString host = url.host().trimmed().toLower();
        if (!host.isEmpty())
            return host;
    }

    return {};
}

QUrl faviconUrlForItem(const VaultItem& item)
{
    for (const QString& uri : item.uris) {
        const QUrl sourceUrl = normalizedItemUrl(uri);
        const QString host = sourceUrl.host().trimmed().toLower();
        if (host.isEmpty())
            continue;

        QString scheme = sourceUrl.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
            scheme = QStringLiteral("https");

        QUrl faviconUrl;
        faviconUrl.setScheme(scheme);
        faviconUrl.setHost(host);
        faviconUrl.setPath(QStringLiteral("/favicon.ico"));
        return faviconUrl;
    }

    return {};
}

QIcon makeResultIcon(const VaultItem& item)
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF box(2.0, 2.0, 18.0, 18.0);
    QColor background("#315b9f");
    if (item.typeCode == 2)
        background = QColor("#6b6075");
    else if (item.typeCode == 3)
        background = QColor("#3d7d73");
    else if (item.typeCode == 4)
        background = QColor("#7557a8");
    else if (item.typeCode == 5)
        background = QColor("#8a5b3a");

    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(box, 5, 5);

    QPen pen(QColor("#ffffff"), 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (item.typeCode == 2) {
        painter.drawRoundedRect(QRectF(7, 5.5, 8, 11), 1.5, 1.5);
        painter.drawLine(QPointF(9, 9), QPointF(13, 9));
        painter.drawLine(QPointF(9, 12), QPointF(13, 12));
    } else if (item.typeCode == 3) {
        painter.drawRoundedRect(QRectF(5.5, 7, 11, 8), 1.5, 1.5);
        painter.drawLine(QPointF(5.8, 10), QPointF(16.2, 10));
    } else if (item.typeCode == 4) {
        painter.drawEllipse(QRectF(8, 5, 6, 6));
        painter.drawArc(QRectF(5.5, 10, 11, 8), 25 * 16, 130 * 16);
    } else if (item.typeCode == 5) {
        painter.drawEllipse(QRectF(5.5, 7, 5, 5));
        painter.drawLine(QPointF(10.5, 9.5), QPointF(16.5, 9.5));
        painter.drawLine(QPointF(14.5, 9.5), QPointF(14.5, 12));
        painter.drawLine(QPointF(16.5, 9.5), QPointF(16.5, 11.5));
    } else {
        painter.drawEllipse(QRectF(5, 5, 12, 12));
        painter.drawLine(QPointF(5, 11), QPointF(17, 11));
        painter.drawArc(QRectF(8, 5, 6, 12), 90 * 16, 180 * 16);
        painter.drawArc(QRectF(8, 5, 6, 12), -90 * 16, 180 * 16);
    }

    if (!item.totp.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#5bd6ff"));
        painter.drawEllipse(QRectF(14.2, 14.2, 5.2, 5.2));
    }

    return QIcon(pixmap);
}

QString shortcutMarkup(const QString& shortcut, const QString& label, const QString& labelColor = QStringLiteral("#f0ecf6"))
{
    return QStringLiteral("<span style=\"color:#b8aebe;font-weight:800;\">%1</span><span style=\"color:%3;font-weight:650;\"> %2</span>")
        .arg(shortcut.toHtmlEscaped(), label.toHtmlEscaped(), labelColor);
}

QByteArray decodeBase32(const QString& input, bool* ok)
{
    QByteArray output;
    quint32 buffer = 0;
    int bitsLeft = 0;
    bool valid = true;

    for (QChar ch : input.trimmed().toUpper()) {
        if (ch == QLatin1Char('='))
            break;
        if (ch.isSpace() || ch == QLatin1Char('-'))
            continue;

        int value = -1;
        if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            value = ch.unicode() - 'A';
        else if (ch >= QLatin1Char('2') && ch <= QLatin1Char('7'))
            value = 26 + ch.unicode() - '2';

        if (value < 0) {
            valid = false;
            break;
        }

        buffer = (buffer << 5) | static_cast<quint32>(value);
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            output.append(static_cast<char>((buffer >> (bitsLeft - 8)) & 0xff));
            bitsLeft -= 8;
            buffer &= bitsLeft == 0 ? 0 : ((1u << bitsLeft) - 1u);
        }
    }

    if (ok)
        *ok = valid && !output.isEmpty();
    return output;
}

QCryptographicHash::Algorithm totpAlgorithm(const QString& value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == "SHA256")
        return QCryptographicHash::Sha256;
    if (normalized == "SHA512")
        return QCryptographicHash::Sha512;
    return QCryptographicHash::Sha1;
}

TotpSpec parseTotpSpec(const QString& value, QString* errorMessage)
{
    QString secretText = value.trimmed();
    TotpSpec spec;

    if (secretText.startsWith(QStringLiteral("otpauth://"), Qt::CaseInsensitive)) {
        const QUrl url(secretText);
        const QUrlQuery query(url);
        secretText = query.queryItemValue(QStringLiteral("secret"));
        if (query.hasQueryItem(QStringLiteral("digits")))
            spec.digits = qBound(4, query.queryItemValue(QStringLiteral("digits")).toInt(), 10);
        if (query.hasQueryItem(QStringLiteral("period")))
            spec.period = qBound(5, query.queryItemValue(QStringLiteral("period")).toInt(), 300);
        spec.algorithm = totpAlgorithm(query.queryItemValue(QStringLiteral("algorithm")));
    }

    bool ok = false;
    spec.secret = decodeBase32(secretText, &ok);
    if (!ok && errorMessage)
        *errorMessage = uiText("TOTP secret을 해석하지 못했습니다.", "Could not parse the TOTP secret.");

    return spec;
}

QString generateTotpCode(const QString& value, QString* errorMessage)
{
    if (value.trimmed().isEmpty()) {
        if (errorMessage)
            *errorMessage = uiText("이 항목에는 TOTP가 없습니다.", "This item has no TOTP.");
        return {};
    }

    const TotpSpec spec = parseTotpSpec(value, errorMessage);
    if (spec.secret.isEmpty())
        return {};

    const quint64 counter = static_cast<quint64>(QDateTime::currentSecsSinceEpoch() / spec.period);
    QByteArray message(8, '\0');
    for (int index = 7; index >= 0; --index)
        message[7 - index] = static_cast<char>((counter >> (index * 8)) & 0xff);

    const QByteArray hmac = QMessageAuthenticationCode::hash(message, spec.secret, spec.algorithm);
    if (hmac.size() < 20) {
        if (errorMessage)
            *errorMessage = uiText("TOTP 코드를 만들지 못했습니다.", "Could not generate the TOTP code.");
        return {};
    }

    const int offset = static_cast<unsigned char>(hmac.at(hmac.size() - 1)) & 0x0f;
    if (offset + 3 >= hmac.size()) {
        if (errorMessage)
            *errorMessage = uiText("TOTP 코드를 만들지 못했습니다.", "Could not generate the TOTP code.");
        return {};
    }

    const quint32 binary =
        ((static_cast<unsigned char>(hmac.at(offset)) & 0x7f) << 24)
        | ((static_cast<unsigned char>(hmac.at(offset + 1)) & 0xff) << 16)
        | ((static_cast<unsigned char>(hmac.at(offset + 2)) & 0xff) << 8)
        | (static_cast<unsigned char>(hmac.at(offset + 3)) & 0xff);

    quint32 modulo = 1;
    for (int index = 0; index < spec.digits; ++index)
        modulo *= 10;

    if (errorMessage)
        errorMessage->clear();
    return QStringLiteral("%1").arg(binary % modulo, spec.digits, 10, QLatin1Char('0'));
}

QString itemSearchText(const VaultItem& item)
{
    QStringList values;
    values << item.name
           << item.username
           << item.type
           << item.notes
           << item.folderId
           << item.organizationId
           << item.collectionIds
           << item.uris;
    return values.join(QLatin1Char('\n')).toLower();
}

int itemMatchGroup(const VaultItem& item, const QString& needle)
{
    if (item.name.compare(needle, Qt::CaseInsensitive) == 0)
        return 0;
    if (item.name.startsWith(needle, Qt::CaseInsensitive))
        return 1;
    if (item.username.startsWith(needle, Qt::CaseInsensitive))
        return 2;
    for (const QString& uri : item.uris) {
        if (uri.contains(needle, Qt::CaseInsensitive))
            return 3;
    }
    return 4;
}

int resultRowHeight()
{
    return qMax(38, AppSettings::quickFontSize() + 28);
}
}

QuickWindow::QuickWindow(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("quickWindow");
    setWindowTitle("PeekWarden");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);

    m_search = new QLineEdit(this);
    m_search->setObjectName("searchEdit");
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(uiText("비밀번호 검색", "Password search"));
    m_search->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition);
    m_search->installEventFilter(this);

    m_list = new QListWidget(this);
    m_list->setObjectName("resultList");
    m_list->setVisible(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_list->installEventFilter(this);

    m_faviconManager = new QNetworkAccessManager(this);
    connect(m_faviconManager, &QNetworkAccessManager::finished, this, [this](QNetworkReply* reply) {
        handleFaviconFinished(reply);
    });

    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setMaximumHeight(42);
    m_status->setVisible(false);

    m_busyBar = new QProgressBar(this);
    m_busyBar->setObjectName("busyBar");
    m_busyBar->setRange(0, 0);
    m_busyBar->setTextVisible(false);
    m_busyBar->setFixedHeight(3);
    m_busyBar->setVisible(false);

    m_authPanel = new QWidget(this);
    m_authPanel->setObjectName("authPanel");
    m_authPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_authPanel->setVisible(false);

    m_authTitle = new QLabel(m_authPanel);
    m_authTitle->setObjectName("authTitle");

    m_authNote = new QLabel(m_authPanel);
    m_authNote->setObjectName("authNote");
    m_authNote->setWordWrap(true);

    m_authAccountLabel = new QLabel(m_authPanel);
    m_authAccountLabel->setObjectName("authFieldLabel");
    m_authServerLabel = new QLabel(m_authPanel);
    m_authServerLabel->setObjectName("authFieldLabel");
    m_authEmailLabel = new QLabel(m_authPanel);
    m_authEmailLabel->setObjectName("authFieldLabel");
    m_authPasswordLabel = new QLabel(m_authPanel);
    m_authPasswordLabel->setObjectName("authFieldLabel");
    m_authMethodLabel = new QLabel(m_authPanel);
    m_authMethodLabel->setObjectName("authFieldLabel");
    m_authCodeLabel = new QLabel(m_authPanel);
    m_authCodeLabel->setObjectName("authFieldLabel");

    m_authAccount = new QLabel(m_authPanel);
    m_authAccount->setObjectName("authAccountText");
    m_authAccount->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_authServer = new QComboBox(m_authPanel);
    m_authServer->setObjectName("authServerCombo");

    m_authCustomServer = new QLineEdit(m_authPanel);
    m_authCustomServer->setObjectName("authCustomServerEdit");
    m_authCustomServer->setClearButtonEnabled(true);

    m_authEmail = new QLineEdit(m_authPanel);
    m_authEmail->setObjectName("authEmailEdit");
    m_authEmail->setClearButtonEnabled(true);

    m_authPassword = new QLineEdit(m_authPanel);
    m_authPassword->setObjectName("authPasswordEdit");
    m_authPassword->setEchoMode(QLineEdit::Password);

    m_authMethod = new QComboBox(m_authPanel);
    m_authMethod->setObjectName("authMethodCombo");

    m_authCode = new QLineEdit(m_authPanel);
    m_authCode->setObjectName("authCodeEdit");
    m_authCode->setClearButtonEnabled(true);

    m_authSubmit = new QPushButton(m_authPanel);
    m_authSubmit->setObjectName("authSubmitButton");
    m_authCancel = new QPushButton(m_authPanel);
    m_authCancel->setObjectName("authCancelButton");

    auto* authForm = new QGridLayout;
    authForm->setContentsMargins(0, 0, 0, 0);
    authForm->setHorizontalSpacing(10);
    authForm->setVerticalSpacing(7);
    authForm->addWidget(m_authAccountLabel, 0, 0);
    authForm->addWidget(m_authAccount, 0, 1);
    authForm->addWidget(m_authServerLabel, 1, 0);
    authForm->addWidget(m_authServer, 1, 1);
    authForm->addWidget(m_authCustomServer, 2, 1);
    authForm->addWidget(m_authEmailLabel, 3, 0);
    authForm->addWidget(m_authEmail, 3, 1);
    authForm->addWidget(m_authPasswordLabel, 4, 0);
    authForm->addWidget(m_authPassword, 4, 1);
    authForm->addWidget(m_authMethodLabel, 5, 0);
    authForm->addWidget(m_authMethod, 5, 1);
    authForm->addWidget(m_authCodeLabel, 6, 0);
    authForm->addWidget(m_authCode, 6, 1);
    authForm->setColumnStretch(1, 1);

    m_authHintBar = new QWidget(m_authPanel);
    m_authHintBar->setObjectName("authHintBar");
    m_authHintBar->setVisible(false);
    m_authHintLayout = new QHBoxLayout(m_authHintBar);
    m_authHintLayout->setContentsMargins(0, 0, 0, 0);
    m_authHintLayout->setSpacing(5);

    m_authBusyLabel = new QLabel(m_authPanel);
    m_authBusyLabel->setObjectName("authBusyLabel");
    m_authBusyLabel->setVisible(false);

    m_authBusyBar = new QProgressBar(m_authPanel);
    m_authBusyBar->setObjectName("busyBar");
    m_authBusyBar->setRange(0, 0);
    m_authBusyBar->setTextVisible(false);
    m_authBusyBar->setFixedHeight(3);
    m_authBusyBar->setVisible(false);

    auto* authButtons = new QHBoxLayout;
    authButtons->setContentsMargins(0, 0, 0, 0);
    authButtons->setSpacing(8);
    authButtons->addWidget(m_authBusyLabel);
    authButtons->addStretch(1);
    authButtons->addWidget(m_authCancel);
    authButtons->addWidget(m_authSubmit);

    auto* authLayout = new QVBoxLayout(m_authPanel);
    authLayout->setContentsMargins(0, 0, 0, 0);
    authLayout->setSpacing(6);
    authLayout->addWidget(m_authTitle);
    authLayout->addWidget(m_authNote);
    authLayout->addLayout(authForm);
    authLayout->addLayout(authButtons);

    m_footer = new QWidget(this);
    m_footer->setObjectName("footerBar");
    m_footer->setMinimumHeight(0);
    m_footer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_footerLayout = new QGridLayout(m_footer);
    m_footerLayout->setContentsMargins(0, 0, 0, 0);
    m_footerLayout->setHorizontalSpacing(12);
    m_footerLayout->setVerticalSpacing(2);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(7);
    layout->addWidget(m_search);
    layout->addWidget(m_authPanel, 0, Qt::AlignTop);
    layout->addWidget(m_list);
    layout->addWidget(m_status);
    layout->addWidget(m_footer);

    setStyleSheet(R"(
        #quickWindow {
            background: #3a2a3f;
            border: 1px solid #080a12;
            color: #f5f0fb;
        }
        #searchEdit {
            background: #171326;
            color: #f6f2ff;
            border: 2px solid #1f8cff;
            border-radius: 9px;
            font-size: 15px;
            font-weight: 600;
            padding: 9px 10px;
            min-height: 26px;
            selection-background-color: #1f8cff;
            selection-color: #ffffff;
            placeholder-text-color: #a69bad;
        }
        #resultList {
            background: #21192a;
            color: #f6f2ff;
            border: 1px solid #594d64;
            border-radius: 8px;
            font-size: 14px;
            outline: 0;
            padding: 2px;
            selection-background-color: #1f8cff;
            selection-color: #ffffff;
        }
        #resultList::item {
            color: #f6f2ff;
            min-height: 34px;
            padding: 5px 9px;
            border-radius: 5px;
        }
        #resultList::item:selected {
            background: #1f8cff;
            color: #ffffff;
            border-radius: 5px;
        }
        #busyBar {
            background: rgba(255, 255, 255, 35);
            border: 0;
            border-radius: 1px;
        }
        #busyBar::chunk {
            background: #49a0ff;
            border-radius: 1px;
        }
        #authPanel {
            background: transparent;
        }
        #authTitle {
            color: #f6f2ff;
            font-size: 13px;
            font-weight: 700;
        }
        #authNote {
            color: #d8cedf;
            font-size: 12px;
        }
        #authFieldLabel {
            color: #f0ecf6;
            font-size: 12px;
            font-weight: 600;
        }
        #authAccountText {
            color: #f6f2ff;
            font-size: 13px;
            font-weight: 700;
            padding: 4px 0;
        }
        #authHintBar {
            background: transparent;
        }
        #authBusyLabel {
            color: #f0c7d2;
            font-size: 12px;
        }
        #authPanel QLineEdit, #authPanel QComboBox {
            background: #191326;
            color: #f7f3ff;
            border: 1px solid #5f5270;
            border-radius: 6px;
            padding: 6px 8px;
            selection-background-color: #1f8cff;
            selection-color: #ffffff;
            min-height: 24px;
        }
        #authPanel QLineEdit:focus, #authPanel QComboBox:focus {
            border: 1px solid #1f8cff;
        }
        #authPanel QPushButton {
            background: #5a5064;
            color: #f7f3ff;
            border: 1px solid #72667d;
            border-radius: 6px;
            padding: 6px 12px;
        }
        #authPanel QPushButton:hover {
            background: #6a5e75;
        }
        #authPanel QPushButton#authSubmitButton {
            background: #1f72c7;
            border-color: #49a0ff;
            font-weight: 700;
        }
        #authPanel QPushButton#authSubmitButton:hover {
            background: #2484e7;
        }
        #statusLabel {
            color: #f0c7d2;
            font-size: 12px;
            min-height: 16px;
        }
        #footerBar {
            background: transparent;
            min-height: 28px;
        }
        #footerText {
            color: #f0ecf6;
            font-size: 12px;
            font-weight: 600;
            padding-left: 2px;
            padding-right: 12px;
        }
        #footerShortcut {
            color: #f0ecf6;
            font-size: 12px;
            padding: 0;
            margin: 0;
        }
        #keycapLabel {
            background: #5a5064;
            border: 1px solid #756a80;
            border-radius: 5px;
            color: #e7e1ec;
            font-size: 11px;
            font-weight: 700;
            min-height: 19px;
            padding: 1px 7px;
        }
    )");

    applySettings();

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(60);

    m_refreshTimer.setSingleShot(false);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (m_bw.hasSession())
            startItemLoad(true, true);
        else
            m_refreshTimer.stop();
    });
    updateAutoSyncTimer();

    connect(m_search, &QLineEdit::textChanged, this, [this] {
        if (m_itemsReady || m_search->text().trimmed().isEmpty()) {
            m_debounce.stop();
            reload();
            return;
        }

        m_debounce.start();
    });

    connect(&m_debounce, &QTimer::timeout, this, [this] {
        reload();
    });

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        showItemDetails(item);
    });

    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        updateFooter();
    });

    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (m_authMode != AuthMode::None)
            return;

        QListWidgetItem* item = m_list->itemAt(position);
        if (!item)
            return;

        m_list->setCurrentItem(item);

        QMenu menu(this);
        menu.setStyleSheet(R"(
            QMenu {
                background: #21192a;
                color: #f6f2ff;
                border: 1px solid #594d64;
                padding: 4px;
            }
            QMenu::item {
                padding: 7px 28px 7px 12px;
                border-radius: 4px;
            }
            QMenu::item:selected {
                background: #1f8cff;
                color: #ffffff;
            }
            QMenu::separator {
                height: 1px;
                background: #594d64;
                margin: 4px 6px;
            }
        )");

        QAction* detailsAction = menu.addAction(uiText("상세보기", "Details"));
        menu.addSeparator();
        QAction* copyPasswordAction = menu.addAction(uiText("비밀번호 복사", "Copy password"));
        QAction* copyUsernameAction = menu.addAction(uiText("사용자 이름 복사", "Copy username"));
        QAction* copyTotpAction = menu.addAction(uiText("TOTP 복사", "Copy TOTP"));

        QAction* selectedAction = menu.exec(m_list->viewport()->mapToGlobal(position));
        if (selectedAction == detailsAction)
            showItemDetails(item);
        else if (selectedAction == copyPasswordAction)
            copyPassword(item);
        else if (selectedAction == copyUsernameAction)
            copyUsername(item);
        else if (selectedAction == copyTotpAction)
            copyTotp(item);
    });

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
        hide();
    });

    auto* returnShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(returnShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode != AuthMode::None) {
            submitAuth();
            return;
        }
        copyPassword(currentItem());
    });

    auto* enterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    connect(enterShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode != AuthMode::None) {
            submitAuth();
            return;
        }
        copyPassword(currentItem());
    });

    auto* copyUsernameShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), this);
    connect(copyUsernameShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            copyUsername(currentItem());
    });

    auto* copyPasswordShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this);
    connect(copyPasswordShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            copyPassword(currentItem());
    });

    auto* copyTotpCtrlShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C), this);
    connect(copyTotpCtrlShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            copyTotp(currentItem());
    });

    auto* detailsShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this);
    connect(detailsShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            showItemDetails(currentItem());
    });

    auto* copyUsernameReturn = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(copyUsernameReturn, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            copyUsername(currentItem());
    });

    auto* copyUsernameEnter = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this);
    connect(copyUsernameEnter, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            copyUsername(currentItem());
    });

    auto* copyTotpShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_T), this);
    connect(copyTotpShortcut, &QShortcut::activated, this, [this] {
        if (m_authMode == AuthMode::None)
            copyTotp(currentItem());
    });

    auto* settingsShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash), this);
    connect(settingsShortcut, &QShortcut::activated, this, [this] {
        openSettings();
    });

    auto* syncShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
    connect(syncShortcut, &QShortcut::activated, this, [this] {
        syncVault();
    });

    connect(m_authSubmit, &QPushButton::clicked, this, [this] {
        submitAuth();
    });

    connect(m_authCancel, &QPushButton::clicked, this, [this] {
        hide();
    });

    connect(m_authServer, &QComboBox::currentIndexChanged, this, [this] {
        const bool custom = m_authServer->currentData().toString() == "custom";
        m_authCustomServer->setVisible(custom);
        updateWindowSize();
    });

    connect(m_authEmail, &QLineEdit::returnPressed, this, [this] {
        m_authPassword->setFocus();
    });

    connect(m_authPassword, &QLineEdit::returnPressed, this, [this] {
        submitAuth();
    });

    connect(m_authCode, &QLineEdit::returnPressed, this, [this] {
        submitAuth();
    });

    connect(&m_prepareWatcher, &QFutureWatcher<PrepareResult>::finished, this, [this] {
        handlePrepareFinished();
    });

    connect(&m_authWatcher, &QFutureWatcher<AuthResult>::finished, this, [this] {
        handleAuthFinished();
    });

    connect(&m_searchWatcher, &QFutureWatcher<SearchResult>::finished, this, [this] {
        handleSearchFinished();
    });

    connect(&m_itemsWatcher, &QFutureWatcher<ItemsResult>::finished, this, [this] {
        handleItemsFinished();
    });

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget* old, QWidget* now) {
        const bool focusLeftWindow = old && (old == this || isAncestorOf(old))
            && !(now && (now == this || isAncestorOf(now)));
        if (focusLeftWindow || !now)
            scheduleFocusLossCheck();
    });

    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationInactive)
            scheduleFocusLossCheck();
    });
}

void QuickWindow::applySettings()
{
    m_bw.reloadSettings();
    applyStyle();
    updateAuthTexts();
    if (m_authMode == AuthMode::None)
        m_search->setPlaceholderText(uiText("비밀번호 검색", "Password search"));
    m_search->setVisible(m_authMode == AuthMode::None);
    m_footer->setVisible(m_authMode == AuthMode::None && AppSettings::showShortcutHints());
    setWindowOpacity(AppSettings::windowOpacity());
    if (!AppSettings::fetchFaviconsFromWeb())
        m_faviconCache.clear();
    if (!AppSettings::showResultIcons() || !AppSettings::fetchFaviconsFromWeb())
        m_pendingFavicons.clear();
    if (m_list) {
        for (int rowIndex = 0; rowIndex < m_list->count(); ++rowIndex) {
            QListWidgetItem* row = m_list->item(rowIndex);
            if (!row)
                continue;

            const VaultItem* item = cachedItemById(row->data(ItemIdRole).toString());
            if (!AppSettings::showResultIcons() || !item) {
                row->setIcon(QIcon());
                continue;
            }

            row->setIcon(makeResultIcon(*item));
            if (AppSettings::fetchFaviconsFromWeb())
                requestFaviconForRow(row, *item);
        }
    }
    updateAutoSyncTimer();
    updateFooter();
    updateWindowSize();
}

void QuickWindow::setSettingsChangedCallback(std::function<void()> callback)
{
    m_settingsChanged = std::move(callback);
}

bool QuickWindow::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == m_search || watched == m_list) && event->type() == QEvent::KeyPress && m_authMode == AuthMode::None) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();

        if (keyEvent->key() == Qt::Key_C && modifiers.testFlag(Qt::ControlModifier)) {
            if (modifiers.testFlag(Qt::ShiftModifier)) {
                copyPassword(currentItem());
            } else if (modifiers.testFlag(Qt::AltModifier)) {
                copyTotp(currentItem());
            } else {
                copyUsername(currentItem());
            }
            return true;
        }

        if (keyEvent->key() == Qt::Key_O
            && modifiers.testFlag(Qt::ControlModifier)
            && modifiers.testFlag(Qt::ShiftModifier)) {
            showItemDetails(currentItem());
            return true;
        }

        if (keyEvent->key() == Qt::Key_Down) {
            moveSelection(1);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Up) {
            moveSelection(-1);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void QuickWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::ActivationChange)
        scheduleFocusLossCheck();
}

void QuickWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateWindowMask();

    if (!m_busyBar)
        return;

    m_busyBar->setGeometry(0, height() - m_busyBar->height(), width(), m_busyBar->height());
    m_busyBar->raise();
}

void QuickWindow::showSettings()
{
    openSettings();
}

bool QuickWindow::lockVault(QString* errorMessage)
{
    const bool locked = m_bw.lock(errorMessage);
    m_refreshTimer.stop();
    m_itemsReady = false;
    m_cachedItems.clear();
    m_faviconCache.clear();
    m_pendingFavicons.clear();
    m_queuedSearchText.clear();
    m_list->clear();
    m_list->setVisible(false);
    m_search->clear();
    hideAuthPanel();
    setStatus({});
    updateFooter();
    hide();
    return locked;
}

bool QuickWindow::logoutVault(QString* errorMessage)
{
    const bool loggedOut = m_bw.logout(errorMessage);
    m_refreshTimer.stop();
    m_itemsReady = false;
    m_cachedItems.clear();
    m_faviconCache.clear();
    m_pendingFavicons.clear();
    m_queuedSearchText.clear();
    m_recentItemIds.clear();
    m_currentUserEmail.clear();
    m_currentServerUrl.clear();
    m_list->clear();
    m_list->setVisible(false);
    m_search->clear();
    m_authEmail->clear();
    hideAuthPanel();
    setBusy(false);
    setStatus({});
    updateFooter();

    if (loggedOut) {
        applySettings();
        showAuthPanel(AuthMode::Login);
        moveToConfiguredPosition();
        show();
        raise();
        activateWindow();
        m_authEmail->setFocus();
    }

    return loggedOut;
}

void QuickWindow::preloadVault()
{
    if (m_authMode != AuthMode::None || m_itemsReady
        || m_prepareWatcher.isRunning() || m_authWatcher.isRunning() || m_itemsWatcher.isRunning())
        return;

    prepareVault(false);
}

void QuickWindow::showQuick()
{
    if (isVisible()) {
        const bool focusInside = QApplication::focusWidget()
            && (QApplication::focusWidget() == this || isAncestorOf(QApplication::focusWidget()));
        if (isActiveWindow() || focusInside) {
            hide();
            return;
        }

        raise();
        activateWindow();
        if (m_authMode == AuthMode::None)
            m_search->setFocus();
        else
            m_authPassword->setFocus();
        return;
    }

    const bool restoreSearch = shouldRestorePreservedSearch();
    applySettings();
    hideAuthPanel();
    m_search->setVisible(true);
    m_footer->setVisible(AppSettings::showShortcutHints());
    m_search->setEnabled(true);
    if (!restoreSearch) {
        m_search->clear();
        m_list->clear();
        m_list->setVisible(false);
    }
    setStatus({});
    updateWindowSize();
    moveToConfiguredPosition();
    show();
    raise();
    activateWindow();

    if (m_bw.hasSession() && m_itemsReady) {
        setBusy(false);
        m_search->setEnabled(true);
        if (restoreSearch)
            restorePreservedSearch();
        else
            m_search->setFocus();
        return;
    }

    if (restoreSearch)
        restorePreservedSearch();

    if (m_itemsWatcher.isRunning()) {
        m_quietItemLoad = false;
        setBusy(true, uiText("Bitwarden 항목 불러오는 중...", "Loading Bitwarden items..."));
        m_search->setEnabled(true);
        m_search->setFocus();
        return;
    }

    prepareVault();
}

void QuickWindow::openSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.logoutRequested()) {
        QString errorMessage;
        if (!logoutVault(&errorMessage) && !errorMessage.isEmpty()) {
            QMessageBox::warning(
                this,
                uiText("Bitwarden 로그아웃", "Bitwarden Logout"),
                errorMessage);
        }
        return;
    }

    if (m_settingsChanged)
        m_settingsChanged();

    applySettings();
}

void QuickWindow::applyStyle()
{
    const int radius = AppSettings::roundedCorners() ? 12 : 0;
    const int fontSize = AppSettings::quickFontSize();
    const int baseWeight = AppSettings::quickFontBold() ? 700 : 500;
    const int footerFontSize = qMax(9, fontSize - 3);
    const int keycapFontSize = qMax(9, fontSize - 3);
    const int rowMinHeight = qMax(30, fontSize + 20);
    QString fontFamily = AppSettings::quickFontFamily();
    fontFamily.replace('\\', "\\\\");
    fontFamily.replace('"', "\\\"");
    setAttribute(Qt::WA_TranslucentBackground, false);

    QString style = styleSheet();
    style.replace(QRegularExpression(QStringLiteral("#quickWindow\\s*\\{[^}]*\\}")),
        QStringLiteral(R"(
        #quickWindow {
            background: %1;
            border: 1px solid #080a12;
            border-radius: %2px;
            color: #f5f0fb;
        })")
            .arg(AppSettings::windowBackgroundColor())
            .arg(radius));
    style.remove(QRegularExpression(
        QStringLiteral("/\\* dynamic-style-start \\*/.*?/\\* dynamic-style-end \\*/"),
        QRegularExpression::DotMatchesEverythingOption));
    style.append(QStringLiteral(R"(
        /* dynamic-style-start */
        #quickWindow, #quickWindow QLabel, #quickWindow QLineEdit, #quickWindow QListWidget, #quickWindow QComboBox, #quickWindow QPushButton {
            font-family: "%1";
        }
        #searchEdit {
            font-size: %2px;
            font-weight: %3;
        }
        #resultList {
            font-size: %4px;
            font-weight: %5;
        }
        #footerText {
            font-size: %6px;
            font-weight: %7;
            padding-left: 1px;
            padding-right: 6px;
        }
        #footerShortcut {
            font-size: %6px;
        }
        #keycapLabel {
            font-size: %8px;
            min-height: 17px;
            padding: 0 5px;
        }
        #resultList::item {
            min-height: %9px;
            padding: 4px 8px;
            border-radius: 5px;
        }
        #authTitle, #detailHeader {
            font-family: "%1";
        }
        /* dynamic-style-end */
    )")
        .arg(fontFamily)
        .arg(fontSize + 1)
        .arg(baseWeight)
        .arg(fontSize)
        .arg(baseWeight)
        .arg(footerFontSize)
        .arg(baseWeight)
        .arg(keycapFontSize)
        .arg(rowMinHeight));
    setStyleSheet(style);
    updateWindowMask();
}

void QuickWindow::updateWindowMask()
{
    if (!AppSettings::roundedCorners()) {
        clearMask();
        return;
    }

    QPainterPath path;
    path.addRoundedRect(rect(), 12, 12);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void QuickWindow::updateAuthTexts()
{
    if (!m_authPanel)
        return;

    const QString currentServer = m_authServer->currentData().toString();
    m_authServer->clear();
    m_authServer->addItem("bitwarden.com", "com");
    m_authServer->addItem("bitwarden.eu", "eu");
    m_authServer->addItem(uiText("사용자 지정", "Custom"), "custom");

    QString serverMode = currentServer.isEmpty() ? AppSettings::bitwardenServerMode() : currentServer;
    int serverIndex = m_authServer->findData(serverMode);
    if (serverIndex < 0)
        serverIndex = 0;
    m_authServer->setCurrentIndex(serverIndex);

    const QString currentMethod = m_authMethod->currentData().toString();
    m_authMethod->clear();
    m_authMethod->addItem(uiText("없음", "None"), QString());
    m_authMethod->addItem(uiText("인증 앱", "Authenticator app"), "0");
    m_authMethod->addItem(uiText("이메일", "Email"), "1");
    m_authMethod->addItem(uiText("YubiKey OTP", "YubiKey OTP"), "3");

    const int methodIndex = m_authMethod->findData(currentMethod);
    m_authMethod->setCurrentIndex(methodIndex >= 0 ? methodIndex : 0);

    m_authAccountLabel->setText(uiText("계정", "Account"));
    m_authServerLabel->setText(uiText("서버", "Server"));
    m_authEmailLabel->setText(uiText("이메일", "Email"));
    m_authPasswordLabel->setText(uiText("마스터 비밀번호", "Master password"));
    m_authMethodLabel->setText(uiText("2단계 방식", "Two-step method"));
    m_authCodeLabel->setText(uiText("2단계 코드", "Two-step code"));
    m_authCustomServer->setPlaceholderText("https://your.bitwarden.example.com");
    if (m_authCustomServer->text().trimmed().isEmpty())
        m_authCustomServer->setText(AppSettings::customBitwardenServerUrl());
    m_authEmail->setPlaceholderText("name@example.com");
    m_authCode->setPlaceholderText(uiText("필요한 경우에만 입력", "Only if required"));
    m_authCancel->setText(uiText("취소", "Cancel"));
    updateAuthHints();

    if (m_authMode == AuthMode::Unlock) {
        m_authTitle->setText(uiText("Bitwarden 잠금 해제", "Unlock Bitwarden"));
        m_authNote->setText(uiText("마스터 비밀번호를 입력하면 이 창에서 바로 검색을 시작할 수 있습니다.",
                                   "Enter your master password to start searching in this window."));
        m_authSubmit->setText(uiText("잠금 해제", "Unlock"));
        return;
    }

    m_authTitle->setText(uiText("Bitwarden 로그인", "Bitwarden Login"));
    m_authNote->setText(uiText("처음 사용하는 경우 Bitwarden CLI 계정 로그인이 먼저 필요합니다.",
                               "First use requires logging in to your Bitwarden CLI account."));
    m_authSubmit->setText(uiText("로그인", "Log in"));
}

void QuickWindow::updateAuthHints()
{
    if (!m_authHintLayout)
        return;

    while (QLayoutItem* item = m_authHintLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const auto addText = [this](const QString& text) {
        auto* label = new QLabel(text, m_authHintBar);
        label->setObjectName("footerText");
        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_authHintLayout->addWidget(label);
    };

    m_authHintLayout->addWidget(makeKeycap("Esc", m_authHintBar));
    addText(uiText("취소", "Cancel"));
    m_authHintLayout->addWidget(makeKeycap("Enter", m_authHintBar));
    addText(m_authMode == AuthMode::Unlock ? uiText("잠금 해제", "Unlock") : uiText("로그인", "Log in"));
}

void QuickWindow::updateFooter()
{
    if (!m_footerLayout)
        return;

    const auto hideFooter = [this] {
        if (m_footer) {
            m_footer->setVisible(false);
            m_footer->setFixedHeight(0);
            m_footer->setMinimumHeight(0);
            m_footer->setMaximumHeight(0);
            m_footer->updateGeometry();
        }
        updateWindowSize();
    };

    while (QLayoutItem* item = m_footerLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    if (!AppSettings::showShortcutHints()) {
        hideFooter();
        return;
    }

    if (m_authMode != AuthMode::None) {
        hideFooter();
        return;
    }

    if (m_footer) {
        m_footer->setMinimumHeight(0);
        m_footer->setMaximumHeight(QWIDGETSIZE_MAX);
        m_footer->setVisible(true);
    }

    const auto addShortcut = [this](int row,
                                 int column,
                                 const QString& shortcut,
                                 const QString& label,
                                 const QString& labelColor = QStringLiteral("#f0ecf6")) {
        auto* widget = new QLabel(m_footer);
        widget->setObjectName("footerShortcut");
        widget->setTextFormat(Qt::RichText);
        widget->setText(shortcutMarkup(shortcut, label, labelColor));
        widget->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        widget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_footerLayout->addWidget(widget, row, column);
        return column + 1;
    };

    const bool bitwardenStyle = AppSettings::shortcutPreset() == QStringLiteral("bitwarden");
    const QString usernameShortcut = bitwardenStyle ? QStringLiteral("Ctrl+Enter") : QStringLiteral("Ctrl+C");
    const QString passwordShortcut = bitwardenStyle ? QStringLiteral("Enter") : QStringLiteral("Ctrl+Shift+C");
    const QString totpShortcut = bitwardenStyle ? QStringLiteral("Alt+T") : QStringLiteral("Ctrl+Alt+C");

    int column = 0;
    column = addShortcut(0, column, usernameShortcut, uiText("아이디 복사", "Copy username"));
    column = addShortcut(0, column, passwordShortcut, uiText("비밀번호 복사", "Copy password"));
    column = addShortcut(0, column, totpShortcut, uiText("OTP 복사", "Copy OTP"));
    addShortcut(0, column, "Ctrl+Shift+O", uiText("상세보기", "Details"));

    column = 0;
    column = addShortcut(1, column, "Esc", uiText("숨기기", "Hide"));
    column = addShortcut(1, column, "Ctrl+/", uiText("설정", "Settings..."));
    addShortcut(1,
        column,
        "Ctrl+R",
        m_syncing ? uiText("동기화 중...", "Syncing...") : uiText("동기화", "Sync"),
        m_syncing ? QStringLiteral("#b8aebe") : QStringLiteral("#f0ecf6"));

    m_footerLayout->setColumnStretch(12, 1);
    if (m_footer) {
        m_footerLayout->activate();
        const int footerHeight = qMax(44, m_footerLayout->sizeHint().height() + 4);
        m_footer->setFixedHeight(footerHeight);
        m_footer->updateGeometry();
    }
    updateWindowSize();
}

void QuickWindow::updateWindowSize()
{
    const bool expanded = m_list && m_list->isVisible();
    int width = AppSettings::windowWidth();
    if (m_authPanel && m_authPanel->isVisible()) {
        if (QLayout* authLayout = m_authPanel->layout()) {
            authLayout->activate();
            m_authPanel->setFixedHeight(authLayout->sizeHint().height());
        }

        const int minimumAuthWidth = m_authMode == AuthMode::Unlock ? 420 : 500;
        const int maximumAuthWidth = qMax(AppSettings::windowWidth(), minimumAuthWidth);
        const int authContentWidth = m_authPanel->sizeHint().width() + 20;
        width = qMin(maximumAuthWidth, qMax(minimumAuthWidth, authContentWidth));
    } else if (m_authPanel) {
        m_authPanel->setMinimumHeight(0);
        m_authPanel->setMaximumHeight(QWIDGETSIZE_MAX);
    }

    if (m_footer && m_footer->isVisible()) {
        m_footerLayout->activate();
        const int footerFixedHeight = qMax(44, m_footerLayout->sizeHint().height() + 4);
        m_footer->setFixedHeight(footerFixedHeight);
        m_footer->adjustSize();
    } else if (m_footer) {
        m_footer->setFixedHeight(0);
    }

    QLayout* rootLayout = layout();
    const QMargins margins = rootLayout ? rootLayout->contentsMargins() : QMargins(10, 10, 10, 10);
    const int spacing = rootLayout ? qMax(0, rootLayout->spacing()) : 7;
    const int marginHeight = margins.top() + margins.bottom();
    auto visibleHeight = [](const QWidget* widget) {
        if (!widget || !widget->isVisible())
            return 0;

        return qMax(widget->sizeHint().height(), widget->minimumHeight());
    };

    const int searchHeight = visibleHeight(m_search);
    const int authHeight = visibleHeight(m_authPanel);
    const int statusHeight = visibleHeight(m_status);
    const int footerHeight = visibleHeight(m_footer);
    const int fixedWidgetCount =
        (m_search && m_search->isVisible() ? 1 : 0)
        + (m_authPanel && m_authPanel->isVisible() ? 1 : 0)
        + (m_status && m_status->isVisible() ? 1 : 0)
        + (m_footer && m_footer->isVisible() ? 1 : 0);
    const int visibleWidgetCount = fixedWidgetCount + (expanded ? 1 : 0);
    const int spacingHeight = qMax(0, visibleWidgetCount - 1) * spacing;
    const int fixedNonListHeight = marginHeight
        + spacingHeight
        + searchHeight
        + authHeight
        + statusHeight
        + footerHeight;
    const int minimumHeight = m_authPanel && m_authPanel->isVisible() ? 88 : 108;
    int listHeight = 0;
    if (expanded) {
        const int rowHeight = qMax(resultRowHeight(), m_list->sizeHintForRow(0));
        const int availableListHeight = qMax(0, AppSettings::windowHeight() - fixedNonListHeight);
        const int naturalRows = qMin(m_list->count(), AppSettings::searchResultLimit());
        const int naturalListHeight = rowHeight * naturalRows + 10;
        listHeight = qMin(naturalListHeight, availableListHeight);
        if (listHeight > 0 && listHeight < 44)
            listHeight = qMin(44, availableListHeight);
        m_list->setFixedHeight(listHeight);
        m_list->setMinimumHeight(listHeight);
        m_list->setMaximumHeight(listHeight);
    } else if (m_list) {
        m_list->setFixedHeight(0);
        m_list->setMinimumHeight(0);
        m_list->setMaximumHeight(0);
    }
    const int compactHeight = searchHeight
        + authHeight
        + listHeight
        + statusHeight
        + footerHeight
        + marginHeight
        + spacingHeight;
    const int desiredHeight = qMax(minimumHeight, compactHeight);
    int height = desiredHeight;
    if (!(m_authPanel && m_authPanel->isVisible()))
        height = qMin(AppSettings::windowHeight(), desiredHeight);
    height = qMax(height, compactHeight);

    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    resize(width, height);
    setFixedSize(width, height);
}

void QuickWindow::reload()
{
    if (m_authMode != AuthMode::None || !m_search->isEnabled())
        return;

    const QString keyword = m_search->text().trimmed();
    if (keyword.isEmpty()) {
        m_queuedSearchText.clear();
        m_list->setVisible(false);
        m_list->clear();
        m_list->setFixedHeight(0);
        m_list->setMinimumHeight(0);
        m_list->setMaximumHeight(0);
        setStatus({});
        updateFooter();
        if (!m_prepareWatcher.isRunning() && !m_authWatcher.isRunning())
            setBusy(false);
        updateWindowSize();
        return;
    }

    startSearch(keyword);
}

void QuickWindow::startSearch(const QString& keyword)
{
    if (!m_itemsReady) {
        m_queuedSearchText = keyword;
        startItemLoad();
        return;
    }

    m_queuedSearchText.clear();
    setBusy(false);

    const QVector<VaultItem> items = filterCachedItems(keyword);
    const int resultLimit = AppSettings::searchResultLimit();
    const QString selectedItemId = currentItem()
        ? currentItem()->data(ItemIdRole).toString()
        : QString();
    const QString preferredItemId = selectedItemId.isEmpty() ? m_preservedItemId : selectedItemId;
    int displayed = 0;

    m_list->setUpdatesEnabled(false);
    if (m_list->viewport())
        m_list->viewport()->setUpdatesEnabled(false);

    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();

        for (const VaultItem& item : items) {
            if (displayed >= resultLimit)
                break;

            auto* row = new QListWidgetItem(displayName(item));
            row->setSizeHint(QSize(0, resultRowHeight()));
            row->setData(ItemIdRole, item.id);
            row->setData(UsernameRole, item.username);
            row->setData(PasswordRole, item.password);
            row->setData(TotpRole, item.totp);
            row->setData(FaviconHostRole, faviconHostForItem(item));
            if (AppSettings::showResultIcons()) {
                row->setIcon(makeResultIcon(item));
                if (AppSettings::fetchFaviconsFromWeb())
                    requestFaviconForRow(row, item);
            }
            m_list->addItem(row);
            ++displayed;
        }
    }

    if (m_list->count() > 0) {
        m_list->setVisible(true);
        m_list->setCurrentRow(0);
        selectItemById(preferredItemId);
    } else {
        m_list->setVisible(false);
    }

    if (m_list->viewport())
        m_list->viewport()->setUpdatesEnabled(true);
    m_list->setUpdatesEnabled(true);
    if (m_list->viewport())
        m_list->viewport()->update();

    if (m_list->count() > 0)
        setStatus({});
    else
        setStatus(uiText("검색 결과 없음", "No matches"));

    updateFooter();
    updateWindowSize();
}

void QuickWindow::handleSearchFinished()
{
}

void QuickWindow::startItemLoad(bool forceRefresh, bool quiet)
{
    if ((!forceRefresh && m_itemsReady) || m_itemsWatcher.isRunning())
        return;

    m_quietItemLoad = quiet;
    if (!quiet)
        setBusy(true, forceRefresh
                ? uiText("Bitwarden 동기화 중...", "Syncing Bitwarden...")
                : uiText("Bitwarden 항목 불러오는 중...", "Loading Bitwarden items..."));

    m_itemsWatcher.setFuture(QtConcurrent::run([this, forceRefresh] {
        ItemsResult result;
        if (forceRefresh) {
            QString syncError;
            if (!m_bw.sync(&syncError)) {
                result.errorMessage = syncError;
                return result;
            }
        }
        result.items = m_bw.listItems(&result.errorMessage);
        return result;
    }));
}

void QuickWindow::handleItemsFinished()
{
    const ItemsResult result = m_itemsWatcher.result();
    const bool quiet = m_quietItemLoad;
    m_quietItemLoad = false;
    const bool wasSyncing = m_syncing;
    m_syncing = false;
    if (!quiet)
        setBusy(false);

    if (!result.errorMessage.isEmpty()) {
        if (!quiet) {
            m_itemsReady = false;
            m_cachedItems.clear();
            m_list->clear();
            m_list->setVisible(false);
            setStatus(result.errorMessage);
            updateWindowSize();
        }
        updateFooter();
        return;
    }

    m_cachedItems = result.items;
    m_itemsReady = true;
    updateAutoSyncTimer();

    const QString keyword = shouldRestorePreservedSearch()
        ? m_preservedSearchText.trimmed()
        : m_search->text().trimmed();
    if (!keyword.isEmpty())
        startSearch(keyword);
    else if (!quiet)
        setStatus({});

    if (wasSyncing) {
        setStatus(uiText("동기화 완료", "Sync complete"));
        QTimer::singleShot(1400, this, [this] {
            if (!m_itemsWatcher.isRunning())
                setStatus({});
        });
    }
    updateFooter();
}

QVector<VaultItem> QuickWindow::filterCachedItems(const QString& keyword) const
{
    QVector<VaultItem> filtered;
    const QString needle = keyword.trimmed().toLower();
    if (needle.isEmpty())
        return filtered;

    const QStringList parts = needle.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const VaultItem& item : m_cachedItems) {
        const QString haystack = itemSearchText(item);
        bool matched = true;
        for (const QString& part : parts) {
            if (!haystack.contains(part)) {
                matched = false;
                break;
            }
        }

        if (matched)
            filtered.push_back(item);
    }

    std::stable_sort(filtered.begin(), filtered.end(), [this, needle](const VaultItem& left, const VaultItem& right) {
        const int leftGroup = itemMatchGroup(left, needle);
        const int rightGroup = itemMatchGroup(right, needle);
        if (leftGroup != rightGroup)
            return leftGroup < rightGroup;

        const int leftRecent = recentItemRank(left.id);
        const int rightRecent = recentItemRank(right.id);
        if (leftRecent != rightRecent)
            return leftRecent < rightRecent;

        return QString::localeAwareCompare(left.name, right.name) < 0;
    });

    return filtered;
}

void QuickWindow::restorePreservedSearch()
{
    if (!shouldRestorePreservedSearch())
        return;

    m_search->setText(m_preservedSearchText);
    m_search->setFocus();

    if (m_itemsReady) {
        startSearch(m_preservedSearchText);
        selectItemById(m_preservedItemId);
    }
}

bool QuickWindow::shouldRestorePreservedSearch() const
{
    const int seconds = AppSettings::reopenRetentionSeconds();
    return seconds > 0
        && m_preservedSearchTimer.isValid()
        && m_preservedSearchTimer.elapsed() <= seconds * 1000
        && !m_preservedSearchText.trimmed().isEmpty();
}

void QuickWindow::preserveSearchState()
{
    m_preservedSearchText = m_search ? m_search->text() : QString();
    m_preservedItemId = currentItem() ? currentItem()->data(ItemIdRole).toString() : QString();

    if (m_preservedSearchText.trimmed().isEmpty()) {
        m_preservedSearchTimer.invalidate();
        return;
    }

    m_preservedSearchTimer.restart();
}

void QuickWindow::recordRecentItem(const QString& itemId)
{
    if (itemId.isEmpty())
        return;

    m_recentItemIds.removeAll(itemId);
    m_recentItemIds.prepend(itemId);
    while (m_recentItemIds.size() > MaxRecentItems)
        m_recentItemIds.removeLast();
}

int QuickWindow::recentItemRank(const QString& itemId) const
{
    const int index = m_recentItemIds.indexOf(itemId);
    return index < 0 ? 100000 : index;
}

const VaultItem* QuickWindow::cachedItemById(const QString& itemId) const
{
    for (const VaultItem& item : m_cachedItems) {
        if (item.id == itemId)
            return &item;
    }
    return nullptr;
}

QString QuickWindow::totpCodeForItem(const QString& itemId, QString* errorMessage) const
{
    const VaultItem* cached = cachedItemById(itemId);
    if (cached && !cached->totp.isEmpty()) {
        const QString code = generateTotpCode(cached->totp, errorMessage);
        if (!code.isEmpty())
            return code;
    }

    return m_bw.getTotp(itemId, errorMessage);
}

void QuickWindow::selectItemById(const QString& itemId)
{
    if (itemId.isEmpty() || !m_list)
        return;

    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item && item->data(ItemIdRole).toString() == itemId) {
            m_list->setCurrentRow(row);
            m_list->scrollToItem(item);
            return;
        }
    }
}

void QuickWindow::moveSelection(int delta)
{
    if (!m_list || !m_list->isVisible() || m_list->count() == 0)
        return;

    int row = m_list->currentRow();
    if (row < 0)
        row = delta > 0 ? -1 : 0;

    row += delta;
    if (row < 0)
        row = m_list->count() - 1;
    else if (row >= m_list->count())
        row = 0;

    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->currentItem());
}

void QuickWindow::prepareVault(bool interactive)
{
    if (m_prepareWatcher.isRunning() || m_authWatcher.isRunning()) {
        if (interactive) {
            m_preloadingVault = false;
            setBusy(true, uiText("Bitwarden 준비 중...", "Preparing Bitwarden..."));
            moveToConfiguredPosition();
            m_search->setEnabled(false);
        }
        return;
    }

    m_preloadingVault = !interactive;
    if (interactive) {
        setBusy(true, uiText("Bitwarden 준비 중...", "Preparing Bitwarden..."));
        moveToConfiguredPosition();
        m_search->setEnabled(false);
    }

    m_prepareWatcher.setFuture(QtConcurrent::run([this] {
        PrepareResult result;
        QString errorMessage;

        result.available = m_bw.isAvailable(&errorMessage);
        if (!result.available) {
            result.errorMessage = errorMessage;
            return result;
        }

        result.statusInfo = m_bw.statusInfo(&errorMessage);
        result.errorMessage = errorMessage;
        return result;
    }));
}

void QuickWindow::handlePrepareFinished()
{
    const PrepareResult result = m_prepareWatcher.result();
    const bool preloading = m_preloadingVault;
    m_preloadingVault = false;

    if (!preloading)
        setBusy(false);

    if (!result.available) {
        if (preloading)
            return;

        hideAuthPanel();
        m_search->setEnabled(false);
        setStatus(result.errorMessage);
        return;
    }

    m_currentUserEmail = result.statusInfo.userEmail;
    m_currentServerUrl = result.statusInfo.serverUrl;

    switch (result.statusInfo.status) {
    case BwClient::VaultStatus::Unauthenticated:
        if (preloading) {
            applySettings();
            hideAuthPanel();
            m_search->setVisible(false);
            m_footer->setVisible(false);
            showAuthPanel(AuthMode::Login);
            moveToConfiguredPosition();
            show();
            raise();
            activateWindow();
            m_authEmail->setFocus();
            return;
        }
        showAuthPanel(AuthMode::Login);
        return;
    case BwClient::VaultStatus::Locked:
        if (preloading) {
            applySettings();
            hideAuthPanel();
            m_search->setVisible(false);
            m_footer->setVisible(false);
            showAuthPanel(AuthMode::Unlock);
            moveToConfiguredPosition();
            show();
            raise();
            activateWindow();
            m_authPassword->setFocus();
            return;
        }
        showAuthPanel(AuthMode::Unlock);
        return;
    case BwClient::VaultStatus::Unlocked:
        if (preloading) {
            startItemLoad(false, true);
            return;
        }

        hideAuthPanel();
        m_search->setEnabled(true);
        m_search->setPlaceholderText(uiText("비밀번호 검색", "Password search"));
        setStatus({});
        m_search->setFocus();
        startItemLoad();
        return;
    case BwClient::VaultStatus::Unknown:
        if (preloading)
            return;

        hideAuthPanel();
        m_search->setEnabled(false);
        setStatus(result.errorMessage.isEmpty()
                ? uiText("Bitwarden 상태를 확인하지 못했습니다.", "Could not check Bitwarden status.")
                : result.errorMessage);
        return;
    }
}

void QuickWindow::showAuthPanel(AuthMode mode)
{
    m_authMode = mode;
    updateAuthTexts();

    const bool loginMode = mode == AuthMode::Login;
    const bool unlockMode = mode == AuthMode::Unlock;
    const bool customServer = m_authServer->currentData().toString() == "custom";
    const QString accountText = m_currentUserEmail.isEmpty()
        ? uiText("로그인된 계정", "Signed-in account")
        : m_currentUserEmail;

    m_authAccount->setText(accountText);
    m_authAccountLabel->setVisible(unlockMode);
    m_authAccount->setVisible(unlockMode);
    m_authServerLabel->setVisible(loginMode);
    m_authServer->setVisible(loginMode);
    m_authCustomServer->setVisible(loginMode && customServer);
    m_authEmailLabel->setVisible(loginMode);
    m_authEmail->setVisible(loginMode);
    m_authMethodLabel->setVisible(loginMode);
    m_authMethod->setVisible(loginMode);
    m_authCodeLabel->setVisible(loginMode);
    m_authCode->setVisible(loginMode);

    m_list->clear();
    m_list->setVisible(false);
    m_authPanel->setVisible(mode != AuthMode::None);
    m_search->setVisible(false);
    m_search->setEnabled(false);
    m_footer->setVisible(false);
    setStatus({});
    updateWindowSize();
    moveToConfiguredPosition();

    if (loginMode && m_authEmail->text().trimmed().isEmpty()) {
        m_authEmail->setFocus();
    } else {
        m_authPassword->setFocus();
        m_authPassword->selectAll();
    }
}

void QuickWindow::hideAuthPanel()
{
    m_authMode = AuthMode::None;
    if (m_authPanel)
        m_authPanel->setVisible(false);
    if (m_authPassword)
        m_authPassword->clear();
    if (m_authCode)
        m_authCode->clear();
    if (m_search) {
        m_search->setVisible(true);
        m_search->setPlaceholderText(uiText("비밀번호 검색", "Password search"));
    }
    if (m_footer)
        m_footer->setVisible(AppSettings::showShortcutHints());
    updateAuthTexts();
    updateWindowSize();
}

QString QuickWindow::selectedAuthServerUrl() const
{
    const QString mode = m_authServer->currentData().toString();
    if (mode == "custom")
        return m_authCustomServer->text().trimmed();

    return defaultServerUrlForMode(mode);
}

void QuickWindow::setBusy(bool busy, const QString& message)
{
    const bool authBusy = m_authMode != AuthMode::None && m_authPanel && m_authPanel->isVisible();

    if (m_authBusyLabel) {
        m_authBusyLabel->setText(authBusy ? message : QString());
        m_authBusyLabel->setVisible(busy && authBusy && !message.isEmpty());
    }

    if (m_authBusyBar)
        m_authBusyBar->setVisible(false);

    if (m_busyBar) {
        m_busyBar->setVisible(busy);
        m_busyBar->setGeometry(0, height() - m_busyBar->height(), width(), m_busyBar->height());
        m_busyBar->raise();
    }

    if (authBusy) {
        if (!busy)
            setStatus({});
    } else if (!message.isEmpty()) {
        setStatus(message);
    } else if (!busy) {
        setStatus({});
    }

    updateWindowSize();
}

void QuickWindow::setAuthInputsEnabled(bool enabled)
{
    m_authServer->setEnabled(enabled);
    m_authCustomServer->setEnabled(enabled);
    m_authEmail->setEnabled(enabled);
    m_authPassword->setEnabled(enabled);
    m_authMethod->setEnabled(enabled);
    m_authCode->setEnabled(enabled);
    m_authCancel->setEnabled(enabled);
    m_authSubmit->setEnabled(enabled);
}

void QuickWindow::syncVault()
{
    if (m_authMode != AuthMode::None)
        return;

    if (m_itemsWatcher.isRunning() || m_authWatcher.isRunning() || m_prepareWatcher.isRunning())
        return;

    if (!m_bw.hasSession()) {
        setStatus(uiText("Bitwarden 잠금 해제가 먼저 필요합니다.", "Unlock Bitwarden first."));
        prepareVault();
        return;
    }

    m_syncing = true;
    updateFooter();
    startItemLoad(true, false);
}

void QuickWindow::updateAutoSyncTimer()
{
    const int minutes = AppSettings::autoSyncIntervalMinutes();
    if (minutes <= 0 || !m_bw.hasSession() || !m_itemsReady) {
        m_refreshTimer.stop();
        return;
    }

    m_refreshTimer.setInterval(minutes * 60 * 1000);
    m_refreshTimer.start();
}

void QuickWindow::requestFaviconForRow(QListWidgetItem* row, const VaultItem& item)
{
    if (!row || !m_faviconManager || !AppSettings::showResultIcons() || !AppSettings::fetchFaviconsFromWeb())
        return;

    const QString host = faviconHostForItem(item);
    if (host.isEmpty())
        return;

    row->setData(FaviconHostRole, host);

    if (m_faviconCache.contains(host)) {
        const QIcon cachedIcon = m_faviconCache.value(host);
        if (!cachedIcon.isNull())
            row->setIcon(cachedIcon);
        return;
    }

    if (m_pendingFavicons.contains(host))
        return;

    const QUrl url = faviconUrlForItem(item);
    if (!url.isValid() || url.host().isEmpty())
        return;

    m_pendingFavicons.insert(host);
    fetchFavicon(host, url, false);
}

void QuickWindow::fetchFavicon(const QString& host, const QUrl& url, bool retriedHttp)
{
    if (!m_faviconManager || host.isEmpty() || !url.isValid())
        return;

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(2500);
    request.setRawHeader("User-Agent", "PeekWarden/0.1");

    QNetworkReply* reply = m_faviconManager->get(request);
    reply->setProperty("peekwardenHost", host);
    reply->setProperty("peekwardenRetriedHttp", retriedHttp);
}

void QuickWindow::handleFaviconFinished(QNetworkReply* reply)
{
    if (!reply)
        return;

    const QString host = reply->property("peekwardenHost").toString();
    const bool retriedHttp = reply->property("peekwardenRetriedHttp").toBool();
    const QUrl requestUrl = reply->url();

    if (!AppSettings::showResultIcons() || !AppSettings::fetchFaviconsFromWeb()) {
        m_pendingFavicons.remove(host);
        reply->deleteLater();
        return;
    }

    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const bool httpOk = !statusCode.isValid() || statusCode.toInt() < 400;
    const bool ok = reply->error() == QNetworkReply::NoError && httpOk;

    if (!ok && !retriedHttp && requestUrl.scheme() == QStringLiteral("https")) {
        QUrl httpUrl = requestUrl;
        httpUrl.setScheme(QStringLiteral("http"));
        reply->deleteLater();
        fetchFavicon(host, httpUrl, true);
        return;
    }

    m_pendingFavicons.remove(host);

    if (!m_bw.hasSession()) {
        reply->deleteLater();
        return;
    }

    QIcon icon;
    if (ok) {
        const QByteArray data = reply->read(256 * 1024);
        QPixmap pixmap;
        if (pixmap.loadFromData(data) && !pixmap.isNull()) {
            pixmap = pixmap.scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            icon = QIcon(pixmap);
        }
    }

    m_faviconCache.insert(host, icon);
    if (!icon.isNull())
        applyFaviconToVisibleRows(host, icon);

    reply->deleteLater();
}

void QuickWindow::applyFaviconToVisibleRows(const QString& host, const QIcon& icon)
{
    if (!m_list || host.isEmpty() || icon.isNull()
        || !AppSettings::showResultIcons() || !AppSettings::fetchFaviconsFromWeb())
        return;

    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (item && item->data(FaviconHostRole).toString() == host)
            item->setIcon(icon);
    }
}

void QuickWindow::scheduleFocusLossCheck()
{
    if (!AppSettings::closeOnFocusLoss() || !isVisible())
        return;

    QTimer::singleShot(0, this, [this] {
        hideIfFocusLost();
    });
    QTimer::singleShot(150, this, [this] {
        hideIfFocusLost();
    });
}

void QuickWindow::hideIfFocusLost()
{
    if (!AppSettings::closeOnFocusLoss() || !isVisible())
        return;

    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return;

    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget && widget->isVisible() && widget->property("peekwardenDetailWindow").toBool())
            return;
    }

    if (QWidget* active = QApplication::activeWindow()) {
        if (active->property("peekwardenDetailWindow").toBool())
            return;
    }

    if (isActiveWindow())
        return;

    QWidget* focusWidget = QApplication::focusWidget();
    if (focusWidget && (focusWidget == this || isAncestorOf(focusWidget)))
        return;

    hide();
}

void QuickWindow::submitAuth()
{
    if (m_authMode == AuthMode::None || m_authWatcher.isRunning())
        return;

    QString password = m_authPassword->text();
    const bool loginMode = m_authMode == AuthMode::Login;
    const QString serverMode = m_authServer->currentData().toString();
    const QString serverUrl = selectedAuthServerUrl();
    const QString email = m_authEmail->text();
    const QString method = m_authMethod->currentData().toString();
    const QString code = m_authCode->text();

    if (loginMode && serverUrl.isEmpty()) {
        setStatus(uiText("사용자 지정 서버 주소를 입력하세요.",
                         "Enter the custom server URL."));
        m_authCustomServer->setFocus();
        password.fill(QChar('\0'));
        return;
    }

    if (loginMode) {
        AppSettings::setBitwardenServerMode(serverMode);
        AppSettings::setCustomBitwardenServerUrl(m_authCustomServer->text());
    }

    setAuthInputsEnabled(false);
    setBusy(true, loginMode
            ? uiText("Bitwarden 로그인 중...", "Logging in to Bitwarden...")
            : uiText("Bitwarden 잠금 해제 중...", "Unlocking Bitwarden..."));

    m_authWatcher.setFuture(QtConcurrent::run([this, loginMode, serverUrl, email, password, method, code] {
        AuthResult result;
        result.loginMode = loginMode;
        QString errorMessage;

        if (loginMode) {
            result.ok = m_bw.configureServer(serverUrl, &errorMessage)
                && m_bw.loginWithCredentials(email, password, method, code, &errorMessage);
        } else {
            result.ok = m_bw.unlockWithPassword(password, &errorMessage);
        }

        result.errorMessage = errorMessage;
        return result;
    }));

    password.fill(QChar('\0'));
}

void QuickWindow::handleAuthFinished()
{
    const AuthResult result = m_authWatcher.result();
    setBusy(false);
    setAuthInputsEnabled(true);
    updateAuthTexts();
    m_authPassword->clear();

    if (!result.ok) {
        setStatus(result.errorMessage);
        m_authPassword->setFocus();
        return;
    }

    m_authCode->clear();
    m_itemsReady = false;
    m_cachedItems.clear();
    m_refreshTimer.stop();
    m_queuedSearchText.clear();
    hideAuthPanel();
    m_search->setEnabled(true);
    m_search->setPlaceholderText(uiText("비밀번호 검색", "Password search"));
    setStatus({});
    m_search->setFocus();
    startItemLoad();
}

void QuickWindow::copyPassword(QListWidgetItem* item)
{
    if (!item)
        return;

    QString errorMessage;
    const QString itemId = item->data(ItemIdRole).toString();
    QString password = item->data(PasswordRole).toString();
    if (password.isEmpty())
        password = m_bw.getPassword(itemId, &errorMessage);

    if (!errorMessage.isEmpty()) {
        setStatus(errorMessage);
        return;
    }

    if (!password.isEmpty()) {
        recordRecentItem(itemId);
        copyTextAndHide(password, true);
    }
}

void QuickWindow::copyUsername(QListWidgetItem* item)
{
    if (!item)
        return;

    const QString cachedUsername = item->data(UsernameRole).toString();
    if (!cachedUsername.isEmpty()) {
        recordRecentItem(item->data(ItemIdRole).toString());
        copyTextAndHide(cachedUsername, false);
        return;
    }

    QString errorMessage;
    QString username = m_bw.getUsername(item->data(ItemIdRole).toString(), &errorMessage);

    if (!errorMessage.isEmpty()) {
        setStatus(errorMessage);
        return;
    }

    if (username.isEmpty())
        username = item->data(UsernameRole).toString();

    if (!username.isEmpty()) {
        recordRecentItem(item->data(ItemIdRole).toString());
        copyTextAndHide(username, false);
    }
}

void QuickWindow::copyTotp(QListWidgetItem* item)
{
    if (!item)
        return;

    QString errorMessage;
    const QString itemId = item->data(ItemIdRole).toString();
    const QString totp = totpCodeForItem(itemId, &errorMessage);

    if (!errorMessage.isEmpty()) {
        setStatus(errorMessage);
        return;
    }

    if (!totp.isEmpty()) {
        recordRecentItem(itemId);
        copyTextAndHide(totp, false);
    }
}

void QuickWindow::showItemDetails(QListWidgetItem* item)
{
    if (!item)
        return;

    const QString itemId = item->data(ItemIdRole).toString();
    if (itemId.isEmpty())
        return;
    recordRecentItem(itemId);

    const QString fallbackTitle = item->text();
    const VaultItem* cachedItem = cachedItemById(itemId);
    const bool hasCachedDetails = cachedItem != nullptr;
    const BwClient::ItemDetails cachedDetails = hasCachedDetails
        ? BwClient::detailsFromVaultItem(*cachedItem)
        : BwClient::ItemDetails();

    struct DetailsResult
    {
        BwClient::ItemDetails details;
        QString errorMessage;
    };

    auto* dialog = new DraggableDialog(nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProperty("peekwardenDetailWindow", true);
    dialog->setWindowTitle(uiText("상세보기", "Details"));
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    dialog->resize(560, 680);
    dialog->setStyleSheet(R"(
        QDialog {
            background: #241a2e;
            border: 1px solid #080a12;
            color: #f6f2ff;
        }
        QLabel {
            color: #f6f2ff;
        }
        #detailHeader {
            font-size: 17px;
            font-weight: 800;
        }
        #detailSubtle {
            color: #cfc3d8;
            font-size: 12px;
        }
        #detailCard {
            background: #171326;
            border: 1px solid #594d64;
            border-radius: 8px;
        }
        #detailSectionTitle {
            color: #ffffff;
            font-size: 13px;
            font-weight: 800;
        }
        #detailLabel {
            color: #d8cedf;
            font-size: 12px;
            font-weight: 700;
        }
        #detailValue {
            color: #ffffff;
            font-size: 13px;
            font-weight: 700;
        }
        #detailLoading {
            color: #f0c7d2;
            font-size: 12px;
        }
        QScrollArea {
            background: transparent;
            border: 0;
        }
        QScrollArea > QWidget > QWidget {
            background: transparent;
        }
        QPlainTextEdit {
            background: #100c1b;
            color: #f6f2ff;
            border: 1px solid #4f435a;
            border-radius: 6px;
            padding: 8px;
            selection-background-color: #1f8cff;
            selection-color: #ffffff;
            font-family: Consolas, "Cascadia Mono", monospace;
            font-size: 12px;
        }
        QProgressBar {
            background: rgba(255, 255, 255, 35);
            border: 0;
            border-radius: 1px;
            max-height: 3px;
        }
        QProgressBar::chunk {
            background: #49a0ff;
            border-radius: 1px;
        }
        QPushButton {
            background: #1f72c7;
            color: #ffffff;
            border: 1px solid #49a0ff;
            border-radius: 6px;
            padding: 6px 10px;
            font-weight: 700;
        }
        QPushButton:hover {
            background: #2484e7;
        }
        QPushButton#detailCloseButton {
            background: #5a5064;
            border-color: #72667d;
        }
        QPushButton#detailCloseButton:hover {
            background: #6a5e75;
        }
    )");

    auto* root = new QVBoxLayout(dialog);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto* headerRow = new QHBoxLayout;
    auto* header = new QLabel(uiText("상세보기", "Details"), dialog);
    header->setObjectName("detailHeader");
    header->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerRow->addWidget(header);
    headerRow->addStretch(1);
    root->addLayout(headerRow);

    auto* scrollArea = new QScrollArea(dialog);
    scrollArea->setWidgetResizable(true);
    auto* content = new QWidget(scrollArea);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    scrollArea->setWidget(content);
    root->addWidget(scrollArea, 1);

    auto* loadingLabel = new QLabel(uiText("상세 정보를 불러오는 중...", "Loading item details..."), content);
    loadingLabel->setObjectName("detailLoading");
    auto* loadingBar = new QProgressBar(content);
    loadingBar->setRange(0, 0);
    loadingBar->setTextVisible(false);
    loadingBar->setFixedHeight(3);
    contentLayout->addWidget(loadingLabel);
    contentLayout->addWidget(loadingBar);
    contentLayout->addStretch(1);

    auto* footerRow = new QHBoxLayout;
    footerRow->addStretch(1);
    auto* closeButton = new QPushButton(uiText("닫기", "Close"), dialog);
    closeButton->setObjectName("detailCloseButton");
    footerRow->addWidget(closeButton);
    root->addLayout(footerRow);

    QObject::connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);

    const auto makeCard = [dialog](QVBoxLayout* parentLayout, const QString& title) {
        auto* card = new QWidget(dialog);
        card->setObjectName("detailCard");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(10);
        if (!title.isEmpty()) {
            auto* titleLabel = new QLabel(title, card);
            titleLabel->setObjectName("detailSectionTitle");
            cardLayout->addWidget(titleLabel);
        }
        parentLayout->addWidget(card);
        return cardLayout;
    };

    const auto addDetailRow = [dialog](QGridLayout* form,
                                   int row,
                                   const QString& label,
                                   const QString& value,
                                   const QString& copyText = {}) {
        if (value.isEmpty())
            return row;

        auto* labelWidget = new QLabel(label, dialog);
        labelWidget->setObjectName("detailLabel");
        auto* valueWidget = new QLabel(value, dialog);
        valueWidget->setObjectName("detailValue");
        valueWidget->setWordWrap(true);
        valueWidget->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* copyButton = new QPushButton(uiText("복사", "Copy"), dialog);
        copyButton->setEnabled(!copyText.isEmpty());
        QObject::connect(copyButton, &QPushButton::clicked, dialog, [copyText] {
            QApplication::clipboard()->setText(copyText);
        });
        form->addWidget(labelWidget, row, 0);
        form->addWidget(valueWidget, row, 1);
        form->addWidget(copyButton, row, 2);
        return row + 1;
    };

    auto* watcher = new QFutureWatcher<DetailsResult>(dialog);
    QObject::connect(watcher, &QFutureWatcher<DetailsResult>::finished, dialog,
        [watcher,
            contentLayout,
            loadingLabel,
            loadingBar,
            makeCard,
            addDetailRow,
            fallbackTitle,
            this,
            itemId] {
            const DetailsResult result = watcher->result();
            loadingLabel->hide();
            loadingBar->hide();
            if (QLayoutItem* item = contentLayout->takeAt(contentLayout->count() - 1))
                delete item;

            if (!result.errorMessage.isEmpty()) {
                loadingLabel->setText(result.errorMessage);
                loadingLabel->show();
                watcher->deleteLater();
                return;
            }

            const BwClient::ItemDetails details = result.details;
            const QString itemTitle = details.name.isEmpty() ? fallbackTitle : details.name;

            QVBoxLayout* titleLayout = makeCard(contentLayout, {});
            auto* titleLabel = new QLabel(itemTitle, titleLayout->parentWidget());
            titleLabel->setObjectName("detailHeader");
            titleLabel->setWordWrap(true);
            titleLayout->addWidget(titleLabel);
            if (!details.id.isEmpty()) {
                auto* idLabel = new QLabel(details.id, titleLayout->parentWidget());
                idLabel->setObjectName("detailSubtle");
                idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
                idLabel->setWordWrap(true);
                titleLayout->addWidget(idLabel);
            }

            QVBoxLayout* credentialsLayout = makeCard(contentLayout, uiText("로그인 정보", "Login credentials"));
            auto* credentialsForm = new QGridLayout;
            credentialsForm->setHorizontalSpacing(10);
            credentialsForm->setVerticalSpacing(9);
            credentialsForm->setColumnStretch(1, 1);
            int row = 0;
            row = addDetailRow(credentialsForm, row, uiText("사용자 이름", "Username"), details.username, details.username);
            row = addDetailRow(credentialsForm,
                row,
                uiText("비밀번호", "Password"),
                details.password.isEmpty() ? QString() : QString(details.password.size(), QChar(0x2022)),
                details.password);
            if (!details.totp.isEmpty()) {
                QWidget* rowParent = credentialsLayout->parentWidget();
                auto* labelWidget = new QLabel(uiText("TOTP", "TOTP"), rowParent);
                labelWidget->setObjectName("detailLabel");
                auto* valueWidget = new QLabel(QString(6, QChar(0x2022)), rowParent);
                valueWidget->setObjectName("detailValue");
                auto* copyButton = new QPushButton(uiText("코드 복사", "Copy code"), rowParent);
                QObject::connect(copyButton, &QPushButton::clicked, copyButton, [this, itemId, copyButton] {
                    QString errorMessage;
                    const QString totp = totpCodeForItem(itemId, &errorMessage);
                    if (totp.isEmpty()) {
                        copyButton->setToolTip(errorMessage);
                        return;
                    }

                    QApplication::clipboard()->setText(totp);
                    copyButton->setText(uiText("복사됨", "Copied"));
                    QTimer::singleShot(1200, copyButton, [copyButton] {
                        copyButton->setText(uiText("코드 복사", "Copy code"));
                    });
                });
                credentialsForm->addWidget(labelWidget, row, 0);
                credentialsForm->addWidget(valueWidget, row, 1);
                credentialsForm->addWidget(copyButton, row, 2);
                ++row;
            }
            if (row == 0)
                row = addDetailRow(credentialsForm, row, uiText("상태", "Status"), uiText("로그인 필드 없음", "No login fields"));
            credentialsLayout->addLayout(credentialsForm);

            if (!details.uris.isEmpty()) {
                QVBoxLayout* urisLayout = makeCard(contentLayout, uiText("웹 사이트", "Websites"));
                auto* uriText = new QLabel(linkedWebsiteText(details.uris), urisLayout->parentWidget());
                uriText->setObjectName("detailValue");
                uriText->setTextFormat(Qt::RichText);
                uriText->setWordWrap(true);
                uriText->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
                uriText->setOpenExternalLinks(false);
                QObject::connect(uriText, &QLabel::linkActivated, uriText, [](const QString& link) {
                    QDesktopServices::openUrl(QUrl(link));
                });
                urisLayout->addWidget(uriText);
            }

            QVBoxLayout* metaLayout = makeCard(contentLayout, uiText("메타데이터", "Metadata"));
            auto* metaForm = new QGridLayout;
            metaForm->setHorizontalSpacing(10);
            metaForm->setVerticalSpacing(9);
            metaForm->setColumnStretch(1, 1);
            int metaRow = 0;
            metaRow = addDetailRow(metaForm, metaRow, uiText("유형", "Type"), details.type);
            metaRow = addDetailRow(metaForm, metaRow, QStringLiteral("ID"), details.id, details.id);
            metaRow = addDetailRow(metaForm, metaRow, uiText("폴더 ID", "Folder ID"), details.folderId, details.folderId);
            metaRow = addDetailRow(metaForm, metaRow, uiText("조직 ID", "Organization ID"), details.organizationId, details.organizationId);
            metaRow = addDetailRow(metaForm, metaRow, uiText("컬렉션", "Collections"), details.collectionIds, details.collectionIds);
            metaRow = addDetailRow(metaForm, metaRow, uiText("생성일", "Created"), details.creationDate);
            metaRow = addDetailRow(metaForm, metaRow, uiText("수정일", "Updated"), details.revisionDate);
            addDetailRow(metaForm, metaRow, uiText("삭제일", "Deleted"), details.deletedDate);
            metaLayout->addLayout(metaForm);

            if (!details.notes.isEmpty()) {
                QVBoxLayout* notesLayout = makeCard(contentLayout, uiText("메모", "Notes"));
                auto* notesEdit = new QPlainTextEdit(details.notes, notesLayout->parentWidget());
                notesEdit->setReadOnly(true);
                notesEdit->setMinimumHeight(90);
                notesLayout->addWidget(notesEdit);
            }

            QVBoxLayout* rawLayout = makeCard(contentLayout, uiText("원본 JSON", "Raw JSON"));
            auto* rawJson = new QPlainTextEdit(details.rawJson, rawLayout->parentWidget());
            rawJson->setReadOnly(true);
            rawJson->setLineWrapMode(QPlainTextEdit::NoWrap);
            rawJson->setMinimumHeight(180);
            rawLayout->addWidget(rawJson);

            contentLayout->addStretch(1);
            watcher->deleteLater();
        });

    watcher->setFuture(QtConcurrent::run([this, itemId, hasCachedDetails, cachedDetails] {
        DetailsResult result;
        if (hasCachedDetails) {
            result.details = cachedDetails;
            return result;
        }

        result.details = m_bw.getItemDetails(itemId, &result.errorMessage);
        return result;
    }));

    QScreen* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    if (screen) {
        const QRect available = screen->availableGeometry();
        const QRect quickRect = frameGeometry();
        constexpr int Gap = 12;
        int x = quickRect.right() + Gap;
        if (x + dialog->width() > available.right() + 1)
            x = quickRect.left() - Gap - dialog->width();
        if (x < available.left())
            x = available.center().x() - dialog->width() / 2;

        const int maxY = qMax(available.top(), available.bottom() - dialog->height() + 1);
        const int y = qBound(available.top(), quickRect.top(), maxY);
        dialog->move(qBound(available.left(), x, qMax(available.left(), available.right() - dialog->width() + 1)), y);
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void QuickWindow::copyTextAndHide(const QString& text, bool clearAfterDelay)
{
    preserveSearchState();
    QApplication::clipboard()->setText(text);
    hide();

    if (!clearAfterDelay)
        return;

    QTimer::singleShot(30000, this, [text] {
        QClipboard* clipboard = QApplication::clipboard();
        if (clipboard && clipboard->text() == text)
            clipboard->clear();
    });
}

void QuickWindow::setStatus(const QString& message)
{
    const QString compactMessage = compactStatusText(message);
    const QString cleanMessage = cleanStatusText(message);

    if (m_search && m_search->isVisible() && m_authMode == AuthMode::None) {
        QString searchMessage = compactMessage;
        constexpr qsizetype MaxSearchPlaceholderLength = 70;
        if (searchMessage.size() > MaxSearchPlaceholderLength)
            searchMessage = searchMessage.left(MaxSearchPlaceholderLength - 3).trimmed() + QStringLiteral("...");
        m_search->setPlaceholderText(compactMessage.isEmpty()
                ? uiText("비밀번호 검색", "Password search")
                : searchMessage);
        m_search->setToolTip(cleanMessage == compactMessage ? QString() : cleanMessage);
        m_status->clear();
        m_status->setVisible(false);
        updateWindowSize();
        return;
    }

    m_status->setText(compactMessage);
    m_status->setToolTip(cleanMessage == compactMessage ? QString() : cleanMessage);
    m_status->setVisible(!compactMessage.isEmpty());
    updateWindowSize();
}

QListWidgetItem* QuickWindow::currentItem() const
{
    return m_list ? m_list->currentItem() : nullptr;
}

void QuickWindow::moveToConfiguredPosition()
{
    QScreen* screen = nullptr;
    if (AppSettings::customWindowPositionEnabled())
        screen = QGuiApplication::screenAt(QPoint(AppSettings::windowPositionX(), AppSettings::windowPositionY()));

    if (!screen)
        screen = QGuiApplication::primaryScreen();

    if (!screen)
        return;

    const QRect available = screen->availableGeometry();
    const QPoint centered = available.center() - rect().center();
    int x = centered.x();
    int y = centered.y();

    if (AppSettings::customWindowPositionEnabled()) {
        x = AppSettings::windowPositionX();
        y = AppSettings::windowPositionY();
    }

    const int maxX = qMax(available.left(), available.right() - width() + 1);
    const int maxY = qMax(available.top(), available.bottom() - height() + 1);
    move(qBound(available.left(), x, maxX), qBound(available.top(), y, maxY));
}
