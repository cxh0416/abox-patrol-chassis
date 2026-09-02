[CmdletBinding()]
param(
    [ValidatePattern('^patrol-chassis-[0-9]+\.[0-9]+\.[0-9]+(?:[A-Za-z0-9._+-]*)?$')]
    [string] $AppVersion = 'patrol-chassis-0.2.1',
    [switch] $EnableAutoSeed
)

$ErrorActionPreference = 'Stop'
$product = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'ota_credentials.ps1')
$credential = Get-PatrolChassisOtaBuildCredential
$username = $credential.UserName
$password = $credential.GetNetworkCredential().Password

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("patrol-chassis-debug-" + [guid]::NewGuid().ToString('N'))
$appBuild = Join-Path $temporaryRoot 'app'
$output = Join-Path $product "dist/debug/$AppVersion-online-debug"
$bootTarget = Join-Path $product 'platform/targets/stm32f105_abox_boot'
$boot = Join-Path $bootTarget 'dist/ABox_Boot.bin'
$bootVerifier = Join-Path $bootTarget 'tools/verify_boot_artifact.py'

function Invoke-ArmBuild([string] $source, [string] $build, [string] $configuration, [string[]] $definitions) {
    $toolchain = Join-Path $source 'cmake/gcc-arm-none-eabi.cmake'
    $arguments = @('-S', $source, '-B', $build, '-G', 'Ninja', "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
                   "-DCMAKE_BUILD_TYPE:STRING=$configuration") + $definitions
    & cmake @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Configure failed: $source" }
    & cmake --build $build --parallel 4 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $source" }
}

try {
    New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
    & (Join-Path $product 'platform/tools/validate_flash_layout.ps1') `
        -BootLinker (Join-Path $bootTarget 'STM32F105xx_FLASH.ld') `
        -AppLinker (Join-Path $product 'app/STM32F105xx_FLASH.ld') | Out-Host
    if (-not $?) { throw 'Flash layout validation failed.' }

    & python $bootVerifier $boot | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Frozen platform Boot validation failed.' }
    $seed = if ($EnableAutoSeed) { 'ON' } else { 'OFF' }
    Invoke-ArmBuild (Join-Path $product 'app') $appBuild 'Debug' @(
        "-DPATROL_CHASSIS_APP_VERSION:STRING=$AppVersion",
        "-DPATROL_CHASSIS_MQTT_USERNAME:STRING=$username",
        "-DPATROL_CHASSIS_MQTT_PASSWORD:STRING=$password",
        "-DPATROL_CHASSIS_OTA_AUTO_SEED:BOOL=$seed"
    )

    $app = Join-Path $appBuild 'Patrol_Vehicle_Chassis_App.bin'
    $bootBytes = [IO.File]::ReadAllBytes($boot)
    $appBytes = [IO.File]::ReadAllBytes($app)
    if ($bootBytes.Length -gt 0x8000) { throw 'Boot artifact exceeds its 32 KiB region.' }
    if ($appBytes.Length -ge 0x36800) { throw 'App artifact exceeds its 218 KiB region.' }

    $marker = "PATROL_VEHICLE_CHASSIS_FW_VERSION=$AppVersion"
    if (-not [Text.Encoding]::ASCII.GetString($appBytes).Contains($marker)) { throw 'App version marker is missing.' }
    $sp = [BitConverter]::ToUInt32($appBytes, 0)
    $reset = [BitConverter]::ToUInt32($appBytes, 4)
    if ($sp -lt 0x20000000 -or $sp -gt 0x20010000 -or ($reset -band 1) -ne 1 -or
        $reset -lt 0x08008000 -or $reset -gt 0x0803E7FF) {
        throw ('Invalid App vectors: SP=0x{0:X8} Reset=0x{1:X8}' -f $sp, $reset)
    }

    New-Item -ItemType Directory -Force -Path $output | Out-Null
    Copy-Item -LiteralPath $boot -Destination (Join-Path $output 'ABox_Boot.bin') -Force
    Copy-Item -LiteralPath $app -Destination (Join-Path $output 'Patrol_Vehicle_Chassis_App.bin') -Force
    $full = New-Object byte[] (0x8000 + $appBytes.Length)
    [Array]::Copy($bootBytes, 0, $full, 0, $bootBytes.Length)
    [Array]::Copy($appBytes, 0, $full, 0x8000, $appBytes.Length)
    $fullPath = Join-Path $output 'Patrol_Vehicle_Chassis_Full.bin'
    [IO.File]::WriteAllBytes($fullPath, $full)
    if ([BitConverter]::ToUInt32($full, 0x8000) -ne $sp -or [BitConverter]::ToUInt32($full, 0x8004) -ne $reset) {
        throw 'Full image App offset validation failed.'
    }

    $item = Get-Item -LiteralPath $fullPath
    $manifest = [ordered]@{
        kind = 'online-debug'
        app_version = $AppVersion
        boot_version = 'abox-boot-2.3.0'
        app_offset = '0x00008000'
        initial_sp = ('0x{0:X8}' -f $sp)
        reset_vector = ('0x{0:X8}' -f $reset)
        auto_seed = [bool]$EnableAutoSeed
        uart5 = '115200 8N1 (PC12 TX / PD2 RX)'
        full_length = $item.Length
        full_sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
        credential = 'embedded-device-account; redacted'
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'debug.manifest.json') -Encoding UTF8
    Write-Host "Debug Full written to $fullPath"
    Write-Host ('SP=0x{0:X8} Reset=0x{1:X8} SHA256={2}' -f $sp, $reset, $manifest.full_sha256)
}
finally {
    $password = $null
    if (Test-Path -LiteralPath $temporaryRoot) {
        $resolvedTemporary = (Resolve-Path -LiteralPath $temporaryRoot).Path
        $systemTemporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolvedTemporary.StartsWith($systemTemporary, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected temporary path: $resolvedTemporary"
        }
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
    }
}
