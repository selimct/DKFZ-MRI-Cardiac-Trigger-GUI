#include "console_output_classifier.h"

#include <QTest>

class ConsoleOutputClassifierTest final : public QObject {
  Q_OBJECT

private slots:
  void classifiesStructuredSeverities();
  void treatsInputPauseAsOperationalAttention();
  void trustsStructuredSeverityOverMessageText();
  void classifiesPlainTextConservatively();
  void classifiesRuntimeInputEvents();
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

void ConsoleOutputClassifierTest::treatsInputPauseAsOperationalAttention() {
  QCOMPARE(classifyConsoleLine(
               R"({"schema":"dkfz-live-diagnostic-v1","severity":"warning","code":"input_paused"})"),
           ConsoleLineSeverity::attention);
  QCOMPARE(classifyConsoleLine(
               R"({"severity":"warning","code":"uart_resynchronized"})"),
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

void ConsoleOutputClassifierTest::classifiesRuntimeInputEvents() {
  QCOMPARE(runtimeInputEventForConsoleLine(
               R"({"code":"input_paused","severity":"warning"})"),
           RuntimeInputEvent::paused);
  QCOMPARE(runtimeInputEventForConsoleLine(
               R"({"code":"input_signal_recovered","severity":"info"})"),
           RuntimeInputEvent::recovered);
  QCOMPARE(runtimeInputEventForConsoleLine(
               R"({"code":"runtime_status","input_paused":true})"),
           RuntimeInputEvent::paused);
  QCOMPARE(runtimeInputEventForConsoleLine(
               R"({"code":"runtime_status","input_paused":false})"),
           RuntimeInputEvent::running);
  QCOMPARE(runtimeInputEventForConsoleLine("not structured"),
           RuntimeInputEvent::none);
}

QTEST_APPLESS_MAIN(ConsoleOutputClassifierTest)

#include "test_console_output_classifier.moc"
