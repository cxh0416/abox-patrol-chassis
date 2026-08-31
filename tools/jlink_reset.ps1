[CmdletBinding()]
param([string]$JLinkExe)

$ErrorActionPreference = 'Stop'
function Resolve-JLinkCommander([string]$requested) {
    if (-not [string]::IsNullOrWhiteSpace($requested)) {
        if ([IO.Path]::IsPathRooted($requested) -or $requested.Contains('\')) {
            $candidate = (Resolve-Path -LiteralPath $requested -ErrorAction Stop).Path
        } else {
            $candidate = (Get-Command $requested -ErrorAction Stop).Source
        }
        if ([IO.Path]::GetFileName($candidate).ToLowerInvariant() -ne 'jlink.exe') { throw "Refusing non-SEGGER executable: $candidate" }
        return $candidate
    }
    foreach ($candidate in @('D:\Program Files\SEGGER\JLink\JLink.exe', 'C:\Program Files\SEGGER\JLink\JLink.exe', 'C:\Program Files (x86)\SEGGER\JLink\JLink.exe')) {
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'SEGGER JLink.exe not found.'
}

$JLinkExe = Resolve-JLinkCommander $JLinkExe
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('patrol-chassis-jlink-reset-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
$script = Join-Path $tempRoot 'reset.jlink'
@('device STM32F105RC', 'if SWD', 'speed 4000', 'connect', 'r', 'g', 'qc') | Set-Content -LiteralPath $script -Encoding ASCII
try {
    & $JLinkExe -CommanderScript $script
    if ($LASTEXITCODE -ne 0) { throw "J-Link reset failed with exit code $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}
