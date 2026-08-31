[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
& python -m unittest discover -s "$PSScriptRoot\..\tests" -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$gcc = (Get-Command gcc -ErrorAction Stop).Source
$hostBuild = "$PSScriptRoot\..\platform\build\host-tests-ninja"
& cmake -S "$PSScriptRoot\..\platform" -B $hostBuild -G Ninja -DBUILD_TESTING=ON "-DCMAKE_C_COMPILER=$gcc"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $hostBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$cloudTest = Join-Path $hostBuild 'cloud_protocol_test.exe'
& $gcc -std=c11 -Wall -Wextra `
    "-I$PSScriptRoot\..\app\Core\User\Inc" `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\Src\cloud_protocol.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\cloud_protocol_test.c" `
    -o $cloudTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cloudTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$chassisTest = Join-Path $hostBuild 'chassis_safety_test.exe'
& $gcc -std=c11 -Wall -Wextra `
    "-I$PSScriptRoot\..\app\Core\User\Inc" `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\Src\absolute_encoder.c" `
    "$PSScriptRoot\..\app\Core\User\Src\chassis_cloud_protocol.c" `
    "$PSScriptRoot\..\app\Core\User\Src\chassis_control.c" `
    "$PSScriptRoot\..\app\Core\User\Src\chassis_power.c" `
    "$PSScriptRoot\..\app\Core\User\Src\patrol_canopen.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\chassis_safety_test.c" `
    -o $chassisTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $chassisTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& ctest --test-dir $hostBuild --output-on-failure
exit $LASTEXITCODE
