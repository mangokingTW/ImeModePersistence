# Ad-hoc fix for the ARM64 spike: GitHub's windows-11-arm hosted runner image
# apparently doesn't suppress Windows' first-interactive-logon onboarding flow
# the way the mature x64 image does. It shows up as a top-level window with
# class "Shell_OOBEProxy" (observed title "Microsoft account", first page seen
# was "Choose privacy settings for your device", rendered as embedded web
# content inside an Internet Explorer_Server control) that sits in the
# foreground and swallows input meant for real apps.
#
# Confirmed dead ends (all directly tested, none changed anything):
#   - HKLM DisablePrivacyExperience alone
#   - HKLM + HKCU DisablePrivacyExperience + explorer.exe restart
#   - Killing CloudExperienceHostBroker.exe alongside explorer.exe (doesn't
#     even exist on this image)
#   - Shell_OOBEProxy's window is directly confirmed (GetWindowThreadProcessId)
#     to be owned by explorer.exe itself, every time, on every run -- not
#     LogonUI, winlogon, ShellExperienceHost, or WWAHost. Killing/restarting
#     explorer.exe just recreates the identical screen from scratch.
# Clicking through is the only thing left to try.
#
# The button's accessible Name is a full descriptive phrase (e.g. "Next, tab
# through all privacy settings to continue"), not a plain "Next" -- exact
# matching against short labels never finds it, AND a top-down
# FindAll(Descendants) search from the Shell_OOBEProxy element never finds the
# button at all (confirmed empty results every time), even though the button
# demonstrably exists -- AutomationElement.FocusedElement finds it instantly.
# This looks like a UIA traversal quirk specific to this CoreWindow/legacy-web
# hybrid content on this runner image, where top-down enumeration silently
# misses elements that direct focused-element access reaches fine.
#
# Strategy: keep using the Shell_OOBEProxy presence check purely as the loop's
# "are we still stuck" gate, but find what to click via FocusedElement (and a
# short walk up its ancestors, in case focus lands on a child of the real
# button) instead of searching down from the window.
param(
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$root = [System.Windows.Automation.AutomationElement]::RootElement
$walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$clicked = 0

$knownAutomationIds = @("OobeSettingsAcceptButton")
$nameSubstrings = @("next", "accept", "skip", "continue", "i agree", "not now", "decline", "ask me later", "close", "sign in later", "do this later")

function Test-IsMatchingButton($element) {
    if ($null -eq $element) { return $false }
    try {
        $c = $element.Current
    } catch {
        return $false
    }
    if ($c.ControlType -ne [System.Windows.Automation.ControlType]::Button) { return $false }
    if ($knownAutomationIds -contains $c.AutomationId) { return $true }
    if (-not [string]::IsNullOrEmpty($c.Name)) {
        $lower = $c.Name.ToLowerInvariant()
        foreach ($sub in $nameSubstrings) {
            if ($lower.Contains($sub)) { return $true }
        }
    }
    return $false
}

while ((Get-Date) -lt $deadline) {
    $classCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ClassNameProperty, "Shell_OOBEProxy")
    $proxy = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $classCond)

    if ($null -eq $proxy) {
        Write-Host "No Shell_OOBEProxy window found (clicked $clicked button(s) total) -- assuming clear."
        break
    }

    Write-Host "Shell_OOBEProxy present (Name='$($proxy.Current.Name)'); checking FocusedElement..."
    $found = $null
    try {
        $focused = [System.Windows.Automation.AutomationElement]::FocusedElement
        if (Test-IsMatchingButton $focused) {
            $found = $focused
        } else {
            $node = $focused
            for ($i = 0; $i -lt 4 -and $null -ne $node; $i++) {
                $node = $walker.GetParent($node)
                if (Test-IsMatchingButton $node) { $found = $node; break }
            }
        }
    } catch {
        Write-Host "  could not read FocusedElement: $($_.Exception.Message)"
    }

    if ($found) {
        try {
            $fc = $found.Current
            Write-Host "  invoking focused button: Name='$($fc.Name)' AutomationId='$($fc.AutomationId)'"
            $invoke = $found.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
            $invoke.Invoke()
            $clicked++
        } catch {
            Write-Host "  invoke failed: $($_.Exception.Message)"
        }
    } else {
        try {
            $fc = [System.Windows.Automation.AutomationElement]::FocusedElement.Current
            Write-Host "  focused element doesn't match: [$($fc.ControlType.ProgrammaticName)] Name='$($fc.Name)' AutomationId='$($fc.AutomationId)'"
        } catch {
            Write-Host "  no focused element readable"
        }
        Write-Host "  waiting..."
    }
    Start-Sleep -Seconds 2
}

if ((Get-Date) -ge $deadline) {
    Write-Host "Timed out after $TimeoutSeconds s with Shell_OOBEProxy still present (clicked $clicked button(s))."
}
