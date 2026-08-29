# Records every wsl.exe / Windows Terminal process that appears, with the chain
# of parents that started it -- event-driven, because the polling version could
# not answer the question it was built for.
#
# The launcher of the stray updater windows lives for well under a second, so a
# 1 Hz poll always found `parents=` already empty. This version subscribes to
# Win32_ProcessStartTrace (ETW, fires at creation) and keeps a birth table of
# EVERY process start it sees -- so when a wsl.exe arrives, its parent can be
# named from the table even if the parent is already gone.
#
# Read-only: it looks at processes, never touches them, so it cannot itself be
# the reason a window appears or disappears. Requires elevation (the runner is).
#
#   pwsh -File tests\ui\watch_wsl_spawns.ps1 -OutFile diag\wsl-watch.log -DurationSeconds 900
param(
  [string]$OutFile = "diag/wsl-watch.log",
  [int]$DurationSeconds = 900
)
$ErrorActionPreference = 'Continue'

$dir = Split-Path -Parent $OutFile
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

function Write-Line([string]$text) {
  $stamp = (Get-Date).ToString('HH:mm:ss.fff')
  Add-Content -Path $OutFile -Value "$stamp $text"
}

$interesting = 'wsl.exe', 'wslhost.exe', 'WindowsTerminal.exe', 'wt.exe', 'OpenConsole.exe'

# Birth table: pid -> what we saw start there. Dead parents stay nameable.
$known = @{}

# Resolve a parent chain, preferring the birth table (has the dead), falling
# back to a live query (has processes older than this watcher).
function Get-Chain([int]$processId) {
  $chain = @()
  $cur = $processId
  for ($depth = 0; $depth -lt 6 -and $cur -gt 0; $depth++) {
    if ($known.ContainsKey($cur)) {
      $e = $known[$cur]
      $chain += "$($e.Name)($cur)$(if ($e.Cmd) { " [$($e.Cmd)]" })"
      $cur = $e.PPid
      continue
    }
    $p = Get-CimInstance Win32_Process -Filter "ProcessId = $cur" -ErrorAction SilentlyContinue
    if (-not $p) { $chain += "<gone pid=$cur>"; break }
    $chain += "$($p.Name)($cur)"
    $cur = [int]$p.ParentProcessId
  }
  return ($chain -join ' <- ')
}

Write-Line "event watcher starting (Win32_ProcessStartTrace); interesting: $($interesting -join ', ')"

Register-CimIndicationEvent -Query "SELECT * FROM Win32_ProcessStartTrace" -SourceIdentifier WslTrace | Out-Null
try {
  $deadline = (Get-Date).AddSeconds($DurationSeconds)
  while ((Get-Date) -lt $deadline) {
    $ev = Wait-Event -SourceIdentifier WslTrace -Timeout 5
    if (-not $ev) { continue }
    Remove-Event -EventIdentifier $ev.EventIdentifier
    $ti = $ev.SourceEventArgs.NewEvent
    $procId = [int]$ti.ProcessID
    $ppid = [int]$ti.ParentProcessID

    # The trace event carries no command line; fetch it immediately while the
    # process is most likely to still exist. Null for the very short-lived.
    $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId = $procId" -ErrorAction SilentlyContinue).CommandLine
    $known[$procId] = @{ Name = $ti.ProcessName; PPid = $ppid; Cmd = $cmd }

    if ($interesting -contains $ti.ProcessName) {
      Write-Line "NEW  $($ti.ProcessName) pid=$procId ppid=$ppid"
      Write-Line "     cmd    = $cmd"
      Write-Line "     parents= $(Get-Chain $ppid)"
    }
  }
}
finally {
  Unregister-Event -SourceIdentifier WslTrace -ErrorAction SilentlyContinue
}

Write-Line "done (birth table: $($known.Count) process starts observed)"
