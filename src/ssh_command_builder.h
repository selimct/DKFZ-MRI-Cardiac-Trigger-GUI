#pragma once

#include <QString>
#include <QStringList>

struct SshConnectionOptions {
  QString host;
  QString user;
  quint16 port{22};
  QString identityFile;
  QString remoteDirectory;
};

[[nodiscard]] QString posixShellQuote(const QString &value);
[[nodiscard]] QString sshDestination(const SshConnectionOptions &options);
[[nodiscard]] QString remoteShellCommand(const SshConnectionOptions &options,
                                         const QString &command);
[[nodiscard]] QStringList sshArguments(const SshConnectionOptions &options,
                                       const QString &command);
