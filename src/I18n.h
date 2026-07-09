#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;
class QTranslator;

namespace I18n
{
QString translate(const char* context, const char* sourceText, const char* koreanFallback = nullptr);
QStringList availableLanguageCodes();
QString languageName(const QString& languageCode);
bool installTranslator(QCoreApplication& app, QTranslator& translator, const QString& languageCode);
bool installQtTranslator(QCoreApplication& app, QTranslator& translator, const QString& languageCode);
}
