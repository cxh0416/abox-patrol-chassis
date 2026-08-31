[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Vid,
    [Parameter(Mandatory = $true)] [string] $Output,
    [string] $HostName = '36.153.162.171',
    [ValidateRange(1, 65535)] [int] $Port = 19683,
    [string] $OtaOrigin = 'https://ota.zxwl.ntiov.com:20443',
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
        '--ota-origin', $OtaOrigin
    )
    if ($Force) { $arguments += '--force' }
    & python @arguments
    if ($LASTEXITCODE -ne 0) { throw "make_device_config.py failed with exit code $LASTEXITCODE" }
}
finally {
    $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME = $previousUsername
    $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD = $previousPassword
}
