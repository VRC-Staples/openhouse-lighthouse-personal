# Application builds

All eight upstream application source directories remain in this fork. The standard Windows
package contains only the driver, installer, and viewer. That package list is separate from
which applications CMake builds.

## Applications and dependencies

| Application | Role | Root build |
| --- | --- | --- |
| `openhouse-viewer` | Displays telemetry from the running driver | Viewers enabled |
| `openhouse-installer` | Installs or restores the driver with consent checks | Windows tools enabled |
| `lhtool` | Live tracking, offline replay, and diagnostics | USB and tools enabled |
| `lhcapture` | Records raw USB reports | USB and tools enabled |
| `lhviz` | Developer viewer with its own tracking session | Viewers enabled |
| `openhouse-check` | Warns about driver replacement and checks upstream releases | Extra apps enabled |
| `openhouse-compare` | Compares tracking with a separately loaded stock driver | Extra apps enabled |
| `example_app` | Demonstrates the session API | Extra apps enabled, or its separate CMake project |

The tracking apps share libraries, including the session runtime in `lh.dll` on Windows.
The SteamVR driver embeds its own runtime. It does not need the diagnostic executables to
be installed or running. Keep the matching `lh.dll` beside diagnostic apps that import it.

The root build already included `lhtool`, `lhcapture`, and `lhviz` before the extra-app option
was added. Their absence from the Windows ZIP was a packaging choice, not source deletion.
The SDK example also retains its original independent build in `apps/example_app`.

## Build every application

Provide the approved, pinned dependencies first. See [Build and validation](../README.md#build-and-validation).
Then configure a separate build directory:

```sh
cmake -S . -B build-all -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLH_BUILD_USB=ON -DLH_BUILD_TOOLS=ON -DLH_BUILD_VIEWER=ON -DLH_BUILD_EXTRA_APPS=ON
cmake --build build-all --parallel 2
```

`LH_BUILD_EXTRA_APPS` defaults to `OFF`. When enabled, it requires the USB target, viewer
libraries, GLFW/OpenGL, and OpenVR headers. Missing prerequisites cause a configuration
error instead of silently omitting the extra apps.

The option adds build targets only. It does not change the driver-only installer, package
contents, startup registration, or headset restrictions. The extra apps are not added to
CMake's installation rules.

## Before running diagnostic apps

- `lhtool`, `lhcapture`, `lhviz`, and `example_app` can open tracking hardware. Do not start
  a second live hardware session while SteamVR owns the same receivers.
- Use an explicit replay mode and copied room/settings files for offline investigations.
  Building the tools does not itself analyze an existing capture.
- `openhouse-compare` loads a stock driver and can access tracking hardware. Building it
  does not validate that driver, its private interfaces, or real-hardware behavior.
- `openhouse-check` retains its upstream release URL. Running it can make a network request
  and open a notification or release page. It is not a fork-aware updater, and this fork
  does not register it to run automatically.
- Building a GUI application does not verify rendering or keyboard interaction.

These application builds do not include base-station firmware. Do not flash firmware as
part of a tracker/controller setup.
