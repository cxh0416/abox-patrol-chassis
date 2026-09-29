[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$productIncludes = @('system', 'motion', 'cloud', 'mqtt', 'ota', 'board', 'common') |
    ForEach-Object { "-I$PSScriptRoot/../app/Core/User/$_/include" }
& python -m unittest discover -s "$PSScriptRoot\..\tests" -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$gcc = (Get-Command gcc -ErrorAction Stop).Source
$hostBuild = "$PSScriptRoot\..\platform\build\host"
& cmake -S "$PSScriptRoot\..\platform" -B $hostBuild -G Ninja -DBUILD_TESTING=ON "-DCMAKE_C_COMPILER=$gcc"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $hostBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$profileTest = Join-Path $hostBuild 'patrol_mqtt_profile_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\..\platform\components\mqtt\include" `
    "-I$PSScriptRoot\..\platform\tests" `
    "-I$PSScriptRoot\..\app\Core\User\cloud\include" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\patrol_mqtt_profile.c" `
    "$PSScriptRoot\..\platform\components\mqtt\src\abox_mqtt_v4.c" `
    "$PSScriptRoot\..\tests\patrol_mqtt_profile_test.c" -o $profileTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $profileTest
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

$mqttAdapterTest = Join-Path $hostBuild 'mqtt_adapter_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\..\tests\mqtt_adapter_stubs" `
    "-I$PSScriptRoot\..\app\Core\User\mqtt\include" `
    "-I$PSScriptRoot\..\platform\components\ec800\include" `
    "-I$PSScriptRoot\..\platform\components\mqtt\include" `
    "$PSScriptRoot\..\tests\mqtt_adapter_test.c" `
    "$PSScriptRoot\..\app\Core\User\mqtt\src\mqtt_core_adapter.c" `
    "$PSScriptRoot\..\platform\components\mqtt\src\abox_mqtt_ec800.c" `
    "$PSScriptRoot\..\platform\components\mqtt\src\abox_mqtt_ec800_rx.c" `
    "$PSScriptRoot\..\platform\components\mqtt\src\abox_mqtt_receipt.c" `
    "$PSScriptRoot\..\platform\components\ec800\src\abox_ec800_at.c" `
    -o $mqttAdapterTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $mqttAdapterTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$chassisTest = Join-Path $hostBuild 'chassis_safety_test.exe'
& $gcc -std=c11 -Wall -Wextra `
    '-DPATROL_CHASSIS_ACTUATION_ENABLED=1' `
    @productIncludes `
    "-I$PSScriptRoot\..\platform\components\cjson" `
    "$PSScriptRoot\..\app\Core\User\motion\src\absolute_encoder.c" `
    "$PSScriptRoot\..\app\Core\User\cloud\src\chassis_cloud_protocol.c" `
    "$PSScriptRoot\..\app\Core\User\motion\src\chassis_control.c" `
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

$powerOffTest = Join-Path $hostBuild 'power_output_off_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    "-I$PSScriptRoot\..\app\Core\User\board\include" `
    "$PSScriptRoot\..\app\Core\User\board\src\vehicle_power_output.c" `
    "$PSScriptRoot\..\tests\power_output_off_test.c" -o $powerOffTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $powerOffTest
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($enabled in @(0, 1)) {
    $boardPowerTest = Join-Path $hostBuild "vehicle_power_port_$enabled.exe"
    & $gcc -std=c11 -Wall -Wextra -Werror `
        "-DPATROL_CHASSIS_POWER_OUTPUT_ENABLED=$enabled" `
        '-DPATROL_CHASSIS_ACTUATION_ENABLED=1' `
        "-I$PSScriptRoot\..\app\Core\User\board\include" `
        "-I$PSScriptRoot\..\app\Core\User\common\include" `
        "-I$PSScriptRoot\..\platform\components\board_io\include" `
        "-I$PSScriptRoot\..\platform\targets\stm32f105_ec800_v1\include" `
        "$PSScriptRoot\..\app\Core\User\board\src\vehicle_power_port.c" `
        "$PSScriptRoot\..\app\Core\User\board\src\vehicle_power_output.c" `
        "$PSScriptRoot\..\platform\components\board_io\src\abox_board_io.c" `
        "$PSScriptRoot\..\tests\vehicle_power_port_test.c" -o $boardPowerTest
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $boardPowerTest
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

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
$benchConfigTest = Join-Path $hostBuild 'control_config_session_test.exe'
& $gcc -std=c11 -Wall -Wextra -Werror `
    '-DPATROL_CHASSIS_ACTUATION_ENABLED=1' `
    "-I$PSScriptRoot\..\tests\bench_stubs" `
    @productIncludes `
    "-I$PSScriptRoot\..\shared\include" `
    "-I$PSScriptRoot\..\platform\components\core\include" `
    "-I$PSScriptRoot\..\platform\components\ec800\include" `
    "-I$PSScriptRoot\..\platform\components\mqtt\include" `
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
    "$PSScriptRoot\..\tests\control_config_session_test.c" -o $benchConfigTest
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
