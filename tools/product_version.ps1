function Resolve-PatrolChassisAppVersion {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [string] $ProductRoot,
        [string] $Override = ''
    )

    $version = $Override.Trim()
    if ([string]::IsNullOrWhiteSpace($version)) {
        $versionFile = Join-Path $ProductRoot 'VERSION'
        if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
            throw "Missing product version file: $versionFile"
        }
        $version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    }
    if ($version -notmatch '^patrol-chassis-[0-9]+\.[0-9]+\.[0-9]+(?:[A-Za-z0-9._+-]*)?$') {
        throw "Invalid patrol chassis App version: $version"
    }
    return $version
}
