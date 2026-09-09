[CmdletBinding()]
param([ValidatePattern('^patrol-chassis-[0-9]+\.[0-9]+\.[0-9]+$')][string]$AppVersion = 'patrol-chassis-0.2.16', [switch]$Drive)
$ErrorActionPreference = 'Stop'
$product = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $product 'app/build/BenchSteering'
$destination = if ($Drive) { 'out/candidates/bench-drive' } else { 'out/candidates/bench-steering' }
$output = Join-Path ([IO.Path]::GetTempPath()) ('patrol-chassis-bench-' + [guid]::NewGuid().ToString('N'))
try {
& cmake -S (Join-Path $product 'app') -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$product/app/cmake/gcc-arm-none-eabi.cmake" `
    '-DCMAKE_BUILD_TYPE=Release' "-DPATROL_CHASSIS_APP_VERSION=$AppVersion" `
    '-DPATROL_CHASSIS_ACTUATION_ENABLED=OFF' '-DPATROL_CHASSIS_BENCH_STEERING_ENABLED=ON' `
    "-DPATROL_CHASSIS_BENCH_DRIVE_ENABLED=$($Drive.IsPresent)" `
    '-DPATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED=ON' '-DPATROL_CHASSIS_APP_FAULT_INJECTION_TEST=OFF' `
    '-DPATROL_CHASSIS_MQTT_USERNAME=' '-DPATROL_CHASSIS_MQTT_PASSWORD='
if ($LASTEXITCODE -ne 0) { throw 'Bench configure failed' }
& cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Bench build failed' }
& python (Join-Path $PSScriptRoot 'verify_bench_stack.py') $build
if ($LASTEXITCODE -ne 0) { throw 'Bench stack validation failed' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Join-Path $build 'Patrol_Vehicle_Chassis_App.bin'
$artifact = Join-Path $output 'Patrol_Vehicle_Chassis_App.bin'
Copy-Item -LiteralPath $source -Destination $artifact -Force
$bytes = [IO.File]::ReadAllBytes($artifact)
if ($bytes.Length -gt 223232 -or [Text.Encoding]::ASCII.GetString($bytes).IndexOf("PATROL_VEHICLE_CHASSIS_FW_VERSION=$AppVersion") -lt 0) { throw 'Invalid bench image' }
[ordered]@{
    product='patrol_vehicle_chassis'; version=$AppVersion; candidate=$true
    steering_only=(-not $Drive.IsPresent); startup_permit=$false; drive_actuation=$Drive.IsPresent
    drive_limit_rpm=$(if ($Drive) {200} else {0})
    commit=(& git -C $product rev-parse HEAD | Out-String).Trim()
    dirty=(-not [string]::IsNullOrWhiteSpace((& git -C $product status --porcelain | Out-String)))
    app_start='0x08008000'; app_end='0x0803E7FF'; config_page='0x0803F800'
    size=$bytes.Length; sha256=(Get-FileHash $artifact -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content (Join-Path $output 'candidate.manifest.json') -Encoding utf8
& python (Join-Path $PSScriptRoot 'artifact_store.py') --root $product --stage $output --destination $destination
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
