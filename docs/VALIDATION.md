# Personal fork validation

This record covers source changes based on upstream commit
`befa16a206e6dc47777e81fc6e36a96fa08def9c`.
The fork is experimental and is not ready for a recommended VR installation.

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
- Compile the Windows installer executable and confirm that missing arguments cause refusal. No installation command runs.
- Parse the changed Python scripts and run `git diff --check`.

The DLL fixtures are inert byte strings. The tests never load them as code.
All file mutations occur in a temporary test directory.
The networking test opens a temporary loopback listener, then closes it.
Shutdown tests launch only hidden copies of the test executable inside a temporary directory.
A Windows job object contains those fixtures and terminates them if the test exits.
No real Steam, SteamVR, or game process receives a close request or termination.

## Tests added but not run

The following tests need the full CMake dependencies, including GoogleTest and OpenVR headers:

- Replay a captured controller and headset with headset tracking disabled. Keep the controller and omit the headset.
- Keep the first two stats clients working while rejecting a third client.
- Disconnect the viewer on an incorrectly typed JSON message without an uncaught exception.
- Refuse to overwrite an existing SteamVR database when reading is denied but replacement is permitted.

These tests are connected to the existing runtime and OpenVR component test targets.
Their presence does not establish that they pass.

## Checks still required

The full driver and viewer build did not run in this environment.
The complete dependency set is not provisioned for this fork, and no dependencies were downloaded or installed.
Cross-compilation and final packaging also did not run.

Before a usable release, build the driver, installer, and viewer with approved dependencies and run the complete test suite.
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
