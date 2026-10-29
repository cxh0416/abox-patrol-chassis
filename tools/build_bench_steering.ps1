[CmdletBinding()]
param([string]$AppVersion = '', [switch]$Drive, [switch]$EnableActuation,
       [ValidateRange(0,4)] [int]$ActiveCar = 2)
$ErrorActionPreference = 'Stop'
if ($EnableActuation -and -not $Drive) { throw 'Motion-enabled candidate requires -Drive.' }
$product = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'product_version.ps1')
$AppVersion = Resolve-PatrolChassisAppVersion -ProductRoot $product -Override $AppVersion
$build = Join-Path $product 'app/build/BenchSteering'
$destination = if ($Drive) { 'out/candidates/bench-drive' } else { 'out/candidates/bench-steering' }
$output = Join-Path ([IO.Path]::GetTempPath()) ('patrol-chassis-bench-' + [guid]::NewGuid().ToString('N'))
try {
& cmake -S (Join-Path $product 'app') -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$product/app/cmake/gcc-arm-none-eabi.cmake" `
    '-DCMAKE_BUILD_TYPE=Release' "-DPATROL_CHASSIS_APP_VERSION_OVERRIDE=$AppVersion" "-DPATROL_ACTIVE_CAR=$ActiveCar" `
    "-DPATROL_CHASSIS_ACTUATION_ENABLED=$(if ($EnableActuation) { 'ON' } else { 'OFF' })" '-DPATROL_STEERING_EXECUTOR_ENABLED=ON' `
    "-DPATROL_DRIVE_EXECUTOR_ENABLED=$($Drive.IsPresent)" `
    '-DPATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED=ON' '-DPATROL_CHASSIS_APP_FAULT_INJECTION_TEST=OFF'
if ($LASTEXITCODE -ne 0) { throw 'Bench configure failed' }
& cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Bench build failed' }
& python -X utf8 (Join-Path $PSScriptRoot 'verify_bench_stack.py') $build
if ($LASTEXITCODE -ne 0) { throw 'Bench stack validation failed' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Join-Path $build 'Patrol_Vehicle_Chassis_App.bin'
$artifact = Join-Path $output 'Patrol_Vehicle_Chassis_App.bin'
Copy-Item -LiteralPath $source -Destination $artifact -Force
$bytes = [IO.File]::ReadAllBytes($artifact)
if ($bytes.Length -gt 221184 -or [Text.Encoding]::ASCII.GetString($bytes).IndexOf("PATROL_VEHICLE_CHASSIS_FW_VERSION=$AppVersion") -lt 0) { throw 'Invalid bench image' }
$candidateManifest = [ordered]@{
    product='patrol_vehicle_chassis'; version=$AppVersion; candidate=$true
    steering_executor_compiled=$true; drive_executor_compiled=$Drive.IsPresent
    startup_permit=$false; actuation_enabled=$EnableActuation.IsPresent; active_car=$ActiveCar
    commit=(& git -C $product rev-parse HEAD | Out-String).Trim()
    dirty=(-not [string]::IsNullOrWhiteSpace((& git -C $product status --porcelain | Out-String)))
    app_start='0x08008000'; app_end='0x0803DFFF'; config_recovery_page='0x0803E000'; config_page='0x0803F800'
    size=$bytes.Length; sha256=(Get-FileHash $artifact -Algorithm SHA256).Hash
}
$manifestJson = ($candidateManifest | ConvertTo-Json).Replace("`r`n", "`n") + "`n"
[IO.File]::WriteAllText((Join-Path $output 'candidate.manifest.json'), $manifestJson, [Text.UTF8Encoding]::new($false))
& python -X utf8 (Join-Path $PSScriptRoot 'artifact_store.py') --root $product --stage $output --destination $destination
if ($LASTEXITCODE -ne 0) { throw 'Bench publication failed; inspect rollback/backup before retrying.' }
Write-Host "Bench candidate: $(Join-Path $product $destination)"
}
finally {
    if (Test-Path -LiteralPath $output) {
        $temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        $resolvedOutput = [IO.Path]::GetFullPath($output)
        if (-not $resolvedOutput.StartsWith($temporaryBase, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected bench temporary path.' }
        Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
    }
}
