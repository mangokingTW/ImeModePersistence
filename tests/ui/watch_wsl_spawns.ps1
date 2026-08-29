# Records every wsl.exe / Windows Terminal process that appears, with the chain
# of parents that started it.
#
# Why: on the ARM64 runner a Windows Terminal window running
# C:\Windows\system32\wsl.exe keeps coming back -- six distinct handles inside
# one capture -- and closing them is whack-a-mole. The desktop snapshots taken
# before and after the capture only say a window existed, never who launched
# it. This polls while the capture runs and writes down the parent chain and
# command line the moment a new process shows up.
#
# Read-only: it looks at processes, never touches them, so it cannot itself be
# the reason a window appears or disappears.
#
#   pwsh -File tests\ui\watch_wsl_spawns.ps1 -OutFile diag\wsl-watch.log -DurationSeconds 600
param(
  [string]$OutFile = "diag/wsl-watch.log",
  [int]$DurationSeconds = 900,
  [double]$IntervalSeconds = 1.0
)
$ErrorActionPreference = 'Continue'

$dir = Split-Path -Parent $OutFile
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

$watched = 'wsl', 'wslhost', 'WindowsTerminal', 'wt', 'conhost', 'OpenConsole'
$seen = @{}
$deadline = (Get-Date).AddSeconds($DurationSeconds)

function Write-Line([string]$text) {
  $stamp = (Get-Date).ToString('HH:mm:ss.fff')
  Add-Content -Path $OutFile -Value "$stamp $text"
}

Write-Line "watching for: $($watched -join ', ')"

# Parent ids come from Win32_Process; a pid can be reused, so the creation time
# is recorded too rather than trusting the id alone.
function Get-Chain([int]$processId) {
  $chain = @()
  $current = $processId
  for ($depth = 0; $depth -lt 6 -and $current -gt 0; $depth++) {
    $p = Get-CimInstance Win32_Process -Filter "ProcessId = $current" -ErrorAction SilentlyContinue
    if (-not $p) { break }
    $chain += "$($p.Name)($($p.ProcessId))"
    $current = [int]$p.ParentProcessId
    if ($current -eq 0) { break }
  }
  return ($chain -join ' <- ')
}

while ((Get-Date) -lt $deadline) {
  $procs = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $watched -contains ($_.Name -replace '\.exe$', '') }

  foreach ($p in $procs) {
    $key = "$($p.ProcessId)/$($p.CreationDate)"
    if ($seen.ContainsKey($key)) { continue }
    $seen[$key] = $true
    Write-Line "NEW  $($p.Name) pid=$($p.ProcessId) created=$($p.CreationDate)"
    Write-Line "     cmd    = $($p.CommandLine)"
    Write-Line "     parents= $(Get-Chain ([int]$p.ParentProcessId))"
  }

  Start-Sleep -Seconds $IntervalSeconds
}

Write-Line "done (saw $($seen.Count) matching processes)"
