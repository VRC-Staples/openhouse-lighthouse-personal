# Windows CI and draft releases

## What runs

The `Windows build and tests` workflow runs on pushes and pull requests to
`personal/lighthouse-only`. It can also run manually.

Each run uses a disposable `windows-2022` GitHub-hosted runner. It checks out the exact
workflow revision, builds the Windows targets, runs CTest, and validates the package.
The workflow does not install into SteamVR, access VR hardware, or change security settings.
Allow about 20–60 minutes for a first dependency build. Each job has a 90-minute limit.

Normal checks have read-only repository permission. Only a manual draft job on this fork's
personal branch has release-write permission. No third-party GitHub Actions are downloaded.
The release token is provided only to the final draft-creation step.

## Toolchain and dependency pins

The runner supplies Visual C++, CMake, Ninja, Python, Git, and the GitHub CLI.
These preinstalled tools follow the runner image rather than a permanently frozen compiler image.
The package records the runner image and MSVC version. This is provenance, not a claim of
independently reproducible binaries.

`scripts/ci/toolchain-lock.json` records the approved vcpkg revision, executable release and
SHA-256, port revisions, and source SHA-512 hashes. The build downloads the executable to a
file and checks its hash before running it. It does not run the upstream bootstrap script.
Pinned vcpkg recipes verify dependency source hashes. They also provision their recorded build helpers,
including CMake 4.4.3, pkgconf 3.0.7, and the MSYS2 runtime 3.6.10-4; their hashes are in the lock record.
The runner's existing CMake starts the project build; the recipes use their pinned helper CMake.
Metrics and external binary caches are disabled.

`vcpkg-configuration.json` selects a repository overlay for static zlib 1.4 only.
An overlay is a local recipe that replaces one upstream port without changing the baseline.
The lock binds the configuration and every overlay file to normalized SHA-256 hashes.
It also records the zlib source URL, commit, and archive SHA-512.
`scripts/toolchain_lock.py` checks those records before CI downloads or installations
and before packaging.
The Linux and MinGW entry points run the same check and require the existing pinned vcpkg checkout.
They do not clone or bootstrap tools. All other dependency pins remain unchanged.

Changes to the zlib recipe do not authorize its execution on a runner or a local PC.
The updated recipe needs separate approval before dependency installation or CI execution.
The standalone zlib check does not replace the integrated driver, consumer, or viewer tests.

Tool installation is restricted to the runner's `.ci-tools/vcpkg` directory. Dependencies and
build output are under `build-ci`. No tool or dependency installation on a local PC is authorized
by this workflow. Updating the lock or adding tools requires a separate review and approval.

## Package checks

Packaging refuses a dirty source tree or a source revision different from the workflow revision.
It requires successful results for every target in the packager's `REQUIRED_TESTS` set,
including personal safety, shutdown, installer actions and discovery, runtime, OpenVR,
and `zlib_compatibility`.
The zlib target checks version identity, exact-output raw-deflate completion,
decompression, and CRC. Failed, skipped, or missing CTest targets block the package.
Individual upstream GoogleTest cases can skip optional fixtures inside a passing target.

The package requires an x64 driver, installer, viewer, `lhtool`, `lhcapture`, `lhviz`,
`openhouse-compare`, and matching `lh.dll`. Tools and runtime are together in `apps/`.
Only `lhtool`, `lhviz`, and `openhouse-compare` may import that bundled runtime DLL.
Every other import must be a recognized Windows system DLL. The driver must export only
`HmdDriverFactory` and contain the personal-fork marker. Dependency license notices are required.
The checker, SDK example, firmware, and OpenVR client DLL are excluded. CI enables extra-app
builds to produce the comparison tool but does not run or ship the checker or SDK example.
Only OpenVR's prebuilt client dependency uses dynamic linkage for the test probe;
the shipped targets retain static compiler runtimes.

The package includes `BUILD.json`. Release assets also include the ZIP's SHA-256 checksum,
a copy of `BUILD.json`, and the CTest XML report.

## Create an unpublished draft

Run this only for an unused personal-preview tag:

```sh
gh workflow run windows.yml --repo VRC-Staples/openhouse-lighthouse-personal \
  --ref personal/lighthouse-only -f draft_tag=v0.1.0-personal-preview.1
```

The draft job rebuilds and tests its exact source revision before creating any release.
It creates a tag bound to that revision and refuses to replace an existing tag or release.
The tag and source are public. The release remains an unpublished draft; GitHub does not
provide a private published release inside a public repository.

The workflow never publishes a draft automatically. Do not publish or recommend installation
until the validation limitations in `VALIDATION.md` have been reviewed.
Binaries remain unsigned, hardware compatibility is untested, and antivirus warnings can remain.
