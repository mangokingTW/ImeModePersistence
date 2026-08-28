# Ad-hoc fix for the ARM64 spike: GitHub's windows-11-arm hosted runner image
# apparently doesn't suppress Windows' first-interactive-logon onboarding flow
# the way the mature x64 image does. It shows up as a top-level window with
# class "Shell_OOBEProxy" (observed title "Microsoft account", first page seen
# was "Choose privacy settings for your device", rendered as embedded web
# content inside an Internet Explorer_Server control) that sits in the
# foreground and swallows input meant for real apps. Registry policy
# (DisablePrivacyExperience) did not dismiss an already-shown screen, and
# restarting explorer.exe just recreates it from scratch -- clicking through
# is the only thing that actually works.
#
# The button's accessible Name is a full descriptive phrase (e.g. "Next, tab
# through all privacy settings to continue"), not a plain "Next" -- exact
# matching against short labels never finds it. Rather than guess how many
# pages the flow has, repeatedly find the window and click whatever known
# "move on" control is present (by AutomationId first, then by substring match
# on Name), until the window is gone or we time out.
param(
    [int]$TimeoutSeconds = 90
)

$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$root = [System.Windows.Automation.AutomationElement]::RootElement
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$clicked = 0

# The button's accessible Name is a full descriptive phrase (e.g. "Next, tab
# through all privacy settings to continue"), not a plain "Next" -- exact
# matching against short labels never found it. AutomationId is stable and
# known for this specific page; substring matching on Name is the fallback for
# whatever page comes next in the flow, since we don't know its exact wording.
$knownAutomationIds = @("OobeSettingsAcceptButton")
$nameSubstrings = @("next", "accept", "skip", "continue", "i agree", "not now", "decline", "ask me later", "close", "sign in later", "do this later")

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
    $foundBy = $null

    foreach ($autoId in $knownAutomationIds) {
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $autoId)
        $btn = $proxy.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($btn) { $found = $btn; $foundBy = "AutomationId='$autoId'"; break }
    }

    if (-not $found) {
        $buttonCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)
        $allButtons = $proxy.FindAll([System.Windows.Automation.TreeScope]::Descendants, $buttonCond)
        foreach ($btn in $allButtons) {
            $name = $btn.Current.Name
            if ([string]::IsNullOrEmpty($name)) { continue }
            foreach ($sub in $nameSubstrings) {
                if ($name.ToLowerInvariant().Contains($sub)) {
                    $found = $btn; $foundBy = "Name contains '$sub' (full name: '$name')"; break
                }
            }
            if ($found) { break }
        }
    }

    if ($found) {
        try {
            Write-Host "  invoking button matched by $foundBy"
            $invoke = $found.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
            $invoke.Invoke()
            $clicked++
        } catch {
            Write-Host "  invoke failed: $($_.Exception.Message)"
        }
    } else {
        Write-Host "  no matching button found on this page yet; buttons present:"
        $buttonCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)
        $all = $proxy.FindAll([System.Windows.Automation.TreeScope]::Descendants, $buttonCond)
        foreach ($el in $all) {
            Write-Host "    Name='$($el.Current.Name)' AutomationId='$($el.Current.AutomationId)'"
        }
        Write-Host "  waiting..."
    }
    Start-Sleep -Seconds 2
}

if ((Get-Date) -ge $deadline) {
    Write-Host "Timed out after $TimeoutSeconds s with Shell_OOBEProxy still present (clicked $clicked button(s))."
}
