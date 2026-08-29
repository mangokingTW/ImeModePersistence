# Ad-hoc diagnostic: dump a screenshot plus every visible top-level window's
# title/class/owning process, and call out the current foreground window. Used
# by the ARM64 spike workflow to see exactly what the interactive desktop is
# showing at a given point in the job, instead of guessing from a video after
# the fact. Not part of the normal test suite.
param(
    [Parameter(Mandatory = $true)]
    [string]$OutPrefix
)

$ErrorActionPreference = 'Continue'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class DiagWin32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@

# Screenshot of the full virtual screen.
try {
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $pngPath = "$OutPrefix-screenshot.png"
    $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Host "Saved screenshot: $pngPath ($($bounds.Width)x$($bounds.Height))"
} catch {
    Write-Host "Screenshot failed: $($_.Exception.Message)"
}

# Every visible top-level window: title, class, owning process.
$lines = New-Object System.Collections.ArrayList
$fg = [DiagWin32]::GetForegroundWindow()

$callback = {
    param($hWnd, $lParam)
    if ([DiagWin32]::IsWindowVisible($hWnd)) {
        $len = [DiagWin32]::GetWindowTextLength($hWnd)
        if ($len -gt 0) {
            $sbTitle = New-Object System.Text.StringBuilder ($len + 1)
            [DiagWin32]::GetWindowText($hWnd, $sbTitle, $sbTitle.Capacity) | Out-Null
            $sbClass = New-Object System.Text.StringBuilder 256
            [DiagWin32]::GetClassName($hWnd, $sbClass, $sbClass.Capacity) | Out-Null
            $procId = 0
            [DiagWin32]::GetWindowThreadProcessId($hWnd, [ref]$procId) | Out-Null
            $procName = "?"
            try { $procName = (Get-Process -Id $procId -ErrorAction Stop).ProcessName } catch {}
            $marker = if ($hWnd -eq $fg) { " <== FOREGROUND" } else { "" }
            [void]$lines.Add("hwnd=$hWnd class=$($sbClass.ToString()) proc=$procName($procId) title=`"$($sbTitle.ToString())`"$marker")
        }
    }
    return $true
}

[DiagWin32]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null

$txtPath = "$OutPrefix-windows.txt"
$lines | Out-File -FilePath $txtPath -Encoding utf8
Write-Host "Saved window list ($($lines.Count) visible windows): $txtPath"
$lines | ForEach-Object { Write-Host $_ }
