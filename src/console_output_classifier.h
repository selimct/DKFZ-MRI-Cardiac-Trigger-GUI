#pragma once

#include <QByteArray>

enum class ConsoleLineSeverity {
  informational,
  elevated,
  unclassified,
};

[[nodiscard]] ConsoleLineSeverity classifyConsoleLine(const QByteArray &line);
