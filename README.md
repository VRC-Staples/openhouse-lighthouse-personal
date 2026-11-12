# Openhouse: personal Lighthouse tracking fork

This is a personal fork of [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse).
It is for a Quest Pro with Vive trackers and Index controllers on Windows.
The goal is to use Openhouse's Lighthouse tracking and inspect tracking statistics.
These edits are for personal use. This is not an official Openhouse release or an upstream support request.

The Quest Pro keeps its existing PC connection software, headset tracking, and display driver.
This fork's SteamVR driver does not register, track, or display a Lighthouse headset.
Keep your existing method of aligning the Quest and Lighthouse tracking spaces.
This fork does not replace that alignment software or guarantee better tracking.

## Current status

This branch contains experimental source changes, not a validated release.
Do not install a build from this branch on your main VR setup yet.
Local hardware-free Windows checks pass. Windows CI requires a full build and its hardware-free tests before creating a draft package.
An unpublished draft is experimental, unsigned, and not hardware-validated. This fork does not release firmware.

No change here certifies the software as safe or guarantees that antivirus warnings disappear.
Keep Defender and SmartScreen enabled. Do not add broad exclusions.

## Personal changes

- Remove the stock-headset bridge and its runtime DLL patches from the driver build.
  The driver exports only the required OpenVR entry point, `HmdDriverFactory`.
- Disable headset tracking in the driver's session. Standalone library replay tools retain the upstream capability for tests.
- Replace the Windows installer with a driver-only installer. It offers normal closure before a separate, explicit force-close fallback.
  It does not elevate itself, edit SteamVR configuration, add startup entries, or install an update checker.
- Keep the stock DLL backup after installation and restoration. Stage each replacement before changing the destination.
  Unknown driver states and conflicting backups stop the operation instead of deleting recovery files.
- Keep `openhouse-viewer` for device state, base-station visibility, battery data, the room view, and logs.
  On Windows, both ends of its loopback connection require a peer owned by the same Windows user.

The telemetry server admits at most two viewers and bounds its backlog and output queues.
The viewer disconnects on malformed message types and bounds retained log text.
Device filenames, legacy packet lengths, and SteamVR database replacement also receive additional checks.
See [the validation record](docs/VALIDATION.md) for tested and untested behavior.

The personal Windows package contains only the driver, installer, viewer, documentation, and license notices.
It does not include the update checker, comparison app, firmware, or standalone diagnostic tools.
The upstream source for unrelated tools remains available for reference.

## Safety boundaries

This is still a replacement for `SteamVR/drivers/lighthouse/bin/win64/driver_lighthouse.dll`.
It runs inside SteamVR with SteamVR's permissions. It is not a sandbox.
The installer checks the DLL structure and fork marker, not an authenticated publisher signature.
Use a reviewed source revision and a trusted build environment.

Programs running as your Windows user can still access the stats connection.
The Windows peer check is not implemented on Linux. Linux packaging is disabled in this fork.
Local recordings and report archives can contain movement, room layout, device identifiers, and SteamVR logs.
Review those files before sharing them. The inspected report code does not upload them automatically.

If upstream Openhouse is already installed, restore it with its own uninstaller first.
This fork does not remove an older installer's configuration edits or startup entries.
Before a first installation, verify SteamVR's files through Steam and save a separate backup of its configuration.
Do not flash base-station firmware for this tracker/controller use case.

## Build and validation

The upstream C++ dependencies are Eigen, Ceres, nlohmann-json, hidapi, zlib, and the OpenVR headers.
The viewer also needs GLFW and OpenGL. The full test suite needs GoogleTest.
Provide approved, pinned dependencies before configuring CMake. The local cross-build script does not download dependencies.
The approved [Windows CI workflow](docs/CI.md) provisions pinned dependencies only on disposable GitHub runners.

With those dependencies available:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake can skip the driver or viewer if their dependencies are missing.
A successful partial build is not a successful VR release build.
Make sure that `driver_lighthouse`, `openhouse-installer`, and `openhouse-viewer` all build before packaging.

The hardware-free regression executable needs only a C++20 compiler and the Windows SDK libraries.
It uses temporary files and a temporary loopback listener. It does not open VR hardware or change SteamVR.
For an existing Zig 0.15.2 installation, run:

```sh
python scripts/check-personal.py
```

This command compiles and runs file, installer-core, packet-boundary, local-network, and shutdown checks.
Shutdown tests use hidden temporary fixture processes, not your real Steam or VR processes.
It also compiles the installer and confirms that missing arguments cause a refusal.
It does not run an install action or certify the full driver.

The optional Linux-hosted Windows cross-build script requires an existing vcpkg checkout at
`a5f736224e85b32e25a8ffc52562333180396642`, with its binary and source cache already provisioned.
It refuses automatic clone, bootstrap, and downloads. The packaging script rejects an uncommitted source tree.
No signing step or independent binary-to-source reproduction is provided.

## Installer interface after validation

These commands document the source interface. They are not a recommendation to install this unvalidated branch.
Save your work and close VR games before an install or restore operation.
Replace the example path with the SteamVR directory on your computer.

```text
openhouse-installer.exe --status --steamvr "D:\SteamLibrary\steamapps\common\SteamVR"
openhouse-installer.exe --install --steamvr "D:\SteamLibrary\steamapps\common\SteamVR"
openhouse-installer.exe --uninstall --steamvr "D:\SteamLibrary\steamapps\common\SteamVR"
```

### Closing running applications

The installer identifies SteamVR processes inside the selected SteamVR directory.
It also identifies Steam and its web helpers inside the Steam directory registered for your Windows account.
It checks executable paths, Windows user identities, and the current Windows session, not process names alone.
An unreadable process identity stops the operation. An unrelated process with the same name is not selected.

1. Review the displayed process paths and IDs, then confirm the normal close request.
2. Wait up to 15 seconds. The installer sends `WM_CLOSE` to each selected process's windows without moving focus.
3. If processes remain, review the new list. Type `FORCE` to terminate only those processes, or press Enter to cancel.

Force-close skips cleanup and can lose unsaved work. It can still trigger antivirus detection.
Some applications minimize to the tray or have no closeable windows. A normal close request does not guarantee shutdown.
The `--yes` option skips only the installation confirmation. It does not answer either shutdown prompt.
End-of-input or any force-close answer other than `FORCE` cancels the operation.

Before force-close, the installer rechecks the process start time, executable path, Windows user, and session.
It waits up to five seconds for termination. A restarted process, denied access, or locked DLL stops installation.
The installer does not request elevation, kill arbitrary games, or repeatedly terminate newly started processes.

Installation saves `driver_lighthouse.dll.openhouse-personal-stock` beside the original DLL.
Restoration keeps that backup. The installer never silently replaces a different retained backup.
A SteamVR update can replace the personal driver. This fork does not automatically reinstall it.
An unrecognized or damaged installed DLL stops restoration and leaves the backup intact.
Use Steam's file verification to recover when the installer cannot establish a safe state.

Run `apps/openhouse-viewer.exe` while SteamVR runs to inspect statistics.
`LH_TELEMETRY_PORT=0` disables statistics, so leave it unset when using the viewer.
This fork does not install the upstream settings page. Existing upstream defaults and user settings can still affect tracking.

## Attribution and scope

Original project: [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse).
Personal fork owner: [VRC-Staples](https://github.com/VRC-Staples).
The original [MIT license](LICENSE) and third-party notices remain in place.

Changes start from upstream commit `befa16a206e6dc47777e81fc6e36a96fa08def9c`.
The [upstream README snapshot](docs/UPSTREAM-README.md) describes the original project, not this fork's installation or support scope.
Report problems with these personal changes in this fork, not as claims against the upstream project.
