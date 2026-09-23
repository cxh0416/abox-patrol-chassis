[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$productIncludes = @('system', 'motion', 'cloud', 'mqtt', 'ota', 'board', 'common') |
    ForEach-Object { "-I$PSScriptRoot/../app/Core/User/$_/include" }
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
    @productIncludes `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\cloud_protocol.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\cloud_protocol_test.c" `
    -o $cloudTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cloudTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$qmtRecvTest = Join-Path $hostBuild 'mqtt_qmtrecv_test.exe'
& $gcc -std=c11 -Wall -Wextra `
    @productIncludes `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\mqtt\src\mqtt_qmtrecv.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\cloud_protocol.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\mqtt_qmtrecv_test.c" `
    -o $qmtRecvTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $qmtRecvTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$mqttPipelineTest = Join-Path $hostBuild 'mqtt_receive_pipeline_test.exe'
& $gcc -std=c11 -Wall -Wextra `
    @productIncludes `
    "-I$PSScriptRoot\..\platform\include" `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\platform\components\ec800\abox_ec800_at.c" `
    "$PSScriptRoot\..\platform\components\ec800\abox_ec800_rx.c" `
    "$PSScriptRoot\..\app\Core\User\mqtt\src\mqtt_qmtrecv.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\cloud_protocol.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\mqtt_receive_pipeline_test.c" `
    -o $mqttPipelineTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $mqttPipelineTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$chassisTest = Join-Path $hostBuild 'chassis_safety_test.exe'
& $gcc -std=c11 -Wall -Wextra `
    @productIncludes `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\motion\src\absolute_encoder.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\chassis_cloud_protocol.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\chassis_control.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\chassis_power.c" `
    "$PSScriptRoot\..\app\Core\User\common\src\fixed_point.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\patrol_canopen.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\patrol_diagnostics.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\steering_angle.c" `
    "$PSScriptRoot\..\app\Core\User\board\src\vehicle_power_output.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\chassis_safety_test.c" `
    -o $chassisTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $chassisTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& ctest --test-dir $hostBuild --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$benchTest = Join-Path $hostBuild 'bench_steering_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    @productIncludes `
    "$PSScriptRoot\..\app\Core\User\motion\src\bench_steering.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\steering_angle.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\patrol_canopen.c" `
    "$PSScriptRoot\..\tests\bench_steering_test.c" -o $benchTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $benchTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$benchConfigTest = Join-Path $hostBuild 'bench_config_session_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\..\tests\bench_stubs" `
    @productIncludes `
    "-I$PSScriptRoot\..\shared\include" `
    "-I$PSScriptRoot\..\platform\include" `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\system\src\patrol_config.c" `
    "$PSScriptRoot\..\app\Core\User\system\src\mqtt_config_adapter.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\remote_service.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\chassis_control.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\cloud_protocol.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\chassis_cloud_protocol.c" `
    "$PSScriptRoot\..\app\Core\User\common\src\fixed_point.c" `
    "$PSScriptRoot\..\app\Core\User\board\src\vehicle_power_output.c" `
    "$PSScriptRoot\..\platform\components\cjson\cJSON.c" `
    "$PSScriptRoot\..\tests\bench_config_session_test.c" -o $benchConfigTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $benchConfigTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$driveTest = Join-Path $hostBuild 'bench_drive_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    @productIncludes `
    "$PSScriptRoot\..\app\Core\User\motion\src\bench_drive.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\patrol_canopen.c" `
    "$PSScriptRoot\..\tests\bench_drive_test.c" -o $driveTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $driveTest
exit $LASTEXITCODE
