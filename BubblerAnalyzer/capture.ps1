# Automated capture pull: trigger a mic recording on the totem, wait for it
# to finish, and download the WAV + DSP metadata into a timestamped folder
# under .\captures\. Usage:
#   .\capture.ps1                # 10s capture from bubblertotem.local
#   .\capture.ps1 -Seconds 30    # longer capture (5..30)
#   .\capture.ps1 -DeviceHost 192.168.1.50
param(
    [int]$Seconds = 10,
    [string]$DeviceHost = "bubblertotem.local"
)

$base = "http://$DeviceHost"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outDir = Join-Path $PSScriptRoot "captures\$stamp"

Write-Host "Triggering ${Seconds}s recording on $base ..."
Invoke-RestMethod -Method Post -Uri "$base/action/mic/record?seconds=$Seconds" -TimeoutSec 10 | Out-Null

# poll until the device reports the recording is done
$deadline = (Get-Date).AddSeconds($Seconds + 30)
do {
    Start-Sleep -Milliseconds 1500
    $st = Invoke-RestMethod -Uri "$base/api/status" -TimeoutSec 10
    $pct = [int]($st.micRecordProgress * 100)
    Write-Host "  recording... $pct% (bpm=$($st.bpm) conf=$($st.conf))"
    if ((Get-Date) -gt $deadline) { throw "Timed out waiting for the recording to finish" }
} while ($st.micRecording -or -not $st.micRecordReady)

New-Item -ItemType Directory -Force $outDir | Out-Null
Write-Host "Downloading capture to $outDir ..."
Invoke-WebRequest -Uri "$base/mic_recording.wav" -OutFile (Join-Path $outDir "mic_recording.wav") -TimeoutSec 60
Invoke-WebRequest -Uri "$base/mic_meta.csv" -OutFile (Join-Path $outDir "mic_meta.csv") -TimeoutSec 60

Get-ChildItem $outDir | Select-Object Name, Length | Format-Table -AutoSize
Write-Host "Done: $outDir"
$outDir
