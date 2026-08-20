#include "console_output_classifier.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

ConsoleLineSeverity classifyConsoleLine(const QByteArray &line) {
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(line.trimmed(), &parseError);
  if (parseError.error == QJsonParseError::NoError && document.isObject()) {
    const QString severity = document.object()
                                 .value(QStringLiteral("severity"))
                                 .toString()
                                 .toLower();
    if (severity == QStringLiteral("debug") ||
        severity == QStringLiteral("info")) {
      return ConsoleLineSeverity::informational;
    }
    if (severity == QStringLiteral("warning") ||
        severity == QStringLiteral("error") ||
        severity == QStringLiteral("fatal")) {
      return ConsoleLineSeverity::elevated;
    }
  }

  const QString text = QString::fromUtf8(line);
  static const QRegularExpression elevatedPattern(
      QStringLiteral(
          "\\b(?:warn(?:ing)?|error|fatal|critical|failed|failure)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  if (text.contains(elevatedPattern)) {
    return ConsoleLineSeverity::elevated;
  }

  static const QRegularExpression informationalPattern(
      QStringLiteral("\\b(?:debug|info)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  if (text.contains(informationalPattern)) {
    return ConsoleLineSeverity::informational;
  }
  return ConsoleLineSeverity::unclassified;
}
