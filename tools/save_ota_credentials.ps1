[CmdletBinding()]
param(
    [string] $Path
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ota_credentials.ps1')

if ([string]::IsNullOrWhiteSpace($Path)) {
    Save-PatrolChassisOtaCredential
}
else {
    Save-PatrolChassisOtaCredential -Path ([IO.Path]::GetFullPath($Path))
}
