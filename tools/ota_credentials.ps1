Set-StrictMode -Version Latest

function Get-PatrolChassisOtaCredentialPath {
    [CmdletBinding()]
    param()

    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw 'LOCALAPPDATA is unavailable; the local OTA credential store requires Windows.'
    }
    return Join-Path $env:LOCALAPPDATA 'Patrol_Vehicle_Chassis\secrets\ota-device-credential.clixml'
}

function Assert-PatrolChassisOtaCredential {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [System.Management.Automation.PSCredential] $Credential
    )

    $username = $Credential.UserName
    $password = $Credential.GetNetworkCredential().Password
    if ([string]::IsNullOrWhiteSpace($username) -or [string]::IsNullOrWhiteSpace($password)) {
        throw 'The Patrol chassis OTA credential must contain both username and password.'
    }
    if ($username -ne 'patrol_vehicle_chassis_device') {
        throw "Expected the dedicated patrol_vehicle_chassis_device account, got '$username'."
    }
}

function Protect-PatrolChassisOtaCredentialFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [string] $Path
    )

    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    $acl = Get-Acl -LiteralPath $Path
    $acl.SetAccessRuleProtection($true, $false)
    $rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
        $identity,
        [System.Security.AccessControl.FileSystemRights]::FullControl,
        [System.Security.AccessControl.AccessControlType]::Allow
    )
    $acl.SetAccessRule($rule)
    Set-Acl -LiteralPath $Path -AclObject $acl
}

function Save-PatrolChassisOtaCredential {
    [CmdletBinding()]
    param(
        [System.Management.Automation.PSCredential] $Credential,
        [string] $Path = (Get-PatrolChassisOtaCredentialPath)
    )

    if ($env:OS -ne 'Windows_NT') {
        throw 'The local OTA credential store is Windows-only because it relies on current-user DPAPI encryption.'
    }
    if ($null -eq $Credential) {
        $Credential = Get-Credential -UserName 'patrol_vehicle_chassis_device' -Message 'Patrol Vehicle Chassis OTA build credential (saved with Windows current-user encryption)'
    }
    if ($null -eq $Credential) {
        throw 'Credential entry was cancelled.'
    }
    Assert-PatrolChassisOtaCredential -Credential $Credential

    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $Credential | Export-Clixml -LiteralPath $Path -Force
    Protect-PatrolChassisOtaCredentialFile -Path $Path

    $roundTrip = Import-Clixml -LiteralPath $Path
    if ($roundTrip -isnot [System.Management.Automation.PSCredential]) {
        throw "Credential verification failed after writing $Path"
    }
    Assert-PatrolChassisOtaCredential -Credential $roundTrip
    Write-Host "Encrypted Patrol chassis OTA credential saved for the current Windows user: $Path"
}

function Get-PatrolChassisOtaBuildCredential {
    [CmdletBinding()]
    param(
        [string] $Path = (Get-PatrolChassisOtaCredentialPath)
    )

    $hasUsername = -not [string]::IsNullOrWhiteSpace($env:OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME)
    $hasPassword = -not [string]::IsNullOrWhiteSpace($env:OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD)
    if ($hasUsername -xor $hasPassword) {
        throw 'Only one OTA credential environment variable is set; provide both or clear both to use the local encrypted store.'
    }
    if ($hasUsername -and $hasPassword) {
        $secure = ConvertTo-SecureString $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD -AsPlainText -Force
        $credential = New-Object System.Management.Automation.PSCredential(
            $env:OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME,
            $secure
        )
        Assert-PatrolChassisOtaCredential -Credential $credential
        return $credential
    }

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Patrol chassis OTA credential is unavailable. Run .\tools\save_ota_credentials.ps1 once, or set both OTA_MQTT_PATROL_CHASSIS_DEVICE_USERNAME and OTA_MQTT_PATROL_CHASSIS_DEVICE_PASSWORD."
    }
    try {
        $credential = Import-Clixml -LiteralPath $Path
    }
    catch {
        throw "Unable to decrypt the local Patrol chassis OTA credential as the current Windows user. Re-run .\tools\save_ota_credentials.ps1. $($_.Exception.Message)"
    }
    if ($credential -isnot [System.Management.Automation.PSCredential]) {
        throw "Invalid local Patrol chassis OTA credential file: $Path"
    }
    Assert-PatrolChassisOtaCredential -Credential $credential
    return $credential
}

