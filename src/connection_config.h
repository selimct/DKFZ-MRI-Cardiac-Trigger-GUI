#pragma once

#include "ssh_command_builder.h"

#include <QString>

struct ConnectionConfigResult {
  SshConnectionOptions options;
  QString path;
  QString error;
  bool found{false};
};

[[nodiscard]] QString
findDefaultConnectionConfig(const QString &applicationDirectory,
                            const QString &currentDirectory);
[[nodiscard]] ConnectionConfigResult loadConnectionConfig(const QString &path,
                                                          bool required = true);
