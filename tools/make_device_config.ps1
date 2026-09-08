[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Vid,
    [Parameter(Mandatory = $true)] [string] $Output,
    [string] $HostName = '36.153.162.171',
    [ValidateRange(1, 65535)] [int] $Port = 19683,
    [string] $OtaOrigin = 'https://ota.zxwl.ntiov.com:20443',
    [int] $SteeringCenterCount = 0,
    [uint32] $AbsoluteCenterCount = 0,
    [uint32] $SteeringCountsPerDegreeMilli = 0,
    [uint32] $AbsoluteCountsPerDegreeMilli = 0,
    [ValidateSet(-1, 1)] [int] $SteeringDirection = 1,
    [ValidateSet(-1, 1)] [int] $AbsoluteDirection = 1,
    [ValidateRange(1, 2000)] [int] $SteeringToleranceMdeg = 2000,
    [switch] $Calibrated,
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ota_credentials.ps1')
$credential = Get-PatrolChassisOtaBuildCredential
$previousUsername = $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME
$previousPassword = $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD

try {
    $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME = $credential.UserName
    $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD = $credential.GetNetworkCredential().Password
    $arguments = @(
        (Join-Path $PSScriptRoot 'make_device_config.py'),
        '--vid', $Vid,
        '--output', ([IO.Path]::GetFullPath($Output)),
        '--host', $HostName,
        '--port', [string]$Port,
        '--username', $credential.UserName,
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
}
finally {
    $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME = $previousUsername
    $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD = $previousPassword
}
