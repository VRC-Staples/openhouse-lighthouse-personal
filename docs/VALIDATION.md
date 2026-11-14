# Personal fork validation

This record covers source changes based on upstream commit
`befa16a206e6dc47777e81fc6e36a96fa08def9c`.
The fork is experimental and is not ready for a recommended VR installation.

## Automatic SteamVR discovery local build

The source now searches known local Steam folders when the menu opens and when **Find SteamVR** is selected.
It uses Steam's registered path, standard Program Files locations, `STEAMVR_PATH`, library metadata,
and the SteamVR app manifest. It validates `bin/win64/vrserver.exe` before offering a result.
A single result fills the folder field. Multiple results remain choices, not an automatic installation target.
Manual entry and the folder picker remain available. Finding a folder does not install or close anything.

The metadata parser limits file size, nesting, and collected library paths.
Discovery uses fixed known filenames and local drive paths. It does not recursively scan disks or query network shares.
The registry path reader is shared with shutdown code. Shutdown still canonicalizes that path as before.
The new `installer_discovery` test source uses synthetic metadata and temporary folders.
It does not call the default search that reads actual Steam settings.
CMake, the existing check script, and the package gate include that regression target.

The updated Windows x64 installer compiled with the existing MSVC 19.44.35224 toolchain,
C++20, the static runtime, optimization, and `/W4`, without warnings or errors.
The action and discovery regression executables compiled and passed using synthetic metadata and temporary folders.
Seven Python package-gate tests passed. Executable inspection accepted the x64 image and Windows DLL imports.
This was a standalone local build, not a full CMake, CTest, or locked release-package build.

The installer itself was not launched. Actual Steam metadata and registry discovery, menu rendering,
folder-list interaction, keyboard navigation, and worker progress remain unverified at runtime.
The previous local menu build still requires manual folder selection.
The earlier build evidence below predates automatic discovery and does not validate the new addition.

## Installer menu local build before automatic discovery

The source adds a Windows menu for no-argument launch and retains the command-line flags.
Shared actions provide path validation, status, confirmation, and install or restore behavior.
The menu requests normal closure only. It stops if verified processes remain open or restart.
The command line retains its separate typed `FORCE` confirmation.

A standalone Windows x64 installer build passed with the existing MSVC 19.44.35224 compiler,
C++20, the static runtime, optimization, and `/W4`. The compiler reported no warnings or errors.
The new `installer_actions` regression executable compiled and passed against temporary files.
It covers parsing, status, cancellation, and preflight refusal without closing real Steam/VR processes.
Executable inspection accepted the x64 image and its Windows system DLL imports.

The installer itself was not launched. The native layout, folder picker, keyboard navigation,
worker completion, and real application behavior remain unverified.
This was a direct compiler build, not a full CMake or locked CI package build.
The CMake target wiring and the full required CTest suite have not run for this addition.
The package gate now requires `installer_actions`. No new preview package or CI run exists.
Preview 5 and its Desktop extraction remain unchanged.

The passed checks below predate the menu addition. They are not evidence of a new full-suite run.

## Checks that passed

The hardware-free suite ran on Windows with the existing Zig 0.15.2 compiler.
The reproducible command is `python scripts/check-personal.py`.

- Compile the installer core, shared file helpers, legacy wireless decoder, and local networking code with compiler warnings enabled.
- Reject unsafe device filenames, including traversal, absolute paths, reserved Windows names, and embedded NULs.
- Install and restore synthetic DLL fixtures in a temporary directory. Preserve the backup across repeated installation and restoration.
- Refuse conflicting backups, damaged installed DLLs, and invalid package files without destroying recovery data.
- Preserve a locked destination after replacement fails. Refuse an unreadable source before backup creation.
- Reject truncated battery and IMU packets. Exercise all 256 header values at lengths 1 through 27 against a Windows guard page.
- Connect two sockets owned by the current Windows user and transfer a stats message through the peer authorization checks.
- Close a cooperative hidden fixture through `WM_CLOSE` and verify process exit.
- Keep an uncooperative fixture running after canceled or non-explicit force-close responses.
- Terminate a fixture only after the shutdown prompt receives `FORCE`. Keep an unrelated same-name fixture running.
- Refuse a stale process creation time and an unreadable user identity. Verify executable-path boundaries.
- Compile the previous CLI-only Windows installer and confirm that missing arguments cause refusal. No installation command runs.
  The current check uses an unknown option instead, because no arguments now select the menu.
- Parse the changed Python scripts and run `git diff --check`.

The DLL fixtures are inert byte strings. The tests never load them as code.
All file mutations occur in a temporary test directory.
The networking test opens a temporary loopback listener, then closes it.
Shutdown tests launch only hidden copies of the test executable inside a temporary directory.
A Windows job object contains those fixtures and terminates them if the test exits.
No real Steam, SteamVR, or game process receives a close request or termination.

## Full-build CI tests

The following tests need the full CMake dependencies, including GoogleTest and OpenVR headers.
They run in Windows CI; the local Zig-only check does not run them:

- Replay a captured controller and headset with headset tracking disabled. Keep the controller and omit the headset.
- Keep the first two stats clients working while rejecting a third client.
- Disconnect the viewer on an incorrectly typed JSON message without an uncaught exception.
- Refuse to overwrite an existing SteamVR database when reading is denied but replacement is permitted.

The [post-preview.2 review fixes](REVIEW-FIXES.md) add parser, report, recovery, and lifecycle
regressions. CI also builds the USB-off runtime and an installed Windows consumer.
Local checks compile the firmware console to host LLVM IR and run its numeric parser tests.
Neither operation builds or executes ARM firmware.

Additional viewer guard tests cover protocol greetings, bounded messages and collections,
numeric overflow, settings validation and atomic replacement, and report ZIP publication
without overwriting existing reports. See [viewer safeguards](VIEWER-SAFETY.md).

These tests are connected to the existing runtime and OpenVR component test targets.
Their presence does not establish that they pass. A draft package must link to its successful CI run.
The package's `BUILD.json` records the source revision, toolchain, binary hashes, and passed CTest targets.
CTest targets can contain individual upstream cases that skip unavailable optional fixtures; inspect the CI test log for those details.

## Checks still required

The full driver and viewer build is performed by the approved Windows CI workflow, not on the local VR computer.
A failed build, missing required CTest target, failed target, unexpected binary import, or missing license notice blocks a draft package.
Native CI uses MSVC with static compiler runtimes. It does not validate the separate MinGW cross-build.

A passing CI run does not replace the following checks before recommending a usable release.
Test stats access and process shutdown boundaries from a separate Windows account to confirm denial.
Test shutdown refusal for the same user in a different Windows session.
Normal shutdown and force-close were not tested against real Steam or SteamVR.
Those applications can minimize instead of exiting, show save prompts, or restart helpers.
Test the intended Quest Pro, Vive tracker, and Index controller setup on a recoverable installation.
Check pairing, buttons, finger tracking, haptics, battery state, reconnects, standby, and tracking-space alignment.
Compare tracking behavior with stock SteamVR under the same physical conditions.

A passed unit test does not establish better tracking, safe headset use, or an antivirus false positive.
No binary signing, independent reproducible-build check, or antivirus-vendor submission was performed.
No firmware was built or flashed. No upstream issue or pull request was created.
