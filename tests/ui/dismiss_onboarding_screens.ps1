# Ad-hoc fix for the ARM64 spike: GitHub's windows-11-arm hosted runner image
# apparently doesn't suppress Windows' first-interactive-logon onboarding flow
# the way the mature x64 image does. It shows up as a top-level window with
# class "Shell_OOBEProxy" (observed title "Microsoft account", first page seen
# was "Choose privacy settings for your device") that sits in the foreground
# across every virtual desktop and swallows input meant for real apps.
#
# Rather than guess how many pages it has, repeatedly find the window and
# click whatever known "move on" button is present, until it's gone or we
# time out.
param(
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$root = [System.Windows.Automation.AutomationElement]::RootElement
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$clicked = 0
$buttonNames = @("Next", "Accept", "Skip for now", "Skip", "OK", "I agree", "Continue", "Not now", "Decline", "Ask me later", "Close")

while ((Get-Date) -lt $deadline) {
    $classCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ClassNameProperty, "Shell_OOBEProxy")
    $proxy = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $classCond)

    if ($null -eq $proxy) {
        Write-Host "No Shell_OOBEProxy window found (clicked $clicked button(s) total) -- assuming clear."
        break
    }

    Write-Host "Shell_OOBEProxy present (Name='$($proxy.Current.Name)'); looking for a button to advance..."
    $found = $null
    $foundName = $null
    foreach ($name in $buttonNames) {
        $cond = New-Object System.Windows.Automation.AndCondition(@(
            (New-Object System.Windows.Automation.PropertyCondition(
                [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                [System.Windows.Automation.ControlType]::Button)),
            (New-Object System.Windows.Automation.PropertyCondition(
                [System.Windows.Automation.AutomationElement]::NameProperty, $name))
        ))
        $btn = $proxy.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($btn) { $found = $btn; $foundName = $name; break }
    }

    if ($found) {
        try {
            Write-Host "  invoking button '$foundName'"
            $invoke = $found.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
            $invoke.Invoke()
            $clicked++
        } catch {
            Write-Host "  invoke failed: $($_.Exception.Message)"
        }
    } else {
        Write-Host "  no known button found on this page yet; waiting..."
    }
    Start-Sleep -Seconds 2
}

if ((Get-Date) -ge $deadline) {
    Write-Host "Timed out after $TimeoutSeconds s with Shell_OOBEProxy still present (clicked $clicked button(s))."
}
