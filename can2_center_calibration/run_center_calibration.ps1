[CmdletBinding()]
param(
    [ValidateSet('status', 'current', 'reset-fault', 'interactive')]
    [string] $Mode = 'current',
    [ValidateSet(1, 2)]
    [int] $Channel = 1,
    [string] $Dll = 'E:\canalyst-II-Driver\CAN\二次开发库文件\x64(64bit)\ControlCAN.dll',
    [ValidateRange(0.2, 10.0)]
    [double] $ResponseTimeout = 1.0,
    [ValidateRange(1, 10)]
    [int] $QueryRetries = 3,
    [switch] $ResetFault,
    [switch] $Yes
)

$ErrorActionPreference = 'Stop'
$python = Get-Command python -ErrorAction Stop
if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) {
    throw "ControlCAN.dll not found: $Dll"
}

$arguments = @(
    (Join-Path $PSScriptRoot 'can2_center_calibration.py'),
    '--channel', [string]($Channel - 1),
    '--dll', $Dll,
    '--response-timeout', $ResponseTimeout.ToString([Globalization.CultureInfo]::InvariantCulture),
    '--query-retries', [string]$QueryRetries
)
switch ($Mode) {
    'status' { $arguments += 'status' }
    'current' {
        $arguments += 'calibrate-current'
        if ($ResetFault) { $arguments += '--reset-fault' }
        if ($Yes) { $arguments += '--yes' }
    }
    'reset-fault' { $arguments += 'reset-fault' }
    'interactive' { $arguments += 'interactive' }
}

& $python.Source @arguments
exit $LASTEXITCODE
