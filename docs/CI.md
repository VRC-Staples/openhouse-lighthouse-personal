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
Pinned vcpkg recipes verify dependency source hashes. Metrics and external binary caches are disabled.

Tool installation is restricted to the runner's `.ci-tools/vcpkg` directory. Dependencies and
build output are under `build-ci`. No tool or dependency installation on a local PC is authorized
by this workflow. Updating the lock or adding tools requires a separate review and approval.

## Package checks

Packaging refuses a dirty source tree or a source revision different from the workflow revision.
It requires successful results for all ten expected CTest targets, including the personal safety,
shutdown, runtime, and OpenVR tests. Failed, skipped, or missing CTest targets block the package.
Individual upstream GoogleTest cases can skip optional fixtures inside a passing target.

The package must contain an x64 driver, installer, and viewer. Each binary must import only
recognized Windows system DLLs. The driver must export only `HmdDriverFactory` and contain the
personal-fork marker. Dependency license notices are required. Developer tools and OpenVR's
client DLL are not included in the package.

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
