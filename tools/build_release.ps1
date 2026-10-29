[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [string] $AppVersion = '',
    [switch] $BenchSteering,
    [switch] $BenchDrive,
    [switch] $EnableActuation,
    [string] $VerifiedCandidate,
    [switch] $ReleasePair
)

$ErrorActionPreference = 'Stop'
if ($Configuration -ne 'Release') { throw 'Formal dist accepts Release only; use build_online_debug.ps1 for Debug.' }
if ($BenchDrive) { $BenchSteering = $true }
if ($EnableActuation -and (-not $BenchDrive -or $ReleasePair)) {
    throw 'Motion-enabled release requires -BenchDrive and a single verified App candidate.'
}
if ($BenchSteering -and ($Configuration -ne 'Release' -or $ReleasePair -or
    [string]::IsNullOrWhiteSpace($VerifiedCandidate) -or !(Test-Path -LiteralPath $VerifiedCandidate -PathType Leaf))) {
    throw 'Steering publication requires Release and the exact verified candidate image.'
}
$product = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'product_version.ps1')
$AppVersion = Resolve-PatrolChassisAppVersion -ProductRoot $product -Override $AppVersion
$entry = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release_entry.json') -Raw | ConvertFrom-Json
$steeringCompiled = [bool]$BenchSteering
$driveCompiled = [bool]$BenchDrive
$actuationCompiled = $EnableActuation.IsPresent
if (-not $BenchSteering -and -not $ReleasePair) {
    # Preserve the declared production build policy; runtime safety gates remain mandatory.
    $steeringCompiled = [bool]$entry.formal_options.steering_executor
    $driveCompiled = [bool]$entry.formal_options.drive_executor
    $actuationCompiled = [bool]$entry.formal_options.actuation
}
$platform = Join-Path $product 'platform'
. (Join-Path $platform 'tools/standard_release.ps1')
$bootTarget = Join-Path $platform 'targets/stm32f105_abox_boot'
$bootArtifact = Join-Path $bootTarget 'dist/ABox_Boot.bin'
$bootVerifier = Join-Path $bootTarget 'tools/verify_boot_artifact.py'
$stableVersion = 'patrol-chassis-0.1.0'
$versions = if ($ReleasePair) { @($stableVersion) } else { @($AppVersion) }
$credentialBuildRoot = Join-Path ([IO.Path]::GetTempPath()) ('patrol-chassis-release-' + [Guid]::NewGuid().ToString('N'))

function Get-GitValue([string] $repo, [string[]] $arguments) { return (& git -C $repo @arguments | Out-String).Trim() }
function Get-Dirty([string] $repo) { return -not [string]::IsNullOrWhiteSpace((& git -C $repo status --porcelain | Out-String).Trim()) }
function Get-Usage([string] $elf) {
    $bin = [IO.Path]::ChangeExtension($elf, '.bin')
    $ramEnd = [int64]0x20000000
    foreach ($line in (& arm-none-eabi-objdump -h $elf)) {
        if ($line -match '^\s*\d+\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+') {
            $size = [Convert]::ToInt64($Matches[1], 16)
            $vma = [Convert]::ToInt64($Matches[2], 16)
            if ($vma -ge 0x20000000 -and $vma -lt 0x20010000) { $ramEnd = [Math]::Max($ramEnd, $vma + $size) }
        }
    }
    return [ordered]@{ flash_bytes = (Get-Item -LiteralPath $bin).Length; ram_bytes = ($ramEnd - 0x20000000) }
}
function Get-Artifact([string] $path, [string] $address) {
    return Get-ABoxReleaseArtifact -Path $path -LoadAddress $address
}
function Assert-FormalBootArtifacts([string] $buildDirectory) {
    $forbidden = @(
        'ABOX_BOOT_FAULT_INJECTION_TEST', 'testBuild=1', 'TEST_HOLD',
        'APP_PROGRAM_P10', 'APP_PROGRAM_P50', 'APP_PROGRAM_P90',
        'BOOTSTATE_AFTER_ERASE', 'BOOTSTATE_AFTER_HEADER', 'BOOTSTATE_MID_RECORD',
        'BOOTSTATE_BEFORE_CRC', 'BOOTSTATE_BOTH_INVALID', 'INVALID_VECTOR', 'UFS_CANDIDATE_MISSING'
    )
    $files = Get-ChildItem -LiteralPath $buildDirectory -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.bin', '.map', '.lst', '.elf') }
    foreach ($file in $files) {
        $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($file.FullName))
        foreach ($marker in $forbidden) {
            if ($text.IndexOf($marker, [StringComparison]::Ordinal) -ge 0) {
                throw "Formal Boot artifact contains forbidden test marker '$marker': $($file.FullName)"
            }
        }
    }
}
function Invoke-TargetBuild([string] $role, [string] $target, [string] $buildName,
                            [string[]] $definitions, [string] $buildRoot = '') {
    $source = Join-Path $product $role
    $build = if ([string]::IsNullOrWhiteSpace($buildRoot)) {
        Join-Path $source "build/$buildName"
    } else {
        Join-Path $buildRoot $buildName
    }
    if ($role -ne 'app') { throw 'Release builds App only; Boot must be frozen.' }
    return Invoke-ABoxAppBuild -ProductRoot $product -BuildDirectory $build -Target $target -Configuration $Configuration -Definitions $definitions
}

try {
    & (Join-Path $platform 'tools/validate_flash_layout.ps1') -BootLinker (Join-Path $bootTarget 'STM32F105xx_FLASH.ld') -AppLinker (Join-Path $product 'app/STM32F105xx_FLASH.ld') | Out-Host
    if (-not $?) { throw 'Flash layout validation failed' }
    $productCommit = Get-GitValue $product @('rev-parse', 'HEAD')
    $platformCommit = Get-GitValue $platform @('rev-parse', 'HEAD')
    $productDirty = Get-Dirty $product; $platformDirty = Get-Dirty $platform
    if ($productDirty -or $platformDirty) { throw 'Release requires clean product and platform worktrees (manifest product_dirty=false).' }

    foreach ($required in @($bootArtifact, $bootVerifier)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing frozen platform Boot input: $required" }
    }
    & python $bootVerifier $bootArtifact | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Frozen platform Boot validation failed' }
    Assert-FormalBootArtifacts (Split-Path -Parent $bootArtifact)
    $bootUsage = [ordered]@{ image_bytes = (Get-Item -LiteralPath $bootArtifact).Length; source = 'platform/targets/stm32f105_abox_boot' }

    foreach ($version in $versions) {
        $appDefinitions = @("-DPATROL_CHASSIS_APP_VERSION_OVERRIDE=$version", "-DPATROL_ACTIVE_CAR=0")
        $appDefinitions += "-DPATROL_CHASSIS_ACTUATION_ENABLED=$(if ($actuationCompiled) { 'ON' } else { 'OFF' })"
        $appDefinitions += "-DPATROL_STEERING_EXECUTOR_ENABLED=$(if ($steeringCompiled) { 'ON' } else { 'OFF' })"
        $appDefinitions += "-DPATROL_DRIVE_EXECUTOR_ENABLED=$(if ($driveCompiled) { 'ON' } else { 'OFF' })"
        $app = Invoke-TargetBuild 'app' 'Patrol_Vehicle_Chassis_App' "ABoxUnified$Configuration-$version" $appDefinitions $credentialBuildRoot
        & python (Join-Path $PSScriptRoot 'verify_can2_stack.py') (Split-Path -Parent $app.elf)
        if ($LASTEXITCODE -ne 0) { throw 'CAN2 owner stack validation failed; dist was not replaced.' }
        $appUsage = Get-Usage $app.elf
        if ($steeringCompiled) {
            & python (Join-Path $PSScriptRoot 'verify_bench_stack.py') (Split-Path -Parent $app.elf)
            if ($LASTEXITCODE -ne 0) { throw 'Steering stack validation failed' }
        }
        if ($BenchSteering) {
            if ((Get-FileHash -LiteralPath $app.bin -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $VerifiedCandidate -Algorithm SHA256).Hash) {
                throw 'Release differs from verified candidate; dist has not been replaced.'
            }
        }
        if ((Get-Item -LiteralPath $app.bin).Length -gt 221184) { throw 'App artifact exceeds its 216 KiB region.' }
        $release = Join-Path $credentialBuildRoot 'release-stage'
        New-Item -ItemType Directory -Force -Path $release | Out-Null
        $images = New-ABoxReleaseImages -AppPath $app.bin -BootPath $bootArtifact -Stage $release -ContractPath (Join-Path $product 'tools/release_contract.json') -Version $version
        $appOut = $images.app; $bootOut = $images.boot; $fullOut = $images.full
        $manifest = [ordered]@{
            manifest_version = 3; product = 'patrol_vehicle_chassis'; version = $version; configuration = $Configuration
            steering_executor_compiled = $steeringCompiled; drive_executor_compiled = $driveCompiled
            startup_motion_permit = $false; actuation_enabled = $actuationCompiled; active_car = 0
            boot_version = 'abox-boot-2.3.0'; ota_scheme = 'boot_v2_ufs'; ota_downloader = 'app'
            app_range = '0x08008000..0x0803DFFF'; config_recovery_page = '0x0803E000'; boot_state_pages = @('0x0803E800', '0x0803F000'); config_page = '0x0803F800'
            product_commit = $productCommit; product_dirty = $false; platform_commit = $platformCommit; platform_dirty = $false
            toolchain = (& arm-none-eabi-gcc --version | Select-Object -First 1).Trim(); build_options = $appDefinitions
            version_marker = "PATROL_VEHICLE_CHASSIS_FW_VERSION=$version"
            flash_ram = [ordered]@{ boot = $bootUsage; app = $appUsage; boot_limit_bytes = 32768; app_limit_bytes = 221184; ram_limit_bytes = 65536 }
            artifacts = @((Get-Artifact $appOut '0x08008000'), (Get-Artifact $bootOut '0x08000000'), (Get-Artifact $fullOut '0x08000000'))
        }
        $manifestJson = ($manifest | ConvertTo-Json -Depth 6).Replace("`r`n", "`n") + "`n"
        [System.IO.File]::WriteAllText((Join-Path $release 'release.manifest.json'), $manifestJson, [System.Text.UTF8Encoding]::new($false))
        & python (Join-Path $PSScriptRoot 'artifact_store.py') --root $product --stage $release --destination dist --validate-only
        if ($LASTEXITCODE -ne 0) { throw 'Patrol release policy validation failed; dist was not replaced.' }
        & python (Join-Path $platform 'tools/publish_release.py') `
            --contract (Join-Path $product 'tools/release_contract.json') `
            --stage $release --product-root $product --validate-only
        if ($LASTEXITCODE -ne 0) { throw 'Release publication failed; previous dist was preserved or restored.' }
        & python (Join-Path $PSScriptRoot 'artifact_store.py') --root $product --stage $release --destination dist
        if ($LASTEXITCODE -ne 0) { throw 'Artifact store publication failed; previous dist was preserved.' }
        Write-Host "Release $version written to $(Join-Path $product 'dist')"
    }

}
finally {
    if (Test-Path -LiteralPath $credentialBuildRoot) {
        $resolvedTemporary = (Resolve-Path -LiteralPath $credentialBuildRoot).Path
        $systemTemporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolvedTemporary.StartsWith($systemTemporary, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected credential build path: $resolvedTemporary"
        }
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
    }
}
