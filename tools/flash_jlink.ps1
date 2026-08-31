[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Binary,
    [ValidateSet('Boot', 'Full')] [string] $Mode = 'Boot',
    [string] $JLinkExe,
    [string] $StateA,
    [string] $StateB
)

$ErrorActionPreference = 'Stop'
function Resolve-JLinkCommander([string] $requested) {
    if (-not [string]::IsNullOrWhiteSpace($requested)) {
        if ([IO.Path]::IsPathRooted($requested) -or $requested.Contains('\')) {
            $candidate = (Resolve-Path -LiteralPath $requested -ErrorAction Stop).Path
        } else {
            $candidate = (Get-Command $requested -ErrorAction Stop).Source
        }
        if ([IO.Path]::GetFileName($candidate).ToLowerInvariant() -ne 'jlink.exe') {
            throw "Refusing non-SEGGER executable: $candidate"
        }
        return $candidate
    }
    $registryPaths = @(
        'HKLM:\SOFTWARE\SEGGER\J-Link',
        'HKLM:\SOFTWARE\WOW6432Node\SEGGER\J-Link',
        'HKCU:\SOFTWARE\SEGGER\J-Link'
    )
    foreach ($registryPath in $registryPaths) {
        try {
            $installPath = (Get-ItemProperty -LiteralPath $registryPath -Name InstallPath -ErrorAction Stop).InstallPath
            $candidate = Join-Path ([string]$installPath) 'JLink.exe'
            if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
        } catch { }
    }
    $known = @(
        'D:\Program Files\SEGGER\JLink\JLink.exe',
        'C:\Program Files\SEGGER\JLink\JLink.exe',
        'C:\Program Files (x86)\SEGGER\JLink\JLink.exe'
    )
    foreach ($candidate in $known) {
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'SEGGER JLink.exe not found. Pass -JLinkExe with the full path.'
}
$JLinkExe = Resolve-JLinkCommander $JLinkExe
$path = (Resolve-Path -LiteralPath $Binary).Path
$item = Get-Item -LiteralPath $path
if ($Mode -eq 'Boot' -and $item.Length -gt 0x8000) { throw 'Boot mode accepts at most 32 KiB.' }
if ($Mode -eq 'Full' -and $item.Length -lt 0x8000) { throw 'Full mode requires a complete image with the 32 KiB Boot prefix.' }
if ($Mode -eq 'Full' -and $item.Length -gt 0x3E800) { throw 'Full image exceeds Boot + App address range.' }
$statePaths = @()
if ($StateA) {
    $statePaths += [ordered]@{ path = (Resolve-Path -LiteralPath $StateA).Path; address = '0x0803E800' }
}
if ($StateB) {
    $statePaths += [ordered]@{ path = (Resolve-Path -LiteralPath $StateB).Path; address = '0x0803F000' }
}
foreach ($state in $statePaths) {
    if ((Get-Item -LiteralPath $state.path).Length -ne 0x800) { throw "BootState image must be exactly 0x800 bytes: $($state.path)" }
}
$address = '0x08000000'
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('patrol-chassis-jlink-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
$commandFile = Join-Path $tempRoot 'flash.jlink'
@(
    'device STM32F105RC',
    'if SWD',
    'speed 4000',
    'RSetType 3',
    'connect',
    'h',
    "loadbin `"$path`", $address",
    "verifybin `"$path`", $address",
    $(foreach ($state in $statePaths) { "loadbin `"$($state.path)`", $($state.address)"; "verifybin `"$($state.path)`", $($state.address)" }),
    'r',
    'g',
    'qc'
) | Set-Content -LiteralPath $commandFile -Encoding ASCII
try {
    & $JLinkExe -CommanderScript $commandFile
    if ($LASTEXITCODE -ne 0) { throw "J-Link failed with exit code $LASTEXITCODE" }
}
finally {
    $resolved = (Resolve-Path -LiteralPath $tempRoot).Path
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
