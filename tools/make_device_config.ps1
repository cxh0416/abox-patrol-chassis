[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Vid,
    [Parameter(Mandatory = $true)] [string] $Output,
    [string] $HostName = '',
    [ValidateRange(0, 65535)] [int] $Port = 0,
    [string] $OtaOrigin = 'https://ota.zxwl.ntiov.com:20443',
    [int] $SteeringCenterCount = 0,
    [uint32] $AbsoluteCenterCount = 0,
    [uint32] $SteeringCountsPerDegreeMilli = 2710800,
    [uint32] $AbsoluteCountsPerDegreeMilli = 55250,
    [ValidateSet(-1, 1)] [int] $SteeringDirection = 1,
    [ValidateSet(-1, 1)] [int] $AbsoluteDirection = 1,
    [ValidateRange(1, 2000)] [int] $SteeringToleranceMdeg = 2000,
    [switch] $Calibrated,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$arguments = @(
        (Join-Path $PSScriptRoot 'make_device_config.py'),
        '--vid', $Vid,
        '--output', ([IO.Path]::GetFullPath($Output)),
        '--host', $HostName,
        '--port', [string]$Port,
        '--ota-origin', $OtaOrigin,
        '--steering-center-count', [string]$SteeringCenterCount,
        '--absolute-center-count', [string]$AbsoluteCenterCount,
        '--steering-counts-per-degree-milli', [string]$SteeringCountsPerDegreeMilli,
        '--absolute-counts-per-degree-milli', [string]$AbsoluteCountsPerDegreeMilli,
        '--steering-direction', [string]$SteeringDirection,
        '--absolute-direction', [string]$AbsoluteDirection,
        '--steering-tolerance-mdeg', [string]$SteeringToleranceMdeg
    )
if ($Calibrated) { $arguments += '--calibrated' }
if ($Force) { $arguments += '--force' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "make_device_config.py failed with exit code $LASTEXITCODE" }
