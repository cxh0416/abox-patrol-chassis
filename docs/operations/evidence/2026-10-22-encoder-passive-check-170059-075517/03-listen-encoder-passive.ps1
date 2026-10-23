param([string]$Port='COM7', [int]$Baud=19200, [int]$Duration=15)
$ErrorActionPreference='Stop'
$capturePath=Join-Path 'E:\1_work\ChassisCar_test\logs' ('encoder-passive-'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffffff'))
[void](New-Item -ItemType Directory -Path $capturePath)
$encoding=[System.Text.UTF8Encoding]::new($false)
$writer=[System.IO.StreamWriter]::new((Join-Path $capturePath 'bytes.jsonl'),$false,$encoding)
$serial=[System.IO.Ports.SerialPort]::new($Port,$Baud,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$serial.Handshake=[System.IO.Ports.Handshake]::None
$serial.DtrEnable=$false
$serial.RtsEnable=$false
$serial.ReadTimeout=100
$serial.ReadBufferSize=65536
$report=[ordered]@{started_at=(Get-Date -Format o);port=$Port;baud=$Baud;format='8N1';passive_only=$true;transmit_calls=0;duration_s=$Duration;bytes=0;chunks=0;error=$null}
$buffer=New-Object byte[] 4096
$timer=[System.Diagnostics.Stopwatch]::new()
try {
    $serial.Open()
    $timer.Start()
    while ($timer.Elapsed.TotalSeconds -lt $Duration) {
        try { $count=$serial.Read($buffer,0,$buffer.Length) } catch [System.TimeoutException] { continue }
        if ($count -gt 0) {
            $hex=[BitConverter]::ToString($buffer,0,$count).Replace('-',' ')
            $entry=[ordered]@{elapsed_s=$timer.Elapsed.TotalSeconds;data=$hex;length=$count}
            $writer.WriteLine(($entry | ConvertTo-Json -Compress));$writer.Flush()
            $report.bytes+=$count;$report.chunks++
        }
    }
} catch { $report.error=$_.Exception.Message }
finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose();$writer.Dispose()
    $report.ended_at=Get-Date -Format o
    $report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $capturePath 'summary.json') -Encoding UTF8
    $report | ConvertTo-Json
    [ordered]@{directory=$capturePath;error=$report.error} | ConvertTo-Json | Set-Content -LiteralPath 'out/logs/encoder-passive-latest.json' -Encoding UTF8
    Write-Output "OUTPUT $capturePath"
}
if ($report.error) { exit 1 }
