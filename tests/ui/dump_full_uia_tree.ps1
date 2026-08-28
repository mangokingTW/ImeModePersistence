# Ad-hoc: dump EVERY element in the UIA tree (regardless of class/control
# type), to see exactly what's exposed on whatever is currently occupying the
# foreground -- no filtering, no guessing at window class or button names.
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

function Dump-Tree($element, $depth, $maxDepth) {
    if ($depth -gt $maxDepth) { return }
    $indent = "  " * $depth
    try {
        $c = $element.Current
        Write-Host "$indent[$($c.ControlType.ProgrammaticName)] Name='$($c.Name)' AutomationId='$($c.AutomationId)' ClassName='$($c.ClassName)'"
    } catch {
        Write-Host "$indent<error reading element: $($_.Exception.Message)>"
        return
    }
    try {
        $children = $element.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($child in $children) {
            Dump-Tree $child ($depth + 1) $maxDepth
        }
    } catch {
        Write-Host "$indent  <error enumerating children: $($_.Exception.Message)>"
    }
}

$root = [System.Windows.Automation.AutomationElement]::RootElement
Write-Host "=== Top-level windows (desktop children) ==="
$topLevel = $root.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
foreach ($w in $topLevel) {
    $c = $w.Current
    Write-Host "[$($c.ControlType.ProgrammaticName)] Name='$($c.Name)' ClassName='$($c.ClassName)'"
}

Write-Host ""
Write-Host "=== Focused element (and its ancestry via TreeWalker) ==="
try {
    $focused = [System.Windows.Automation.AutomationElement]::FocusedElement
    $fc = $focused.Current
    Write-Host "Focused: [$($fc.ControlType.ProgrammaticName)] Name='$($fc.Name)' AutomationId='$($fc.AutomationId)' ClassName='$($fc.ClassName)'"

    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $node = $focused
    $chain = New-Object System.Collections.ArrayList
    while ($null -ne $node) {
        $nc = $node.Current
        [void]$chain.Add("[$($nc.ControlType.ProgrammaticName)] Name='$($nc.Name)' ClassName='$($nc.ClassName)'")
        $node = $walker.GetParent($node)
    }
    [array]::Reverse($chain)
    Write-Host "Ancestry (desktop -> focused):"
    foreach ($line in $chain) { Write-Host "  $line" }

    Write-Host ""
    Write-Host "=== Full subtree of the focused element's top-level window (depth<=6) ==="
    $node = $focused
    $top = $node
    while ($null -ne $walker.GetParent($top) -and $walker.GetParent($top).Current.ClassName -ne "") {
        $parent = $walker.GetParent($top)
        if ($parent.Current.NativeWindowHandle -ne 0 -and $top.Current.NativeWindowHandle -eq 0) {
            # keep climbing to the nearest window-backed ancestor
        }
        if ($parent -eq $root) { break }
        $top = $parent
    }
    Dump-Tree $top 0 6
} catch {
    Write-Host "Could not read focused element: $($_.Exception.Message)"
}
