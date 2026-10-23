param([string]$Port='COM7', [int]$Baud=115200, [byte]$Slave=1)
$ErrorActionPreference='Stop'
$capturePath=Join-Path 'E:\1_work\ChassisCar_test\logs' ('driver-position-readonly-'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffffff'))
[void](New-Item -ItemType Directory -Path $capturePath)
$encoding=[System.Text.UTF8Encoding]::new($false)
$writer=[System.IO.StreamWriter]::new((Join-Path $capturePath 'frames.jsonl'),$false,$encoding)
$serial=[System.IO.Ports.SerialPort]::new($Port,$Baud,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$serial.Handshake=[System.IO.Ports.Handshake]::None
$serial.DtrEnable=$false;$serial.RtsEnable=$false
$serial.ReadTimeout=100;$serial.WriteTimeout=500
$timer=[System.Diagnostics.Stopwatch]::StartNew()
$result=[ordered]@{started_at=(Get-Date -Format o);port=$Port;baud=$Baud;protocol='Modbus ASCII';slave=$Slave;function=3;register='0x1018';quantity=2;parameter_writes=0;motor_commands=0;responses=@();error=$null}
function Record([string]$direction,[byte[]]$data) {
    $entry=[ordered]@{at_s=$timer.Elapsed.TotalSeconds;direction=$direction;hex=[BitConverter]::ToString($data).Replace('-',' ');ascii=[System.Text.Encoding]::ASCII.GetString($data)}
    $writer.WriteLine(($entry|ConvertTo-Json -Compress));$writer.Flush()
}
try {
    $serial.Open()
    for ($sample=0;$sample -lt 3;$sample++) {
        # Only function 03 and this documented position register may be sent.
        [byte[]]$payload=@($Slave,3,0x10,0x18,0,2)
        if ($payload[1] -ne 3 -or $payload[2] -ne 0x10 -or $payload[3] -ne 0x18) { throw 'Read-only request guard failed' }
        $sum=0;foreach($value in $payload){$sum+=$value}
        $lrc=(-$sum)-band 255
        $hex=([BitConverter]::ToString($payload).Replace('-',''))+('{0:X2}' -f $lrc)
        [byte[]]$request=[System.Text.Encoding]::ASCII.GetBytes(':'+$hex+"`r`n")
        Record 'TX_READ_FUNCTION_03' $request
        $serial.Write($request,0,$request.Length)
        $rx=[System.Collections.Generic.List[byte]]::new()
        $deadline=$timer.Elapsed.TotalSeconds+0.8
        while($timer.Elapsed.TotalSeconds -lt $deadline) {
            $available=$serial.BytesToRead
            if ($available -gt 0) {
                $buffer=New-Object byte[] $available
                $count=$serial.Read($buffer,0,$buffer.Length)
                if($count -gt 0){$rx.AddRange([byte[]]$buffer[0..($count-1)])}
                if($rx.Count -ge 2 -and $rx[$rx.Count-1] -eq 10 -and $rx[$rx.Count-2] -eq 13){break}
            }
            Start-Sleep -Milliseconds 10
        }
        [byte[]]$received=$rx.ToArray()
        if($received.Length -gt 0){Record 'RX' $received}
        $result.responses+=@{sample=$sample;bytes=$received.Length;hex=[BitConverter]::ToString($received).Replace('-',' ');ascii=[System.Text.Encoding]::ASCII.GetString($received)}
        if($received.Length -eq 0){break}
        Start-Sleep -Milliseconds 200
    }
}catch{$result.error=$_.Exception.Message}
finally{
    if($serial.IsOpen){$serial.Close()};$serial.Dispose();$writer.Dispose()
    $result.ended_at=Get-Date -Format o
    $result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $capturePath 'summary.json') -Encoding UTF8
    [ordered]@{directory=$capturePath;error=$result.error}|ConvertTo-Json|Set-Content -LiteralPath 'out/logs/driver-position-readonly-latest.json' -Encoding UTF8
    $result|ConvertTo-Json -Depth 6
    Write-Output "OUTPUT $capturePath"
}
if($result.error){exit 1}
