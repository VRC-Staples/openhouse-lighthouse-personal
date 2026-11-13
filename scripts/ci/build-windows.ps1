$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'This approved tool installation is restricted to GitHub Windows runners.'
}

function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

$repo = (Resolve-Path "$PSScriptRoot/../..").Path
Set-Location $repo
$lock = Get-Content "$PSScriptRoot/toolchain-lock.json" -Raw | ConvertFrom-Json
$manifestText = (Get-Content "$repo/vcpkg.json" -Raw).Replace("`r`n", "`n")
$manifest = $manifestText | ConvertFrom-Json
$manifestHash = [Convert]::ToHexString(
    [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($manifestText)))
if ($manifest.'builtin-baseline' -ne $lock.vcpkg_commit -or
    $manifestHash -ne $lock.manifest_sha256) { throw 'The approved dependency manifest changed.' }

$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'The runner must already have Visual C++ installed.' }
$environment = & $env:ComSpec /d /s /c "`"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the existing Visual C++ toolchain.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

$tools = "$repo/.ci-tools/vcpkg"
Run git @('init', $tools)
Run git @('-C', $tools, 'remote', 'add', 'origin', 'https://github.com/microsoft/vcpkg.git')
Run git @('-C', $tools, 'fetch', '--depth=1', 'origin', $lock.vcpkg_commit)
Run git @('-C', $tools, 'checkout', '--detach', 'FETCH_HEAD')
$revision = & git -C $tools rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $lock.vcpkg_commit) { throw 'Wrong vcpkg revision.' }
$binary = "$tools/vcpkg.exe"
Invoke-WebRequest -Uri $lock.vcpkg_tool.url -OutFile $binary
if ((Get-FileHash $binary -Algorithm SHA256).Hash -ne $lock.vcpkg_tool.sha256) {
    throw 'vcpkg.exe integrity check failed. The downloaded file will not run.'
}
$env:VCPKG_ROOT = $tools
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_BINARY_SOURCES = 'clear'
$env:VCPKG_MAX_CONCURRENCY = '2'
Run $binary @('version', '--disable-metrics')

$build = "$repo/build-ci"
$triplet = 'x64-windows-static-release'
Run python @('scripts/tests/test_package_windows.py')
Run python @('scripts/tests/check-viewer-startup.py', '--self-test')
Run cmake @('-P', 'scripts/tests/check-triplet.cmake')
Run cmake @('-P', 'scripts/tests/check-package-dependencies.cmake')
Run cmake @('-S', '.', '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_TOOLCHAIN_FILE=$tools/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_OVERLAY_TRIPLETS=$repo/cmake/triplets", "-DVCPKG_TARGET_TRIPLET=$triplet",
    '-DVCPKG_MANIFEST_FEATURES=tests;viewer', '-DLH_BUILD_ORACLE_TESTS=OFF')
Run cmake @('--build', $build, '--parallel', '2', '--', '-k', '0')
Run ctest @('--test-dir', $build, '--output-on-failure', '--timeout', '180',
    '--output-junit', "$build/ctest-results.xml")
Run ctest @('--test-dir', $build, '-V', '-R', '^viewer_startup$')
Run "$build/lh_openvr_component_tests.exe" @('--gtest_filter=GlbBounds.*:ReportTransactions.*:ReportLimits.*:ReportNames.*:SteamRoomMetadata.*:Viewer*.*')
Run "$build/lh_unit_tests.exe" @('--gtest_filter=PlatformReview.*:ReceiverLifecycle.*:TrackerPlacement.*')
Run "$build/lh_runtime_tests.exe" @('--gtest_filter=MappedBounds.*:DeviceSetup.*:ReplayLifecycle.*')
Run python @('scripts/package-windows-msvc.py', $build, "$build/vcpkg_installed/$triplet")

# Reuse the approved installed dependencies. These configurations cannot install packages.
$reuse = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_TOOLCHAIN_FILE=$tools/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_OVERLAY_TRIPLETS=$repo/cmake/triplets", "-DVCPKG_TARGET_TRIPLET=$triplet",
    "-DVCPKG_INSTALLED_DIR=$build/vcpkg_installed", '-DVCPKG_MANIFEST_INSTALL=OFF')
$stage = "$build/installed-consumer-prefix"
Run cmake @('--install', $build, '--prefix', $stage)
Run cmake (@('-S', 'scripts/tests/installed-consumer', '-B', "$build/consumer",
    "-DCMAKE_PREFIX_PATH=$stage") + $reuse)
Run cmake @('--build', "$build/consumer", '--parallel', '2')
$oldPath = $env:PATH
try {
    $env:PATH = "$stage/bin;$oldPath"
    Run "$build/consumer/consumer.exe" @()
} finally { $env:PATH = $oldPath }
Run cmake (@('-S', '.', '-B', "$repo/build-ci-usb-off", '-DLH_BUILD_USB=OFF',
    '-DLH_BUILD_TOOLS=OFF', '-DLH_BUILD_VIEWER=OFF', '-DLH_BUILD_TESTS=OFF') + $reuse)
Run cmake @('--build', "$repo/build-ci-usb-off", '--target', 'lh', '--parallel', '2')
$summary = @"
## Windows build passed
- Driver, installer, viewer, and hardware-free CTest targets built and checked.
- Package imports, driver exports, checksums, licenses, and source revision checked.
- USB-off runtime built; installed Windows consumer linked and ran without starting USB.
- Firmware numeric tests ran on the host; Linux/Apple dependency selection used stubs.
- Source: $env:GITHUB_SHA
- Runner image: $env:ImageVersion; MSVC: $env:VCToolsVersion
- Not checked: real VR hardware, signing, antivirus verdict, or stock-driver oracle tests.
"@
$summary | Add-Content $env:GITHUB_STEP_SUMMARY
