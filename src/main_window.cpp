#include "main_window.h"

#include "console_output_classifier.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr auto kServiceName = "dkfz-native-uart-live";

QColor informationalColor() { return QColor(QStringLiteral("#4d9b58")); }

QColor elevatedColor() { return QColor(QStringLiteral("#d45b5b")); }

QPushButton *makeActionButton(const QString &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return button;
}

QString completionSummary(int exitCode, QProcess::ExitStatus exitStatus,
                          bool cancelled, qint64 elapsed) {
  if (cancelled) {
    return QStringLiteral("Cancelled after %1 ms").arg(elapsed);
  }
  if (exitStatus == QProcess::CrashExit) {
    return QStringLiteral("SSH process crashed after %1 ms").arg(elapsed);
  }
  return QStringLiteral("Finished with exit code %1 after %2 ms")
      .arg(exitCode)
      .arg(elapsed);
}

QColor completionColor(int exitCode, QProcess::ExitStatus exitStatus,
                       bool cancelled) {
  if (cancelled) {
    return QColor(QStringLiteral("#c28b27"));
  }
  if (exitStatus == QProcess::NormalExit && exitCode == 0) {
    return informationalColor();
  }
  return elevatedColor();
}
} // namespace

MainWindow::MainWindow(const SshConnectionOptions &initialConnection,
                       const QString &connectionFile, QWidget *parent)
    : QMainWindow(parent), runner_(this), controlRunner_(this) {
  setWindowTitle(QStringLiteral("Jetson Control"));
  resize(1120, 820);

  auto *central = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(central);

  auto *connectionGroup = new QGroupBox(QStringLiteral("Connection"), central);
  auto *connectionOuterLayout = new QVBoxLayout(connectionGroup);
  auto *connectionLayout = new QFormLayout;

  hostEdit_ = new QLineEdit(connectionGroup);
  hostEdit_->setPlaceholderText(QStringLiteral("jetson or 192.168.1.50"));
  userEdit_ = new QLineEdit(connectionGroup);
  userEdit_->setPlaceholderText(
      QStringLiteral("orin (optional when configured in SSH)"));
  portSpin_ = new QSpinBox(connectionGroup);
  portSpin_->setRange(1, 65535);
  portSpin_->setValue(initialConnection.port);
  identityFileEdit_ =
      new QLineEdit(initialConnection.identityFile, connectionGroup);
  identityFileEdit_->setPlaceholderText(
      QStringLiteral("optional; loaded from connection.ini"));
  identityFileEdit_->setToolTip(QStringLiteral(
      "Private-key path on this computer. A relative identity_file in "
      "connection.ini is resolved next to that file."));
  remoteDirectoryEdit_ =
      new QLineEdit(initialConnection.remoteDirectory.isEmpty()
                        ? QStringLiteral("/home/orin/work/live_stack")
                        : initialConnection.remoteDirectory,
                    connectionGroup);
  remoteDirectoryEdit_->setToolTip(QStringLiteral(
      "The Jetson checkout path. It must match the source side of the "
      "service's BindReadOnlyPaths entry."));

  hostEdit_->setText(initialConnection.host);
  userEdit_->setText(initialConnection.user);
  connectionLayout->addRow(QStringLiteral("Host"), hostEdit_);
  connectionLayout->addRow(QStringLiteral("User"), userEdit_);
  connectionLayout->addRow(QStringLiteral("Port"), portSpin_);
  connectionLayout->addRow(QStringLiteral("SSH key"), identityFileEdit_);
  connectionLayout->addRow(QStringLiteral("Remote project root"),
                           remoteDirectoryEdit_);
  connectionOuterLayout->addLayout(connectionLayout);

  auto *connectionActions = new QHBoxLayout;
  probeButton_ =
      makeActionButton(QStringLiteral("Check availability"), connectionGroup);
  healthButton_ =
      makeActionButton(QStringLiteral("System information"), connectionGroup);
  connectionActions->addWidget(probeButton_);
  connectionActions->addWidget(healthButton_);
  connectionOuterLayout->addLayout(connectionActions);

  operationsTabs_ = new QTabWidget(central);

  auto *servicePage = new QWidget(operationsTabs_);
  auto *serviceLayout = new QVBoxLayout(servicePage);
  serviceAvailabilityLabel_ = new QLabel(
      QStringLiteral(
          "Availability has not been checked. Service actions are disabled."),
      servicePage);
  serviceAvailabilityLabel_->setWordWrap(true);
  serviceSafetyCheck_ =
      new QCheckBox(QStringLiteral("I reviewed the installed configuration and "
                                   "verified the connected hardware"),
                    servicePage);
  auto *serviceActions = new QHBoxLayout;
  serviceStatusButton_ =
      makeActionButton(QStringLiteral("Status"), servicePage);
  serviceReloadButton_ =
      makeActionButton(QStringLiteral("Reload unit"), servicePage);
  serviceStartButton_ = makeActionButton(QStringLiteral("Start"), servicePage);
  serviceStopButton_ = makeActionButton(QStringLiteral("Stop"), servicePage);
  serviceRestartButton_ =
      makeActionButton(QStringLiteral("Restart"), servicePage);
  serviceLogsButton_ =
      makeActionButton(QStringLiteral("Follow log"), servicePage);
  serviceActions->addWidget(serviceStatusButton_);
  serviceActions->addWidget(serviceReloadButton_);
  serviceActions->addWidget(serviceStartButton_);
  serviceActions->addWidget(serviceStopButton_);
  serviceActions->addWidget(serviceRestartButton_);
  serviceActions->addWidget(serviceLogsButton_);
  serviceLayout->addWidget(serviceAvailabilityLabel_);
  serviceLayout->addWidget(serviceSafetyCheck_);
  serviceLayout->addLayout(serviceActions);
  serviceLayout->addStretch();
  operationsTabs_->addTab(servicePage, QStringLiteral("Installed service"));

  auto *diagnosticPage = new QWidget(operationsTabs_);
  auto *diagnosticLayout = new QVBoxLayout(diagnosticPage);
  diagnosticAvailabilityLabel_ = new QLabel(
      QStringLiteral(
          "Availability has not been checked. Direct actions are disabled."),
      diagnosticPage);
  diagnosticAvailabilityLabel_->setWordWrap(true);

  auto *diagnosticColumns = new QHBoxLayout;
  auto *deploymentForm = new QFormLayout;
  auto *runtimeForm = new QFormLayout;
  deploymentForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  runtimeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  modelNameEdit_ = new QLineEdit(QStringLiteral("target_r"), diagnosticPage);
  modelNameEdit_->setToolTip(QStringLiteral(
      "The modelname declared by the selected training configuration."));
  modelDirectoryEdit_ =
      new QLineEdit(QStringLiteral("models/target_r"), diagnosticPage);
  modelDirectoryEdit_->setToolTip(QStringLiteral(
      "Exact source directory beneath the remote project root. Use this for "
      "alternate roots such as models_noCV/target_r."));
  variantCombo_ = new QComboBox(diagnosticPage);
  variantCombo_->addItems({QStringLiteral("best"), QStringLiteral("tuned")});
  preparationModeCombo_ = new QComboBox(diagnosticPage);
  preparationModeCombo_->addItem(
      QStringLiteral("Deploy existing ONNX → TensorRT"),
      QStringLiteral("jetson-only"));
  preparationModeCombo_->addItem(
      QStringLiteral("Complete: PTH → ONNX → TensorRT"),
      QStringLiteral("complete"));
  preparationModeCombo_->setToolTip(QStringLiteral(
      "Both modes run on the Jetson. Complete mode also exports ONNX and runs "
      "PyTorch/ONNX parity there."));
  outputModeCombo_ = new QComboBox(diagnosticPage);
  outputModeCombo_->addItems({QStringLiteral("none"), QStringLiteral("jsonl"),
                              QStringLiteral("gpio"),
                              QStringLiteral("gpio+jsonl")});
  outputModeCombo_->setToolTip(
      QStringLiteral("Use none for the safest live diagnostic. GPIO modes can "
                     "create physical pulses."));
  configPathEdit_ = new QLineEdit(
      QStringLiteral("config/native_uart_live.conf"), diagnosticPage);
  sessionLogPathEdit_ = new QLineEdit(diagnosticPage);
  sessionLogPathEdit_->setPlaceholderText(
      QStringLiteral("output/live-session.log (optional)"));
  sessionLogPathEdit_->setToolTip(
      QStringLiteral("Append stdout and stderr to this path beneath the remote "
                     "project root."));
  rawCapturePathEdit_ = new QLineEdit(diagnosticPage);
  rawCapturePathEdit_->setPlaceholderText(
      QStringLiteral("output/session.bin (optional)"));
  rawCapturePathEdit_->setToolTip(
      QStringLiteral("Raw UART byte capture. Empty explicitly disables "
                     "capture, overriding the config."));
  emitProbabilitiesCheck_ =
      new QCheckBox(QStringLiteral("Emit probability records"), diagnosticPage);
  emitProbabilitiesCheck_->setEnabled(false);
  gpioSafetyCheck_ =
      new QCheckBox(QStringLiteral("I verified the GPIO line, polarity, "
                                   "voltage, and connected hardware"),
                    diagnosticPage);
  gpioSafetyCheck_->setEnabled(false);

  deploymentForm->addRow(QStringLiteral("Model name"), modelNameEdit_);
  deploymentForm->addRow(QStringLiteral("Model source directory"),
                         modelDirectoryEdit_);
  deploymentForm->addRow(QStringLiteral("Variant"), variantCombo_);
  deploymentForm->addRow(QStringLiteral("Jetson preparation"),
                         preparationModeCombo_);

  runtimeForm->addRow(QStringLiteral("Trigger output"), outputModeCombo_);
  runtimeForm->addRow(QStringLiteral("Runtime config"), configPathEdit_);
  runtimeForm->addRow(QStringLiteral("Combined session log"),
                      sessionLogPathEdit_);
  runtimeForm->addRow(QStringLiteral("Raw UART capture"), rawCapturePathEdit_);

  diagnosticColumns->addLayout(deploymentForm, 1);
  diagnosticColumns->addSpacing(18);
  diagnosticColumns->addLayout(runtimeForm, 1);

  auto *diagnosticChecks = new QHBoxLayout;
  diagnosticChecks->addWidget(emitProbabilitiesCheck_, 1);
  diagnosticChecks->addWidget(gpioSafetyCheck_, 1);

  auto *diagnosticActions = new QHBoxLayout;
  prepareModelButton_ = makeActionButton(
      QStringLiteral("Prepare + activate model"), diagnosticPage);
  prepareModelButton_->setToolTip(QStringLiteral(
      "Runs prepare_model.sh with the exact source directory and replaces "
      "deploy/model only after validation. The systemd service must be "
      "stopped."));
  validateRuntimeButton_ = makeActionButton(
      QStringLiteral("Validate config + engine"), diagnosticPage);
  startInferenceButton_ = makeActionButton(
      QStringLiteral("Start direct inference"), diagnosticPage);
  stopInferenceButton_ =
      makeActionButton(QStringLiteral("Stop direct inference"), diagnosticPage);
  rebuildRuntimeButton_ = makeActionButton(
      QStringLiteral("Build / rebuild runtime"), diagnosticPage);
  diagnosticActions->addWidget(prepareModelButton_);
  diagnosticActions->addWidget(validateRuntimeButton_);
  diagnosticActions->addWidget(startInferenceButton_);
  diagnosticActions->addWidget(stopInferenceButton_);
  diagnosticActions->addWidget(rebuildRuntimeButton_);

  diagnosticLayout->addWidget(diagnosticAvailabilityLabel_);
  diagnosticLayout->addLayout(diagnosticColumns);
  diagnosticLayout->addLayout(diagnosticChecks);
  diagnosticLayout->addLayout(diagnosticActions);
  operationsTabs_->addTab(diagnosticPage,
                          QStringLiteral("Model deployment / direct"));

  auto *commandGroup =
      new QGroupBox(QStringLiteral("Custom non-interactive command"), central);
  auto *commandLayout = new QHBoxLayout(commandGroup);
  commandCombo_ = new QComboBox(commandGroup);
  commandCombo_->setEditable(true);
  commandCombo_->setInsertPolicy(QComboBox::NoInsert);
  commandCombo_->lineEdit()->setPlaceholderText(
      QStringLiteral("Enter a command"));
  runButton_ = new QPushButton(QStringLiteral("Run"), commandGroup);
  cancelButton_ = new QPushButton(QStringLiteral("Cancel"), commandGroup);
  cancelButton_->setEnabled(false);
  auto *clearButton =
      new QPushButton(QStringLiteral("Clear output"), commandGroup);
  commandLayout->addWidget(commandCombo_, 1);
  commandLayout->addWidget(runButton_);
  commandLayout->addWidget(cancelButton_);
  commandLayout->addWidget(clearButton);

  console_ = new QPlainTextEdit(central);
  console_->setReadOnly(true);
  console_->setLineWrapMode(QPlainTextEdit::NoWrap);
  console_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  console_->document()->setMaximumBlockCount(20000);

  statusLabel_ = new QLabel(central);
  const QString sshStatus = runner_.sshExecutable().isEmpty()
                                ? QStringLiteral("not found")
                                : runner_.sshExecutable();
  statusLabel_->setText(
      connectionFile.isEmpty()
          ? QStringLiteral(
                "Ready — SSH executable: %1; no connection.ini loaded")
                .arg(sshStatus)
          : QStringLiteral("Ready — loaded %1; SSH executable: %2")
                .arg(connectionFile, sshStatus));

  mainLayout->addWidget(connectionGroup);
  mainLayout->addWidget(operationsTabs_);
  mainLayout->addWidget(commandGroup);
  mainLayout->addWidget(console_, 1);
  mainLayout->addWidget(statusLabel_);
  setCentralWidget(central);

  connect(probeButton_, &QPushButton::clicked, this, [this] {
    const DiagnosticRuntimeOptions options = diagnosticOptions();
    const QString error = validateDiagnosticOptions(options);
    if (!error.isEmpty()) {
      appendConsole(QStringLiteral("Error: %1\n").arg(error),
                    QColor(QStringLiteral("#d45b5b")));
      statusLabel_->setText(error);
      return;
    }
    runCommand(QStringLiteral("Availability check"),
               availabilityProbeCommand(options), TaskKind::availabilityProbe);
  });
  connect(healthButton_, &QPushButton::clicked, this, [this] {
    runCommand(QStringLiteral("System information"),
               QStringLiteral(
                   "printf '%s\\n' '--- system ---'; uname -a; "
                   "printf '%s\\n' '--- uptime ---'; uptime; "
                   "printf '%s\\n' '--- storage ---'; df -h .; "
                   "printf '%s\\n' '--- CUDA/TensorRT ---'; "
                   "command -v nvidia-smi >/dev/null && nvidia-smi || true; "
                   "if command -v trtexec >/dev/null; then command -v trtexec; "
                   "elif test -x /usr/src/tensorrt/bin/trtexec; then "
                   "printf '%s\\n' /usr/src/tensorrt/bin/trtexec; "
                   "else printf '%s\\n' 'trtexec not found'; fi"));
  });

  connect(serviceStatusButton_, &QPushButton::clicked, this, [this] {
    runCommand(
        QStringLiteral("Service status"),
        QStringLiteral("systemctl --no-pager --full status %1; "
                       "code=$?; test \"$code\" -eq 0 || test \"$code\" -eq 3")
            .arg(kServiceName));
  });
  connect(serviceReloadButton_, &QPushButton::clicked, this, [this] {
    runCommand(QStringLiteral("Reload systemd unit"),
               QStringLiteral("sudo -n systemctl daemon-reload && "
                              "systemctl show --property=NeedDaemonReload %1")
                   .arg(kServiceName),
               TaskKind::serviceMutation);
  });
  connect(serviceStartButton_, &QPushButton::clicked, this, [this] {
    runCommand(QStringLiteral("Start service"),
               QStringLiteral("sudo -n systemctl start %1 && "
                              "systemctl --no-pager --full status %1")
                   .arg(kServiceName),
               TaskKind::serviceMutation);
  });
  connect(serviceStopButton_, &QPushButton::clicked, this, [this] {
    runCommand(
        QStringLiteral("Stop service"),
        QStringLiteral(
            "sudo -n systemctl stop %1; "
            "stop_code=$?; systemctl --no-pager --full status %1 || true; "
            "exit \"$stop_code\"")
            .arg(kServiceName),
        TaskKind::serviceMutation);
  });
  connect(serviceRestartButton_, &QPushButton::clicked, this, [this] {
    runCommand(QStringLiteral("Restart service"),
               QStringLiteral("sudo -n systemctl restart %1 && "
                              "systemctl --no-pager --full status %1")
                   .arg(kServiceName),
               TaskKind::serviceMutation);
  });
  connect(serviceLogsButton_, &QPushButton::clicked, this, [this] {
    runCommand(
        QStringLiteral("Follow service log"),
        QStringLiteral("journalctl --follow --lines=100 --output=cat --unit=%1")
            .arg(kServiceName));
  });

  connect(prepareModelButton_, &QPushButton::clicked, this, [this] {
    const auto options = diagnosticOptions();
    const QString error = validateDiagnosticOptions(options);
    if (!error.isEmpty()) {
      statusLabel_->setText(error);
      return;
    }
    runCommand(QStringLiteral("Prepare model on Jetson"),
               prepareModelCommand(options), TaskKind::prepareModel);
  });
  connect(validateRuntimeButton_, &QPushButton::clicked, this, [this] {
    const auto options = diagnosticOptions();
    const QString error = validateDiagnosticOptions(options);
    if (!error.isEmpty()) {
      statusLabel_->setText(error);
      return;
    }
    runCommand(QStringLiteral("Validate direct runtime"),
               validateRuntimeCommand(options), TaskKind::validateRuntime);
  });
  connect(startInferenceButton_, &QPushButton::clicked, this, [this] {
    const auto options = diagnosticOptions();
    const QString error = validateDiagnosticOptions(options);
    if (!error.isEmpty()) {
      statusLabel_->setText(error);
      return;
    }
    if (options.outputMode.contains(QStringLiteral("gpio")) &&
        !gpioSafetyCheck_->isChecked()) {
      statusLabel_->setText(
          QStringLiteral("GPIO safety confirmation is required."));
      return;
    }
    runCommand(QStringLiteral("Direct inference"),
               startDirectRuntimeCommand(options), TaskKind::directInference);
  });
  connect(stopInferenceButton_, &QPushButton::clicked, this,
          &MainWindow::stopDirectInference);
  connect(rebuildRuntimeButton_, &QPushButton::clicked, this, [this] {
    runCommand(
        QStringLiteral("Build or rebuild runtime"),
        QStringLiteral(
            "if test -f build_data_ingest/CMakeCache.txt; then "
            "bash run/rebuild_native.sh --target native_uart_live_runtime; "
            "else bash run/build_data_ingest.sh --tensorrt on --test; fi"),
        TaskKind::rebuildRuntime);
  });

  connect(runButton_, &QPushButton::clicked, this, [this] {
    const QString command = commandCombo_->currentText().trimmed();
    if (!command.isEmpty()) {
      addCommandToHistory(command);
    }
    runCommand(QStringLiteral("Custom command"), command);
  });
  connect(commandCombo_->lineEdit(), &QLineEdit::returnPressed, runButton_,
          &QPushButton::click);
  connect(cancelButton_, &QPushButton::clicked, this, [this] {
    if (activeTask_ == TaskKind::directInference) {
      stopDirectInference();
    } else {
      runner_.cancel();
    }
  });
  connect(clearButton, &QPushButton::clicked, console_, &QPlainTextEdit::clear);

  const auto invalidate = [this] { invalidateAvailability(); };
  connect(hostEdit_, &QLineEdit::textChanged, this, invalidate);
  connect(userEdit_, &QLineEdit::textChanged, this, invalidate);
  connect(portSpin_, qOverload<int>(&QSpinBox::valueChanged), this, invalidate);
  connect(identityFileEdit_, &QLineEdit::textChanged, this, invalidate);
  connect(remoteDirectoryEdit_, &QLineEdit::textChanged, this, invalidate);
  connect(modelNameEdit_, &QLineEdit::textChanged, this,
          [this, invalidate](const QString &name) {
            if (modelDirectoryFollowsName_) {
              modelDirectoryEdit_->setText(
                  QStringLiteral("models/%1").arg(name.trimmed()));
            }
            invalidate();
          });
  connect(modelDirectoryEdit_, &QLineEdit::textEdited, this,
          [this] { modelDirectoryFollowsName_ = false; });
  connect(modelDirectoryEdit_, &QLineEdit::textChanged, this, invalidate);
  connect(variantCombo_, &QComboBox::currentTextChanged, this, invalidate);
  connect(preparationModeCombo_,
          qOverload<int>(&QComboBox::currentIndexChanged), this,
          [invalidate] { invalidate(); });
  connect(configPathEdit_, &QLineEdit::textChanged, this, invalidate);
  connect(sessionLogPathEdit_, &QLineEdit::textChanged, this,
          [this] { updateActionAvailability(); });
  connect(rawCapturePathEdit_, &QLineEdit::textChanged, this, [this] {
    capabilities_.directRuntimeReady = false;
    if (capabilities_.known) {
      diagnosticAvailabilityLabel_->setText(QStringLiteral(
          "Direct runtime parameters changed. Validate again before starting "
          "inference."));
    }
    updateActionAvailability();
  });
  connect(emitProbabilitiesCheck_, &QCheckBox::toggled, this, [this] {
    capabilities_.directRuntimeReady = false;
    if (capabilities_.known) {
      diagnosticAvailabilityLabel_->setText(QStringLiteral(
          "Direct runtime parameters changed. Validate again before starting "
          "inference."));
    }
    updateActionAvailability();
  });
  connect(gpioSafetyCheck_, &QCheckBox::toggled, this,
          [this] { updateActionAvailability(); });
  connect(serviceSafetyCheck_, &QCheckBox::toggled, this,
          [this] { updateActionAvailability(); });
  connect(outputModeCombo_, &QComboBox::currentTextChanged, this,
          [this] { updateDiagnosticModeControls(); });

  connect(&runner_, &RemoteCommandRunner::started, this,
          [this](const QString &destination, const QString &command) {
            commandTimer_.restart();
            activeStandardOutput_.clear();
            mainStandardOutputDisplayBuffer_.clear();
            mainStandardErrorDisplayBuffer_.clear();
            QString displayCommand = command;
            if (activeTask_ == TaskKind::availabilityProbe) {
              displayCommand = QStringLiteral("[availability preflight]");
            } else if (activeTask_ == TaskKind::directInference) {
              displayCommand = QStringLiteral("[managed direct inference]");
            }
            appendConsole(
                QStringLiteral("\n[%1] %2 $ %3\n")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                         destination, displayCommand),
                QColor(QStringLiteral("#4f8dd6")));
            if (activeTask_ == TaskKind::directInference) {
              capabilities_.directInferenceActive = true;
            }
            updateActionAvailability();
          });
  connect(&runner_, &RemoteCommandRunner::standardOutputReceived, this,
          [this](const QByteArray &data) {
            activeStandardOutput_.append(data);
            if (activeTask_ != TaskKind::availabilityProbe) {
              appendClassifiedConsoleBytes(mainStandardOutputDisplayBuffer_,
                                           data);
            }
          });
  connect(&runner_, &RemoteCommandRunner::standardErrorReceived, this,
          [this](const QByteArray &data) {
            appendClassifiedConsoleBytes(mainStandardErrorDisplayBuffer_, data);
          });
  connect(&runner_, &RemoteCommandRunner::failedToStart, this,
          [this](const QString &message) {
            appendConsole(QStringLiteral("Error: %1\n").arg(message),
                          QColor(QStringLiteral("#d45b5b")));
            statusLabel_->setText(message);
            activeTask_ = TaskKind::none;
            updateActionAvailability();
          });
  connect(&runner_, &RemoteCommandRunner::finished, this,
          &MainWindow::finishMainCommand);

  connect(&controlRunner_, &RemoteCommandRunner::started, this,
          [this](const QString &destination, const QString &) {
            controlTimer_.restart();
            controlStandardOutputDisplayBuffer_.clear();
            controlStandardErrorDisplayBuffer_.clear();
            appendConsole(
                QStringLiteral("\n[%1] %2 $ %3\n")
                    .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                         destination,
                         QStringLiteral("[managed direct-inference stop]")),
                QColor(QStringLiteral("#4f8dd6")));
            updateActionAvailability();
          });
  connect(&controlRunner_, &RemoteCommandRunner::standardOutputReceived, this,
          [this](const QByteArray &data) {
            appendClassifiedConsoleBytes(controlStandardOutputDisplayBuffer_,
                                         data);
          });
  connect(&controlRunner_, &RemoteCommandRunner::standardErrorReceived, this,
          [this](const QByteArray &data) {
            appendClassifiedConsoleBytes(controlStandardErrorDisplayBuffer_,
                                         data);
          });
  connect(&controlRunner_, &RemoteCommandRunner::failedToStart, this,
          [this](const QString &message) {
            appendConsole(QStringLiteral("Error: %1\n").arg(message),
                          QColor(QStringLiteral("#d45b5b")));
            statusLabel_->setText(message);
            updateActionAvailability();
          });
  connect(
      &controlRunner_, &RemoteCommandRunner::finished, this,
      [this](int exitCode, QProcess::ExitStatus exitStatus, bool cancelled) {
        flushClassifiedConsoleBytes(controlStandardOutputDisplayBuffer_);
        flushClassifiedConsoleBytes(controlStandardErrorDisplayBuffer_);
        const qint64 elapsed =
            controlTimer_.isValid() ? controlTimer_.elapsed() : 0;
        const QString summary =
            completionSummary(exitCode, exitStatus, cancelled, elapsed);
        appendConsole(QStringLiteral("\n[%1]\n").arg(summary),
                      completionColor(exitCode, exitStatus, cancelled));
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
          capabilities_.directInferenceActive = false;
        }
        statusLabel_->setText(summary);
        updateActionAvailability();
      });

  updateDiagnosticModeControls();
  invalidateAvailability();
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (!runner_.isRunning() && !controlRunner_.isRunning()) {
    event->accept();
    return;
  }

  if (controlRunner_.isRunning()) {
    QMessageBox::information(
        this, QStringLiteral("Stop request in progress"),
        QStringLiteral("Wait for the direct-inference stop request to finish "
                       "before closing."));
    event->ignore();
    return;
  }

  if (activeTask_ == TaskKind::directInference) {
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Inference is running"),
        QStringLiteral(
            "Stop direct inference before closing the application?"));
    if (answer == QMessageBox::Yes) {
      stopDirectInference();
    }
    event->ignore();
    return;
  }

  const QString text = QStringLiteral(
      "Cancel the active SSH command and close the application?");
  const auto answer =
      QMessageBox::question(this, QStringLiteral("Command is running"), text);
  if (answer == QMessageBox::Yes) {
    runner_.cancel();
    controlRunner_.cancel();
    event->accept();
  } else {
    event->ignore();
  }
}

SshConnectionOptions MainWindow::connectionOptions() const {
  return {
      .host = hostEdit_->text(),
      .user = userEdit_->text(),
      .port = static_cast<quint16>(portSpin_->value()),
      .identityFile = identityFileEdit_->text(),
      .remoteDirectory = remoteDirectoryEdit_->text(),
  };
}

DiagnosticRuntimeOptions MainWindow::diagnosticOptions() const {
  return {
      .modelName = modelNameEdit_->text().trimmed(),
      .modelDirectory = modelDirectoryEdit_->text().trimmed(),
      .variant = variantCombo_->currentText(),
      .preparationMode = preparationModeCombo_->currentData().toString(),
      .outputMode = outputModeCombo_->currentText(),
      .configPath = configPathEdit_->text().trimmed(),
      .sessionLogPath = sessionLogPathEdit_->text().trimmed(),
      .rawCapturePath = rawCapturePathEdit_->text().trimmed(),
      .emitProbabilities = emitProbabilitiesCheck_->isChecked(),
  };
}

void MainWindow::runCommand(const QString &label, const QString &command,
                            TaskKind kind) {
  if (runner_.isRunning()) {
    statusLabel_->setText(
        QStringLiteral("Stop or cancel the active command first."));
    return;
  }

  activeTask_ = kind;
  statusLabel_->setText(QStringLiteral("Starting %1…").arg(label));
  runner_.start(connectionOptions(), command);
  updateActionAvailability();
}

void MainWindow::stopDirectInference() {
  if (controlRunner_.isRunning()) {
    statusLabel_->setText(QStringLiteral("A stop request is already running."));
    return;
  }
  controlRunner_.start(connectionOptions(), stopDirectRuntimeCommand());
  updateActionAvailability();
}

void MainWindow::appendConsole(const QString &text, const QColor &color) {
  QTextCursor cursor = console_->textCursor();
  cursor.movePosition(QTextCursor::End);
  QTextCharFormat format;
  format.setForeground(color);
  cursor.insertText(text, format);
  console_->setTextCursor(cursor);
  console_->ensureCursorVisible();
}

void MainWindow::appendConsoleBytes(const QByteArray &data,
                                    const QColor &color) {
  appendConsole(QString::fromUtf8(data), color);
}

void MainWindow::appendClassifiedConsoleBytes(QByteArray &pending,
                                              const QByteArray &data) {
  pending.append(data);
  while (true) {
    const qsizetype newline = pending.indexOf('\n');
    if (newline < 0) {
      break;
    }

    const QByteArray line = pending.left(newline + 1);
    pending.remove(0, newline + 1);
    const ConsoleLineSeverity severity = classifyConsoleLine(line);
    if (severity == ConsoleLineSeverity::elevated) {
      appendConsoleBytes(line, elevatedColor());
    } else if (severity == ConsoleLineSeverity::informational) {
      appendConsoleBytes(line, informationalColor());
    } else {
      appendConsoleBytes(line, palette().color(QPalette::Text));
    }
  }
}

void MainWindow::flushClassifiedConsoleBytes(QByteArray &pending) {
  if (pending.isEmpty()) {
    return;
  }
  QByteArray finalLine = pending;
  pending.clear();
  finalLine.append('\n');
  appendClassifiedConsoleBytes(pending, finalLine);
}

void MainWindow::addCommandToHistory(const QString &command) {
  const int existing = commandCombo_->findText(command);
  if (existing >= 0) {
    commandCombo_->removeItem(existing);
  }
  commandCombo_->insertItem(0, command);
  commandCombo_->setCurrentIndex(0);

  constexpr int kMaximumHistory = 20;
  while (commandCombo_->count() > kMaximumHistory) {
    commandCombo_->removeItem(commandCombo_->count() - 1);
  }
}

void MainWindow::invalidateAvailability() {
  capabilities_ = {};
  serviceSafetyCheck_->setChecked(false);
  gpioSafetyCheck_->setChecked(false);
  serviceAvailabilityLabel_->setText(QStringLiteral(
      "Availability has not been checked. Service actions are disabled."));
  diagnosticAvailabilityLabel_->setText(QStringLiteral(
      "Availability has not been checked. Direct actions are disabled."));
  updateActionAvailability();
}

void MainWindow::applyAvailabilityProbe(const QByteArray &output) {
  const auto present = [&output](const char *marker) {
    return output.contains(QByteArray(marker) + "=yes");
  };

  capabilities_.known = true;
  capabilities_.service = present("__JCG_SERVICE__");
  capabilities_.serviceActive = present("__JCG_SERVICE_ACTIVE__");
  capabilities_.serviceEnabled = present("__JCG_SERVICE_ENABLED__");
  capabilities_.serviceLinkedToCheckout = present("__JCG_SERVICE_LINKED__");
  capabilities_.serviceContract = present("__JCG_SERVICE_CONTRACT__");
  capabilities_.serviceReady = present("__JCG_SERVICE_READY__");
  capabilities_.runtime = present("__JCG_RUNTIME__");
  capabilities_.serviceConfig = present("__JCG_SERVICE_CONFIG__");
  capabilities_.config = present("__JCG_CONFIG__");
  capabilities_.manifest = present("__JCG_MANIFEST__");
  capabilities_.engine = present("__JCG_ENGINE__");
  capabilities_.activeSlotMetadata = present("__JCG_SLOT_METADATA__");
  capabilities_.activeSelectionMatches = present("__JCG_ACTIVE_SELECTION__");
  capabilities_.directRuntimeReady = present("__JCG_DIRECT_READY__");
  capabilities_.prepareScript = present("__JCG_PREPARE__");
  capabilities_.prepareInputs = present("__JCG_PREPARE_INPUTS__");
  capabilities_.rebuildScript = present("__JCG_REBUILD__");
  capabilities_.directInferenceActive = present("__JCG_DIRECT_ACTIVE__");

  const auto value = [&output](const char *marker) {
    const QByteArray prefix = QByteArray(marker) + '=';
    const qsizetype start = output.indexOf(prefix);
    if (start < 0) {
      return QString{};
    }
    const qsizetype valueStart = start + prefix.size();
    qsizetype end = output.indexOf('\n', valueStart);
    if (end < 0) {
      end = output.size();
    }
    return QString::fromUtf8(output.mid(valueStart, end - valueStart))
        .trimmed();
  };
  capabilities_.activeModel = value("__JCG_ACTIVE_MODEL__");
  capabilities_.activeVariant = value("__JCG_ACTIVE_VARIANT__");
  capabilities_.activeSourceDirectory = value("__JCG_ACTIVE_SOURCE__");
  capabilities_.serviceState = value("__JCG_SERVICE_STATE__");

  if (capabilities_.serviceReady) {
    serviceAvailabilityLabel_->setText(
        QStringLiteral("The checkout-linked unit is installed, %1 and %2. "
                       "Its bind mount, service account, state directory, "
                       "configuration, and active model slot passed the "
                       "non-I/O preflight.")
            .arg(capabilities_.serviceState, capabilities_.serviceEnabled
                                                 ? QStringLiteral("enabled")
                                                 : QStringLiteral("disabled")));
  } else if (capabilities_.service) {
    QStringList reasons;
    if (!capabilities_.serviceLinkedToCheckout) {
      reasons.append(QStringLiteral(
          "the installed unit is not linked to this checkout's deploy unit"));
    }
    if (!capabilities_.serviceContract) {
      reasons.append(QStringLiteral(
          "the bind path/account/groups/state directory/config sandbox is "
          "invalid, or systemd needs daemon-reload"));
    }
    if (!capabilities_.runtime || !capabilities_.serviceConfig ||
        !capabilities_.manifest || !capabilities_.engine ||
        !capabilities_.activeSlotMetadata) {
      reasons.append(QStringLiteral(
          "the runtime, service config, or deploy/model slot is incomplete"));
    }
    if (reasons.isEmpty()) {
      reasons.append(QStringLiteral("runtime or engine validation failed"));
    }
    serviceAvailabilityLabel_->setText(
        QStringLiteral("The unit is installed but %1. Start and Restart "
                       "remain disabled.")
            .arg(reasons.join(QStringLiteral("; "))));
  } else {
    serviceAvailabilityLabel_->setText(QStringLiteral(
        "The systemd unit is not installed. Service actions remain disabled."));
  }

  QStringList available;
  QStringList missing;
  const auto addCapability = [&available, &missing](bool value,
                                                    const QString &name) {
    (value ? available : missing).append(name);
  };
  addCapability(capabilities_.runtime, QStringLiteral("runtime"));
  addCapability(capabilities_.config, QStringLiteral("config"));
  addCapability(capabilities_.manifest, QStringLiteral("active-slot manifest"));
  addCapability(capabilities_.engine, QStringLiteral("active-slot engine"));
  addCapability(capabilities_.activeSlotMetadata,
                QStringLiteral("active-slot identity"));
  addCapability(capabilities_.activeSelectionMatches,
                QStringLiteral("selected model is active"));
  addCapability(capabilities_.directRuntimeReady,
                QStringLiteral("selected runtime validation"));
  addCapability(capabilities_.prepareScript,
                QStringLiteral("model preparation"));
  addCapability(capabilities_.prepareInputs,
                QStringLiteral("selected preparation inputs"));
  addCapability(capabilities_.rebuildScript, QStringLiteral("build scripts"));

  QString message =
      QStringLiteral("Available: %1.")
          .arg(available.isEmpty() ? QStringLiteral("none")
                                   : available.join(QStringLiteral(", ")));
  if (!missing.isEmpty()) {
    message +=
        QStringLiteral(" Missing: %1.").arg(missing.join(QStringLiteral(", ")));
  }
  if (!capabilities_.activeModel.isEmpty()) {
    message += QStringLiteral(" Active slot: %1 (%2), source %3.")
                   .arg(capabilities_.activeModel, capabilities_.activeVariant,
                        capabilities_.activeSourceDirectory);
  }
  if (capabilities_.serviceActive) {
    message += QStringLiteral(
        " Stop the service before preparing or replacing the active model.");
  }
  if (capabilities_.directInferenceActive) {
    message +=
        QStringLiteral(" A GUI-managed direct inference process is active.");
  }
  diagnosticAvailabilityLabel_->setText(message);
  appendConsole(QStringLiteral("SSH connection successful.\n%1\n%2\n")
                    .arg(serviceAvailabilityLabel_->text(), message),
                QColor(QStringLiteral("#4d9b58")));
}

void MainWindow::updateActionAvailability() {
  const bool mainBusy = runner_.isRunning();
  const bool controlBusy = controlRunner_.isRunning();
  const bool anyBusy = mainBusy || controlBusy;
  const QString optionError = validateDiagnosticOptions(diagnosticOptions());
  const bool parametersValid = optionError.isEmpty();
  const bool gpioMode =
      outputModeCombo_->currentText().contains(QStringLiteral("gpio"));
  const bool gpioApproved = !gpioMode || gpioSafetyCheck_->isChecked();

  hostEdit_->setEnabled(!anyBusy);
  userEdit_->setEnabled(!anyBusy);
  portSpin_->setEnabled(!anyBusy);
  identityFileEdit_->setEnabled(!anyBusy);
  remoteDirectoryEdit_->setEnabled(!anyBusy);
  probeButton_->setEnabled(!anyBusy);
  healthButton_->setEnabled(!anyBusy);

  serviceStatusButton_->setEnabled(!anyBusy && capabilities_.service);
  serviceReloadButton_->setEnabled(!anyBusy && capabilities_.service);
  serviceSafetyCheck_->setEnabled(!anyBusy && capabilities_.serviceReady);
  serviceStartButton_->setEnabled(
      !anyBusy && capabilities_.serviceReady && !capabilities_.serviceActive &&
      !capabilities_.directInferenceActive && serviceSafetyCheck_->isChecked());
  serviceStopButton_->setEnabled(!anyBusy && capabilities_.service &&
                                 capabilities_.serviceActive);
  serviceRestartButton_->setEnabled(
      !anyBusy && capabilities_.serviceReady && capabilities_.serviceActive &&
      !capabilities_.directInferenceActive && serviceSafetyCheck_->isChecked());
  serviceLogsButton_->setEnabled(!anyBusy && capabilities_.service);

  modelNameEdit_->setEnabled(!anyBusy);
  modelDirectoryEdit_->setEnabled(!anyBusy);
  variantCombo_->setEnabled(!anyBusy);
  preparationModeCombo_->setEnabled(!anyBusy);
  outputModeCombo_->setEnabled(!anyBusy);
  configPathEdit_->setEnabled(!anyBusy);
  sessionLogPathEdit_->setEnabled(!anyBusy);
  rawCapturePathEdit_->setEnabled(!anyBusy);
  emitProbabilitiesCheck_->setEnabled(
      !anyBusy &&
      outputModeCombo_->currentText().contains(QStringLiteral("jsonl")));
  gpioSafetyCheck_->setEnabled(!anyBusy && gpioMode);

  prepareModelButton_->setEnabled(
      !anyBusy && parametersValid && capabilities_.prepareScript &&
      capabilities_.prepareInputs && !capabilities_.serviceActive &&
      !capabilities_.directInferenceActive);
  validateRuntimeButton_->setEnabled(
      !anyBusy && parametersValid && capabilities_.runtime &&
      capabilities_.config && capabilities_.manifest && capabilities_.engine &&
      capabilities_.activeSlotMetadata && !capabilities_.serviceActive &&
      !capabilities_.directInferenceActive);
  startInferenceButton_->setEnabled(
      !anyBusy && parametersValid && gpioApproved && capabilities_.runtime &&
      capabilities_.config && capabilities_.manifest && capabilities_.engine &&
      capabilities_.activeSlotMetadata &&
      capabilities_.activeSelectionMatches &&
      capabilities_.directRuntimeReady && !capabilities_.serviceActive &&
      !capabilities_.directInferenceActive);
  rebuildRuntimeButton_->setEnabled(!anyBusy && capabilities_.rebuildScript &&
                                    !capabilities_.serviceActive &&
                                    !capabilities_.directInferenceActive);
  stopInferenceButton_->setEnabled(!controlBusy &&
                                   (capabilities_.directInferenceActive ||
                                    activeTask_ == TaskKind::directInference));

  commandCombo_->setEnabled(!anyBusy);
  runButton_->setEnabled(!anyBusy);
  cancelButton_->setEnabled(mainBusy && !controlBusy);
  cancelButton_->setText(activeTask_ == TaskKind::directInference
                             ? QStringLiteral("Stop inference")
                             : QStringLiteral("Cancel"));
}

void MainWindow::updateDiagnosticModeControls() {
  capabilities_.directRuntimeReady = false;
  if (capabilities_.known) {
    diagnosticAvailabilityLabel_->setText(QStringLiteral(
        "Trigger output changed. Validate again before starting inference."));
  }
  const bool hasJsonl =
      outputModeCombo_->currentText().contains(QStringLiteral("jsonl"));
  const bool hasGpio =
      outputModeCombo_->currentText().contains(QStringLiteral("gpio"));
  if (!hasJsonl) {
    emitProbabilitiesCheck_->setChecked(false);
  }
  if (!hasGpio) {
    gpioSafetyCheck_->setChecked(false);
  }
  updateActionAvailability();
}

void MainWindow::finishMainCommand(int exitCode,
                                   QProcess::ExitStatus exitStatus,
                                   bool cancelled) {
  const TaskKind completedTask = activeTask_;
  if (completedTask != TaskKind::availabilityProbe) {
    flushClassifiedConsoleBytes(mainStandardOutputDisplayBuffer_);
  }
  flushClassifiedConsoleBytes(mainStandardErrorDisplayBuffer_);
  const qint64 elapsed = commandTimer_.isValid() ? commandTimer_.elapsed() : 0;

  if (completedTask == TaskKind::availabilityProbe &&
      exitStatus == QProcess::NormalExit && exitCode == 0) {
    applyAvailabilityProbe(activeStandardOutput_);
  }
  if (completedTask == TaskKind::directInference) {
    capabilities_.directInferenceActive = false;
  }
  if (completedTask == TaskKind::validateRuntime &&
      exitStatus == QProcess::NormalExit && exitCode == 0) {
    capabilities_.directRuntimeReady = true;
    diagnosticAvailabilityLabel_->setText(
        QStringLiteral("The direct runtime configuration and active model "
                       "slot passed validation."));
  }
  if ((completedTask == TaskKind::prepareModel ||
       completedTask == TaskKind::rebuildRuntime) &&
      exitStatus == QProcess::NormalExit && exitCode == 0) {
    invalidateAvailability();
  }
  if (completedTask == TaskKind::serviceMutation &&
      exitStatus == QProcess::NormalExit && exitCode == 0) {
    invalidateAvailability();
  }

  const QString summary =
      completionSummary(exitCode, exitStatus, cancelled, elapsed);
  appendConsole(QStringLiteral("\n[%1]\n").arg(summary),
                completionColor(exitCode, exitStatus, cancelled));
  statusLabel_->setText(summary);
  activeTask_ = TaskKind::none;
  updateActionAvailability();
}
