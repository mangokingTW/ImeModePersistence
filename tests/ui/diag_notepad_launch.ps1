# Ad-hoc: does notepad.exe actually produce a window on this runner? The
# capture script does subprocess.Popen(["notepad.exe"]) -> sleep 1s ->
# pywinauto connect(process=pid) -> top_window(), and top_window() raised
# "No windows for that process could be found" even though connect() itself
# didn't throw (so the PID existed). This checks, step by step, with more
# generous waits and full process/window visibility, what's actually going on.
$ErrorActionPreference = 'Continue'

Write-Host "=== Launching notepad.exe ==="
$proc = Start-Process notepad.exe -PassThru
Write-Host "Started PID=$($proc.Id)"

for ($i = 1; $i -le 6; $i++) {
    Start-Sleep -Seconds 1
    $stillRunning = $false
    try {
        $p = Get-Process -Id $proc.Id -ErrorAction Stop
        $stillRunning = $true
        Write-Host "[t=${i}s] PID $($proc.Id) alive. MainWindowHandle=$($p.MainWindowHandle) MainWindowTitle='$($p.MainWindowTitle)'"
    } catch {
        Write-Host "[t=${i}s] PID $($proc.Id) NO LONGER RUNNING (exited)"
    }

    Write-Host "[t=${i}s] all processes named *notepad*:"
    Get-Process -Name "*notepad*" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "    PID=$($_.Id) Name=$($_.ProcessName) MainWindowHandle=$($_.MainWindowHandle) MainWindowTitle='$($_.MainWindowTitle)'"
    }
}

Write-Host ""
Write-Host "=== Full visible top-level window list ==="
.\tests\ui\diag_desktop_state.ps1 -OutPrefix "diag/notepad-launch"
