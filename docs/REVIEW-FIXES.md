# Review fixes

## Additional findings after preview.3

These repairs address the eight findings reviewed at
`0fb9220d2852eefb722a57571f4eea19ce361134`. The existing draft releases remain unchanged.
The source changes do not explain Elastic's detection or establish a different antivirus verdict.

### Viewer

| Finding | Repair | Regression coverage |
| --- | --- | --- |
| V1 | Pose and settings validation use JSON references before accepting values. Unknown nested fields are not copied. | A 60,000-level pose and 30,000-level settings values exercise rejection and transactional loading. |
| V2 | Log normalization scans each hexadecimal-looking run once. | Matching examples, 5,000 bounded comparisons against the previous behavior, and 250,000-character unchanged runs. |
| V3 | Map labels reject nonfinite, out-of-range, and off-screen coordinates before text rendering. | The reported projection above `INT32_MAX`, a normal label, and a nonfinite position. |
| L1 | The viewer restricts default DLL searches to System32 before GLFW starts. MSVC also sets `/DEPENDENTLOADFLAG:0x800` for static imports. | A harmless DLL fixture tests application-directory, current-directory, and PATH exclusion. Windows CI compares baseline and hardened GLFW startup. Both packagers require the PE flag. |

The static-import restriction requires Windows 10 version 1607 or later. The supported package
uses the pinned MSVC CI. Legacy cross-packaging refuses a viewer without the same loader flag.
Explicit DLL paths remain supported. The viewer does not modify system-wide security settings.
The startup test requires native windows in both modes. It compares OpenGL context creation separately.
If both modes report GLFW's specific no-OpenGL error, the test records rendering as unvalidated.
A baseline success followed by a hardened failure fails the test. Other unexpected errors also fail.
This comparison does not establish compatibility with every vendor graphics driver.

### Runtime and reports

| Finding | Repair | Regression coverage |
| --- | --- | --- |
| R1 | Room imports skip ownership markers with the wrong type. Driver initialization catches exceptions from room-metadata import. | Wrong-type markers are excluded while valid foreign rooms remain available. |
| R2 | The standalone tracker inverts SteamVR's base-from-world transform before use. The direct `bases` format remains world-from-base. | Translation and rotation inversion use the conversion called by the importer. |
| R3 | Replay retains read errors and emits an error event. CLI callers return failure, and `lhviz` displays the error. | Corrupt records, invalid headers, successful completion, restart, cancellation, and actual CLI exit codes. |
| V4 | Report filenames include an atomic per-process sequence after the timestamp and process ID. Atomic no-overwrite publication remains unchanged. | Concurrent same-second names and two completed ZIPs with distinct destinations. |

`Session::Finished()` now means that replay processing ended, including failure or cancellation.
`Session::ReplayError()` distinguishes failed input from normal completion or cancellation.
The error resets on `Start()`. Consumers must rebuild with the changed C++ interface.

`python scripts/check-personal.py` runs the local native boundary and loader tests with existing Zig 0.15.2.
The pinned Windows CI adds `viewer_math`, `viewer_dll_search`, `viewer_startup`, and `replay_errors`
to CTest. A successful run for the exact source revision is required before reporting integrated validation.
The retained large source files keep their existing structure. New camera, log, loader, and pose helpers
separate the behavior needed by these tests without restructuring unrelated tracking code.

## Findings after preview.2

This change addresses the 14 findings from the whole-repository review of
`14b8e1d27a7d16921ecf59c63d6b65e351b1f44f`. Existing draft releases remain unchanged.
The changes do not establish the cause of Elastic's detection or promise a different verdict.

### Reports and parser boundaries

| Finding | Change | Regression coverage |
| --- | --- | --- |
| A1 | A failed ZIP addition permanently prevents publication. The report caller does not finalize after failure. | Missing input followed by `Close()`, failure after metadata, and a midstream read error. |
| A3 | ZIP writes enforce input and output byte limits while streaming. File sizes, names, and entry counts are bounded. | Reduced byte budgets, central-directory overflow, 65,536 entries, and actual decompression of a successful report. |
| R1 | Mapped input validates integer types and ranges before conversion. Record bounds use subtraction rather than overflowing addition. | Extreme offsets, fractional integers, invalid widths, and a valid field across two bytes. |
| R2 | GLB accessors validate BIN, view, stride, offset, count, and element ranges before address calculation. | Wrapped offsets, invalid counts and strides, truncated BIN data, and synthetic animations through the actual loader. |
| R5 | Device setup prepares metadata, the tracker, and the input decoder before replacing live state. | A late metadata failure preserves the previous configuration. Repeated failures retry instead of taking the duplicate shortcut. |

### Platform and recovery

| Finding | Change | Regression coverage |
| --- | --- | --- |
| A2 | Backup creation stages a complete file before publishing the reserved backup name. A competing identical backup is accepted. | Concurrent backups and refusal to replace a different backup. |
| A4 | Each thread closes its Windows waitable timer when the thread exits. | Handle counts before and after 32 short-lived threads. |
| A8 | Installation paths use the existing JSON library to decode strings. Invalid documents or array types are rejected. | Escaped backslashes, Unicode escapes, surrogate pairs, and malformed path lists. |

The viewer also defines `IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS`. This removes ImGui's
unused documentation-opening callback without disabling statistics. Windows packaging rejects
viewer imports of `ShellExecute` or `ShellExecuteEx`. This is not a demonstrated antivirus remedy.

## Lifecycle and retained source

| Finding | Change | Regression coverage |
| --- | --- | --- |
| R3 | Wireless reports restore receiver occupancy after reconnection or an inferred radio reset. | Synthetic disconnect, reconnect, and a missed-disconnect gap through the transport's report handler. |
| R4 | Real-time replay checks cancellation between waits of at most 10 ms. Capture reading also stops between records. | A two-second synthetic gap stops within a 500 ms test allowance. Reader cancellation stops after one record. |
| R6 | USB-only session operations compile out when `LH_BUILD_USB=OFF`. | A separate Windows build links the USB-off runtime using the already installed dependencies. |
| A5 | Firmware console parsing rejects nonfinite floats and overflowing integers. Parameter writes validate destination ranges before narrowing. | Native numeric tests cover `1e60`, long integers, signed limits, hexadecimal values, and ordinary values. Host LLVM IR compilation covers console C semantics. |
| A6 | The retained `lhtool session` loop observes its existing interruption flag. | Windows builds compile the tool. Interactive Ctrl+C delivery is not exercised by these tests. |

A7 corrects installed dependency discovery. Windows and Apple consumers request the existing
hidapi CMake package. Linux consumers recreate `PkgConfig::UDEV` from `libudev`, matching the exported target.
CI builds and runs an installed Windows consumer without starting USB. A separate test exercises
all three dependency-selection branches with stubs. It does not compile or link a Linux or Apple consumer.

### Evidence limits

The regression cases belong to the existing unit, runtime, and OpenVR component targets.
The native firmware number test is an additional CTest target. CI also prints the focused regression results.
A successful source-specific CI run, not the presence of these tests, establishes that they passed.

The tests use temporary files, synthetic reports, and repository fixtures. They do not open VR hardware,
install the driver, terminate real Steam processes, or execute an existing release artifact.
No new dependency version, public release, antivirus upload, or vendor submission is part of this change.

Firmware control algorithms and calibration limits remain unchanged. The host tests do not establish
safe motor behavior, a complete ARM firmware build, or hardware compatibility. Forced power loss during
backup creation, real USB reconnects, actual Steam shutdown, and Quest Pro compatibility remain untested.
