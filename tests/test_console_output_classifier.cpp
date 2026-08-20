#include "console_output_classifier.h"

#include <QTest>

class ConsoleOutputClassifierTest final : public QObject {
  Q_OBJECT

private slots:
  void classifiesStructuredSeverities();
  void trustsStructuredSeverityOverMessageText();
  void classifiesPlainTextConservatively();
};

void ConsoleOutputClassifierTest::classifiesStructuredSeverities() {
  QCOMPARE(classifyConsoleLine(
               R"({"schema":"dkfz-live-diagnostic-v1","severity":"debug"})"),
           ConsoleLineSeverity::informational);
  QCOMPARE(classifyConsoleLine(
               R"({"schema":"dkfz-live-diagnostic-v1","severity":"info"})"),
           ConsoleLineSeverity::informational);
  QCOMPARE(classifyConsoleLine(R"({"severity":"warning"})"),
           ConsoleLineSeverity::elevated);
  QCOMPARE(classifyConsoleLine(R"({"severity":"error"})"),
           ConsoleLineSeverity::elevated);
  QCOMPARE(classifyConsoleLine(R"({"severity":"fatal"})"),
           ConsoleLineSeverity::elevated);
}

void ConsoleOutputClassifierTest::trustsStructuredSeverityOverMessageText() {
  QCOMPARE(classifyConsoleLine(
               R"({"severity":"info","message":"No fatal error occurred"})"),
           ConsoleLineSeverity::informational);
}

void ConsoleOutputClassifierTest::classifiesPlainTextConservatively() {
  QCOMPARE(classifyConsoleLine("WARNING: engine fallback\n"),
           ConsoleLineSeverity::elevated);
  QCOMPARE(classifyConsoleLine("Runtime information\n"),
           ConsoleLineSeverity::unclassified);
  QCOMPARE(classifyConsoleLine("Step 2 of 4\n"),
           ConsoleLineSeverity::unclassified);
}

QTEST_APPLESS_MAIN(ConsoleOutputClassifierTest)

#include "test_console_output_classifier.moc"
