#include "console_output_classifier.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

namespace {
QJsonObject structuredObject(const QByteArray &line) {
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(line.trimmed(), &parseError);
  if (parseError.error == QJsonParseError::NoError && document.isObject()) {
    return document.object();
  }
  return {};
}
} // namespace

ConsoleLineSeverity classifyConsoleLine(const QByteArray &line) {
  const QJsonObject object = structuredObject(line);
  if (!object.isEmpty()) {
    const QString code =
        object.value(QStringLiteral("code")).toString().toLower();
    if (code == QStringLiteral("input_paused")) {
      return ConsoleLineSeverity::attention;
    }

    const QString severity =
        object.value(QStringLiteral("severity")).toString().toLower();
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

RuntimeInputEvent runtimeInputEventForConsoleLine(const QByteArray &line) {
  const QJsonObject object = structuredObject(line);
  if (object.isEmpty()) {
    return RuntimeInputEvent::none;
  }

  const QString code =
      object.value(QStringLiteral("code")).toString().toLower();
  if (code == QStringLiteral("input_paused")) {
    return RuntimeInputEvent::paused;
  }
  if (code == QStringLiteral("input_signal_recovered")) {
    return RuntimeInputEvent::recovered;
  }
  if (code == QStringLiteral("runtime_status") &&
      object.contains(QStringLiteral("input_paused"))) {
    return object.value(QStringLiteral("input_paused")).toBool()
               ? RuntimeInputEvent::paused
               : RuntimeInputEvent::running;
  }
  return RuntimeInputEvent::none;
}
