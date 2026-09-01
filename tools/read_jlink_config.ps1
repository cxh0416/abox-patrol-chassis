[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Output,
    [string] $JLinkExe
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
    foreach ($candidate in @(
        'D:\Program Files\SEGGER\JLink\JLink.exe',
        'C:\Program Files\SEGGER\JLink\JLink.exe',
        'C:\Program Files (x86)\SEGGER\JLink\JLink.exe'
    )) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'SEGGER JLink.exe not found. Pass -JLinkExe with the full path.'
}

$JLinkExe = Resolve-JLinkCommander $JLinkExe
$outputPath = [IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path -Parent $outputPath
if (-not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('patrol-chassis-jlink-read-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
$commandFile = Join-Path $tempRoot 'read-config.jlink'
@(
    'device STM32F105RC',
    'if SWD',
    'speed 4000',
    'RSetType 3',
    'connect',
    'h',
    "savebin `"$outputPath`", 0x0803F800, 0x800",
    'g',
    'qc'
) | Set-Content -LiteralPath $commandFile -Encoding ASCII

try {
    & $JLinkExe -CommanderScript $commandFile
    if ($LASTEXITCODE -ne 0) { throw "J-Link failed with exit code $LASTEXITCODE" }
    if ((Get-Item -LiteralPath $outputPath).Length -ne 0x800) {
        throw "Unexpected Config page length: $outputPath"
    }
    Write-Host "Config page read: $outputPath"
}
finally {
    $resolved = (Resolve-Path -LiteralPath $tempRoot).Path
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
