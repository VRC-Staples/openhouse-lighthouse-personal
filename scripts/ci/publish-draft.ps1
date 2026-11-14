$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_EVENT_NAME -ne 'workflow_dispatch' -or
    $env:GITHUB_REPOSITORY -ne 'VRC-Staples/openhouse-lighthouse-personal' -or
    $env:GITHUB_REF -ne 'refs/heads/personal/lighthouse-only') {
    throw 'Draft creation is restricted to a manual run on the personal branch.'
}
if ($env:DRAFT_TAG -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+-personal-preview\.[0-9]+$') {
    throw 'Use a personal preview tag such as v0.1.0-personal-preview.1.'
}
$record = Get-Content 'dist/BUILD.json' -Raw | ConvertFrom-Json
if ($record.source_commit -ne $env:GITHUB_SHA -or $record.source_dirty) {
    throw 'Package source does not match this workflow.'
}
$notes = @"
## Experimental personal Lighthouse-only preview

For Quest Pro users with Vive trackers and Index controllers. Not hardware-validated.
This unpublished draft is not a private published release; the source repository is public.

- Includes the driver, consent-based installer, tracking-statistics viewer, and license notices.
- Includes lhtool, lhcapture, lhviz, openhouse-compare, and their matching lh.dll in apps/.
- Diagnostic tools can open tracking hardware. Do not run a second live USB session while SteamVR owns the receivers.
- Keep lh.dll beside the diagnostic executables. See APPLICATIONS.md before running them.
- Windows builds and the required hardware-free CTest targets passed before packaging.
- Excludes the stock-headset bridge, firmware, upstream update checker, and SDK example.
- Binaries are unsigned. No antivirus-vendor verdict or real SteamVR shutdown test is claimed.
- Keep Defender and SmartScreen enabled. Force-close can lose unsaved work.

Source: $env:GITHUB_SHA
Build: https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID

Inspect BUILD.json and SHA256SUMS.txt before using the ZIP. This workflow does not install into SteamVR.
Original project and MIT license: https://github.com/YusufOzmen01/openhouse
"@
$notes | Set-Content 'build-ci/release-notes.md' -Encoding utf8
# Atomic creation binds the tag to this tested commit; an existing tag stops the operation.
gh api --method POST "repos/$env:GITHUB_REPOSITORY/git/refs" `
    -f "ref=refs/tags/$env:DRAFT_TAG" -f "sha=$env:GITHUB_SHA"
if ($LASTEXITCODE -ne 0) { throw 'Tag creation failed. No existing tag was changed.' }
gh release create $env:DRAFT_TAG 'dist/openhouse-lighthouse-personal-windows-x64.zip' `
    'dist/SHA256SUMS.txt' 'dist/BUILD.json' 'build-ci/ctest-results.xml' `
    --repo $env:GITHUB_REPOSITORY --verify-tag --draft --prerelease `
    --title "Personal Lighthouse-only $env:DRAFT_TAG" --notes-file 'build-ci/release-notes.md'
if ($LASTEXITCODE -ne 0) { throw 'Draft creation failed. No existing release was overwritten.' }
