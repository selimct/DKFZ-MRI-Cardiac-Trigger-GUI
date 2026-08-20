# Jetson Control GUI

A local Qt 6 desktop application for running commands on the Jetson over SSH
and viewing stdout, stderr, exit status, and service logs. Nothing from this
directory runs on the Jetson; commands are executed through its existing SSH
server.

This is a standalone repository. The computer running the GUI does not need a
local live-stack checkout. The live-stack checkout is required only on the
Jetson, at the path configured as `remote_directory`.

## Operating modes

The application uses the system OpenSSH client with key-based,
non-interactive authentication and the user's normal `known_hosts` file. It
streams output with severity-aware colors and also supports arbitrary
non-interactive commands. Ordinary unclassified output uses the normal text
color. Structured `debug` and `info` records are green, while structured
`warning`, `error`, and `fatal` records are red. Unclassified output remains
neutral unless it contains an explicit elevated-severity keyword.

### Installed service

Controls the checkout-linked `dkfz-native-uart-live` systemd unit. An
availability check must find the unit before Status, Stop, Reload Unit, or
Follow Log are enabled. Start and Restart additionally require a non-I/O
deployment preflight and explicit hardware confirmation.

The preflight follows the finalized deployment contract. It verifies that the
installed fragment resolves to `deploy/dkfz-native-uart-live.service` in the
selected checkout, that systemd does not have a stale unit definition, and
that the unit's read-only bind source matches the selected remote project
root. It also checks the service account and groups, `/var/lib/dkfz-live`, the
fixed `config/native_uart_live.conf`, the native runtime, and both files in the
stable `deploy/model` slot. While inactive, it runs
`--validate-only --check-engine`; while active, it avoids loading a second
engine and performs configuration-only validation. Capture paths in the
service config must stay under `/var/lib/dkfz-live`.

Reload Unit runs `systemctl daemon-reload`. It is needed after editing the unit
file, but runtime, config, manifest, and engine changes need only a service
restart. The GUI does not install or repair the service and cannot prove
electrical safety.

### Diagnostic / direct

Operates the current checkout beneath the selected remote project root. It can:

- build the TensorRT-enabled native runtime initially or rebuild it
  incrementally;
- prepare a selected best/tuned model on the Jetson in either of two modes:
  TensorRT from an existing ONNX (`--jetson-only`), or the complete
  PTH-to-ONNX-to-TensorRT pipeline;
- select an exact source directory such as `models/target_r` or
  `models_noCV/target_r` and pass it through `--model-dir`;
- atomically activate the resulting engine and manifest as
  `deploy/model/model.engine` and `deploy/model/model.conf`;
- validate the configuration and deserialize/check the TensorRT engine;
- start and stop the integrated runtime directly;
- select `none`, `jsonl`, `gpio`, or `gpio+jsonl` trigger output;
- optionally emit probability records;
- append stdout and stderr to a session log beneath the project root; and
- optionally capture raw UART bytes beneath the project root.

Both the service and direct runtime consume the stable `deploy/model` slot.
The GUI reads its model name, variant, and source directory and prevents direct
Start when they do not match the current selection. Model preparation is
disabled while the service is active, activating, reloading, or deactivating,
matching `run/deploy_model.sh`; use Stop, prepare and activate the selected
model, check availability again, then Start the service.

Direct inference defaults to `none`. GPIO-capable modes cannot start until the
hardware safety checkbox is selected. An empty raw-capture field explicitly
overrides and disables any capture path inherited from the committed config.
Direct Start stays disabled until the selected configuration and engine pass
validation, and direct/service starts are mutually locked to avoid competing
for the same UART or GPIO resources.

Both preparation modes require the selected variant's training configuration
and checkpoint on the Jetson because the standard TensorRT parity step uses
PyTorch. Existing-ONNX mode additionally requires the correctly named ONNX
artifact. Complete mode creates that ONNX artifact and runs PyTorch/ONNX parity
before continuing with TensorRT. It therefore requires the full configured
Jetson Python environment, not just `trtexec`.

The direct runtime remains attached to SSH for live output. The GUI records its
PID in the remote user's runtime directory, and Stop verifies that PID's
command line before sending `SIGTERM`. It will not use broad `pkill` matching
or stop a runtime it cannot identify.

After changing the connection, project root, model source directory, model,
variant, or configuration, run **Check availability** again. Operations whose
executable, script, configuration, active-slot files, or service unit is
unavailable stay disabled.

This is a command console, not a terminal emulator. Interactive password
prompts, `sudo` prompts, full-screen terminal programs, and shell job control
are deliberately unsupported. Service actions use `sudo -n` and therefore
require an appropriate narrow sudoers rule when the SSH user cannot control
the service directly. Model preparation and initial TensorRT builds can be
long-running; cancelling their SSH process is not guaranteed to terminate
every remote child.

## Build on Linux

From this repository's root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/jetson-control
```

Qt 6.5 or newer, CMake 3.21 or newer, a C++20 compiler, and OpenSSH are
required. The build directory remains local and is ignored by Git.

## Local connection file

Copy the committed template and place the Jetson private key beside it:

```bash
cp connection.ini.example connection.ini
cp /secure/path/to/jetson_ed25519 ./jetson_ed25519
chmod 600 jetson_ed25519
```

Then edit `connection.ini`:

```ini
[ssh]
host=192.168.1.50
user=orin
port=22
identity_file=jetson_ed25519
remote_directory=/home/orin/work/live_stack
```

`connection.ini` and the common private-key names in `.gitignore` are local
only. Do not force-add them to Git. A relative `identity_file` is resolved
relative to `connection.ini`, not relative to the shell's working directory.
When an identity is configured, the GUI passes it to OpenSSH with `-i` and
enables `IdentitiesOnly=yes`.

At startup the GUI looks for `connection.ini` in the current directory, beside
the executable, and one directory above the executable (the normal development
layout). An explicit file can be selected from any location:

```bash
./build/jetson-control --connection-file /path/to/connection.ini
```

Without a connection file, the fields remain editable and normal
`~/.ssh/config` host aliases continue to work.

## First connection

The application sets `BatchMode=yes` and `StrictHostKeyChecking=yes` so it
cannot display password or unknown-host prompts. Establish and verify the
connection once from a terminal before using the GUI:

```bash
ssh orin@JETSON_HOST
```

An entry from `~/.ssh/config` can be entered directly into the Host field. The
GUI's explicit User, Port, and SSH key values take precedence over values in
SSH config.
Set Remote project root to the actual checkout containing `prepare_model.sh`,
`run/`, `config/`, `models/`, and `build_data_ingest/`; do not set it to the
service-only `/opt/dkfz-live` mount. With the provided unit this is
`/home/orin/work/live_stack`.

## Cancellation

Cancel terminates the local SSH process for ordinary commands. Direct inference
uses its dedicated Stop action so the verified remote runtime receives
`SIGTERM`. The installed production runtime should continue to be managed
through systemd once its deployment has been reviewed and configured.

## Disclaimer

Copyright (c) 2026 Furkan Selim Cetin <mail@furkancetin.dev>

This software is provided "as is", without any warranty or condition. To the
extent permitted by law, the copyright holder will not be liable for damages
arising from the software, its use, or its nature. See the [LICENSE](LICENSE)
file for the complete PolyForm Noncommercial License 1.0.0 terms.
