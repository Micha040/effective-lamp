# Prüft Setup.ps1 von FragDesk unter StrictMode 2 (pwsh unter Linux; Registry, Prozesse und COM gemockt).
# Aufruf: pwsh -NoProfile -File apps/fragdesk/tools/ps_check.ps1 -SetupPath apps/fragdesk/setup/Setup.ps1
param([Parameter(Mandatory = $true)][string]$SetupPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$root = Join-Path ([IO.Path]::GetTempPath()) ('fragdesk-pscheck-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$env:LOCALAPPDATA = Join-Path $root 'LocalAppData'
$env:SystemRoot = Join-Path $root 'Windows'
$env:TEMP = Join-Path $root 'Temp'
$startMenu = Join-Path $root 'StartMenu'
foreach ($d in $env:LOCALAPPDATA, $env:SystemRoot, $env:TEMP, $startMenu) { $null = New-Item -ItemType Directory -Path $d }
$src = Join-Path $root 'zip'
$null = New-Item -ItemType Directory -Path $src
foreach ($n in 'FragDesk.exe', 'fdanalyze.exe', 'README.md', 'THIRD-PARTY-NOTICES.txt') { $n | Set-Content -LiteralPath (Join-Path $src $n) }
$text = [IO.File]::ReadAllText($SetupPath)
$expectedVersion = ([regex]::Match($text, '\$AppVersion\s*=\s*''([^'']+)''')).Groups[1].Value
# Den Startmenü-Ordner gibt es unter Linux nicht: auf den Testordner umlenken
$origFolder = "[Environment]::GetFolderPath('Programs')"
if ([regex]::Matches($text, [regex]::Escape($origFolder)).Count -ne 1) { throw 'GetFolderPath-Aufruf nicht gefunden' }
$text = $text.Replace($origFolder, '$global:StartMenu')
$origOs = 'function Get-OsVersion { return [Environment]::OSVersion.Version }'
if ([regex]::Matches($text, [regex]::Escape($origOs)).Count -ne 1) { throw 'Get-OsVersion-Zeile nicht gefunden' }
$text = $text.Replace($origOs, 'function Get-OsVersion { return $global:FakeOsVersion }')
[IO.File]::WriteAllText((Join-Path $src 'Setup.ps1'), $text, (New-Object System.Text.UTF8Encoding $true))

$global:StartMenu = $startMenu
$installed = Join-Path $env:LOCALAPPDATA 'Programs\FragDesk'
$dataDir = Join-Path $env:LOCALAPPDATA 'FragDesk'
$psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$ukey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FragDesk'
$progKey = 'HKCU:\Software\Classes\FragDesk.Demo'
$appKey = 'HKCU:\Software\Classes\Applications\FragDesk.exe'
$demRoot = 'HKCU:\Software\Classes\.dem'
$demKey = "$demRoot\OpenWithProgids"
$link = Join-Path $startMenu 'FragDesk.lnk'

$global:FakeOsVersion = [version]'10.0.22631.0'
$global:ProcPath = Join-Path $installed 'FragDesk.exe'
$global:Hang = $false
$global:Reg = @{}
$global:Procs = New-Object System.Collections.ArrayList
$global:Calls = New-Object System.Collections.ArrayList
$global:Prompts = New-Object System.Collections.ArrayList
$global:Answers = New-Object System.Collections.Queue

# ------------------------------------------------------------------ Mocks
function global:Test-RegPath([string]$P) { return ($P -like 'HKCU:*' -or $P -like 'HKLM:*') }
function global:New-FakeProcess([string]$Name = 'FragDesk') {
    $o = [pscustomobject]@{ Id = (Get-Random -Minimum 1000 -Maximum 9999); ProcessName = $Name; ExitCode = 0; Handle = [IntPtr]1; Path = $global:ProcPath }
    $o | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param($ms) return $true }
    $o | Add-Member -MemberType ScriptMethod -Name Kill -Value { [void]$global:Calls.Add("kill $($this.ProcessName)"); $global:Procs.Remove($this) }
    # Fenster schließen: FragDesk beendet sich – außer es soll "hängen"
    $o | Add-Member -MemberType ScriptMethod -Name CloseMainWindow -Value {
        [void]$global:Calls.Add("close $($this.ProcessName)")
        if (-not $global:Hang) { $global:Procs.Remove($this) }
        return $true
    }
    return $o
}
function global:New-Item {
    [CmdletBinding()]
    param([Parameter(Position = 0)][string[]]$Path, [string]$ItemType, [switch]$Force, [object]$Value)
    $regs = @($Path | Where-Object { Test-RegPath $_ })
    if ($regs.Count -eq 0) { return Microsoft.PowerShell.Management\New-Item @PSBoundParameters }
    foreach ($p in $regs) {
        if ($global:Reg.ContainsKey($p)) {
            # wie der echte Registry-Provider: ein vorhandener Schlüssel wird mit -Force geleert (ohne -Force: Fehler)
            if (-not $Force) { Write-Error "Schlüssel existiert schon: $p"; continue }
            foreach ($sub in @($global:Reg.Keys | Where-Object { $_ -eq $p -or $_ -like "$p\*" })) { $global:Reg.Remove($sub) }
        }
        $parts = $p.Split('\')
        for ($i = 1; $i -le $parts.Count; $i++) {
            $k = ($parts[0..($i - 1)] -join '\')
            if (-not $global:Reg.ContainsKey($k)) { $global:Reg[$k] = @{} }
        }
    }
    return [pscustomobject]@{ PSPath = $regs[0] }
}
function global:Set-ItemProperty {
    [CmdletBinding()]
    param([string]$Path, [string]$Name, $Value, [string]$Type)
    if (-not (Test-RegPath $Path)) { throw "Set-ItemProperty außerhalb der Registry: $Path" }
    if (-not $global:Reg.ContainsKey($Path)) { throw "Registry-Schlüssel fehlt: $Path" }
    $global:Reg[$Path][$Name] = $Value
    if ($Type) { $global:Reg[$Path]["$Name#type"] = $Type }
}
function global:Remove-ItemProperty {
    [CmdletBinding()]
    param([string]$Path, [string]$Name)
    if ($global:Reg.ContainsKey($Path) -and $global:Reg[$Path].ContainsKey($Name)) { $global:Reg[$Path].Remove($Name) }
    else { Write-Error "Wert fehlt: $Path $Name" }
}
function global:Test-Path {
    [CmdletBinding()]
    param([Parameter(Position = 0)][string[]]$Path, [string[]]$LiteralPath)
    $p = @(if ($LiteralPath) { $LiteralPath } else { $Path })
    if (@($p | Where-Object { Test-RegPath $_ }).Count -gt 0) { return $global:Reg.ContainsKey($p[0]) }
    return Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
function global:Remove-Item {
    [CmdletBinding()]
    param([Parameter(Position = 0)][string[]]$Path, [string[]]$LiteralPath, [switch]$Recurse, [switch]$Force)
    $p = @(if ($LiteralPath) { $LiteralPath } else { $Path })
    if (@($p | Where-Object { Test-RegPath $_ }).Count -eq 0) { return Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters }
    foreach ($k in $p) {
        if (-not $global:Reg.ContainsKey($k)) { Write-Error "Schlüssel fehlt: $k"; continue }
        foreach ($sub in @($global:Reg.Keys | Where-Object { $_ -eq $k -or $_ -like "$k\*" })) { $global:Reg.Remove($sub) }
    }
}
function global:Get-Process {
    [CmdletBinding()]
    param([string[]]$Name)
    return @($global:Procs | Where-Object { $Name -contains $_.ProcessName })
}
function global:Start-Process {
    [CmdletBinding()]
    param([string]$FilePath, [string[]]$ArgumentList, [string]$WorkingDirectory, [switch]$PassThru, [string]$Verb, [string]$WindowStyle)
    [void]$global:Calls.Add(([IO.Path]::GetFileName($FilePath) + ' ' + ($ArgumentList -join ' ')))
    if ($FilePath -like '*FragDesk.exe') { [void]$global:Procs.Add((New-FakeProcess)) }
    if ($PassThru) { return (New-FakeProcess -Name 'helper') }
}
function global:Unblock-File { [CmdletBinding()] param([string]$LiteralPath) }
function global:Start-Sleep { [CmdletBinding()] param([int]$Seconds, [int]$Milliseconds) }
function global:Read-Host {
    [CmdletBinding()]
    param([Parameter(Position = 0)][string]$Prompt)
    [void]$global:Prompts.Add($Prompt)
    if ($global:Answers.Count -gt 0) { return $global:Answers.Dequeue() }
    return ''
}
function global:New-Object {
    [CmdletBinding()]
    param([Parameter(Position = 0)][string]$TypeName, [Parameter(Position = 1)][object[]]$ArgumentList, [string]$ComObject)
    if ($ComObject) {
        [void]$global:Calls.Add("COM $ComObject")
        $o = [pscustomobject]@{ Name = $ComObject }
        $o | Add-Member -MemberType ScriptMethod -Name CreateShortcut -Value {
            param($p)
            $l = [pscustomobject]@{ Path = $p; TargetPath = ''; WorkingDirectory = ''; IconLocation = ''; Description = '' }
            $l | Add-Member -MemberType ScriptMethod -Name Save -Value { Set-Content -LiteralPath $this.Path -Value ($this.TargetPath + '|' + $this.IconLocation) }
            return $l
        }
        return $o
    }
    return Microsoft.PowerShell.Utility\New-Object @PSBoundParameters
}

# ------------------------------------------------------------------ Ablauf
$script:failures = 0
$script:logFile = Join-Path $root 'setup-output.txt'
function Assert($Condition, [string]$What) {
    if ($Condition) { "  ok    $What" } else { "  FAIL  $What"; $script:failures++ }
}
function Invoke-Setup([string]$Dir, [string[]]$SetupArgs, [string]$Label) {
    $global:Calls.Clear(); $global:Prompts.Clear()
    $splat = @{}
    foreach ($a in $SetupArgs) { $splat[$a.TrimStart('-')] = $true }
    $out = & (Join-Path $Dir 'Setup.ps1') @splat *>&1
    $code = $LASTEXITCODE
    Add-Content -LiteralPath $script:logFile -Value ("===== $Label (Exit $code)`n" + ($out | Out-String))
    return [pscustomobject]@{ Code = $code; Text = ($out | Out-String); Location = (Get-Location).Path }
}
$home0 = (Get-Location).Path

# Vorhandene "Öffnen mit"-Einträge anderer Programme und die Standard-App müssen erhalten bleiben
foreach ($k in 'HKCU:', 'HKCU:\Software', 'HKCU:\Software\Classes', $demRoot, $demKey) {
    if (-not $global:Reg.ContainsKey($k)) { $global:Reg[$k] = @{} }
}
$global:Reg[$demKey]['CSDM.Demo'] = ''
$global:Reg[$demRoot]['(default)'] = 'CSDM.Demo'
$exe = Join-Path $installed 'FragDesk.exe'

'1) Installation -Quiet -NoStart aus dem ZIP-Ordner, eine alte FragDesk läuft noch:'
[void]$global:Procs.Add((New-FakeProcess))
$r = Invoke-Setup $src @('-Quiet', '-NoStart') 'install quiet'
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert ($r.Text -notmatch 'Fehler:') 'keine Fehlermeldung'
Assert (@($global:Calls | Where-Object { $_ -eq 'close FragDesk' }).Count -eq 1) 'alte FragDesk über ihr Fenster geschlossen'
Assert (@($global:Calls | Where-Object { $_ -like 'kill*' }).Count -eq 0) 'nichts hart beendet'
foreach ($n in 'FragDesk.exe', 'fdanalyze.exe', 'README.md', 'THIRD-PARTY-NOTICES.txt', 'Setup.ps1') { Assert (Test-Path -LiteralPath (Join-Path $installed $n)) "$n im Programmordner" }
Assert ($global:Reg.ContainsKey($ukey)) 'Eintrag unter Installierte Apps angelegt'
if ($global:Reg.ContainsKey($ukey)) {
    $u = $global:Reg[$ukey]
    $expected = '"' + $psExe + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installed 'Setup.ps1') + '" -Uninstall -Pause'
    Assert ($u['DisplayName'] -eq 'FragDesk' -and $u['DisplayVersion'] -eq $expectedVersion) "Name/Version ($($u['DisplayVersion']))"
    Assert ($u['UninstallString'] -eq $expected) "UninstallString = $($u['UninstallString'])"
    Assert ($u['NoModify#type'] -eq 'DWord' -and $u['EstimatedSize#type'] -eq 'DWord') 'DWord-Werte'
}
Assert ($global:Reg.ContainsKey("$progKey\shell\open\command") -and $global:Reg["$progKey\shell\open\command"]['(default)'] -eq ('"' + $exe + '" "%1"')) 'ProgID mit Öffnen-Befehl'
Assert ($global:Reg.ContainsKey("$appKey\SupportedTypes") -and $global:Reg["$appKey\SupportedTypes"].ContainsKey('.dem')) 'Applications\FragDesk.exe kennt .dem'
Assert ($global:Reg[$demKey].ContainsKey('FragDesk.Demo')) '.dem bietet FragDesk unter "Öffnen mit" an'
Assert ($global:Reg[$demKey].ContainsKey('CSDM.Demo')) 'fremder "Öffnen mit"-Eintrag bleibt erhalten'
Assert ($global:Reg[$demRoot]['(default)'] -eq 'CSDM.Demo') 'Standard-App für .dem unverändert'
Assert ((Test-Path -LiteralPath $link) -and ((Get-Content -LiteralPath $link) -eq "$exe|$exe,0")) 'Startmenü-Verknüpfung'
Assert ($global:Prompts.Count -eq 0) 'keine Rückfrage bei -Quiet'
Assert (@($global:Procs).Count -eq 0) 'nicht gestartet (-NoStart)'

'2) Installation mit Rückfragen; FragDesk hängt beim Schließen (wird hart beendet):'
[void]$global:Procs.Add((New-FakeProcess))
$global:Hang = $true
$global:Answers.Clear()
$r = Invoke-Setup $src @() 'install interactive'
$global:Hang = $false
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert (@($global:Calls | Where-Object { $_ -eq 'kill FragDesk' }).Count -eq 1) 'hängende FragDesk hart beendet'
Assert ($global:Prompts.Count -eq 1 -and $global:Prompts[0] -like '*öffnen*') "eine Rückfrage: $($global:Prompts -join ' | ')"
Assert (@($global:Calls | Where-Object { $_ -like 'FragDesk.exe*' }).Count -eq 1) 'FragDesk gestartet (Standardantwort J)'
Assert (@($global:Procs).Count -eq 1) 'genau eine FragDesk läuft'

'3) Reparatur aus dem Programmordner (-Quiet -NoStart); ein fremdes fdanalyze bleibt unberührt:'
$global:ProcPath = 'C:\Andere\fdanalyze.exe'
[void]$global:Procs.Add((New-FakeProcess -Name 'fdanalyze'))
$global:ProcPath = $exe
$r = Invoke-Setup $installed @('-Quiet', '-NoStart') 'repair'
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert ($r.Text -match 'Reparatur') 'Reparatur-Weg'
Assert (@($global:Procs | Where-Object { $_.ProcessName -eq 'fdanalyze' }).Count -eq 1) 'fremdes Programm gleichen Namens nicht angefasst'
$global:Procs.Clear()

'4) Windows 10 1909: Hinweis zum Ton:'
$global:FakeOsVersion = [version]'10.0.18363.0'
$r = Invoke-Setup $src @('-Quiet', '-NoStart') 'install 1909'
$global:FakeOsVersion = [version]'10.0.22631.0'
Assert ($r.Code -eq 0 -and $r.Text -match 'Spielton') 'Hinweis auf stumme Videos'

"5) Deinstallation wie aus 'Installierte Apps' (-Uninstall -Pause), Daten behalten:"
$null = New-Item -ItemType Directory -Path (Join-Path $dataDir 'matches') -Force
'x' | Set-Content -LiteralPath (Join-Path $dataDir 'fragdesk.log')
[void]$global:Procs.Add((New-FakeProcess))
$global:Answers.Clear()
Set-Location -LiteralPath $installed
$r = Invoke-Setup $installed @('-Uninstall', '-Pause') 'uninstall pause'
Set-Location -LiteralPath $home0
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert ($r.Location -eq $env:TEMP) "Arbeitsordner auf TEMP gewechselt ($($r.Location))"
Assert (-not (Test-Path -LiteralPath $installed)) 'Programmordner gelöscht'
Assert (-not (Test-Path -LiteralPath $link)) 'Startmenü-Verknüpfung gelöscht'
Assert (-not $global:Reg.ContainsKey($ukey)) 'Eintrag unter Installierte Apps entfernt'
Assert (-not $global:Reg.ContainsKey($progKey) -and -not $global:Reg.ContainsKey($appKey)) 'ProgID und Applications-Eintrag entfernt'
Assert (-not $global:Reg[$demKey].ContainsKey('FragDesk.Demo') -and $global:Reg[$demKey].ContainsKey('CSDM.Demo')) 'nur der eigene "Öffnen mit"-Eintrag entfernt'
Assert (Test-Path -LiteralPath (Join-Path $dataDir 'matches')) 'Auswertungen behalten (Standardantwort N)'
Assert ($global:Prompts.Count -eq 2 -and $global:Prompts[1] -like '*Enter*') "Rückfragen: $($global:Prompts -join ' | ')"
Assert (@($global:Procs).Count -eq 0) 'FragDesk beendet'

'6) Neu installieren, dann entfernen und Daten löschen (Antwort J):'
$r = Invoke-Setup $src @('-Quiet', '-NoStart') 'reinstall'
$global:Answers.Clear(); $global:Answers.Enqueue('j')
$r = Invoke-Setup $installed @('-Uninstall') 'uninstall delete data'
Set-Location -LiteralPath $home0
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert (-not (Test-Path -LiteralPath $dataDir)) 'Datenordner gelöscht'
Assert ($r.Text -match 'Highlight-Videos') 'Hinweis: Videos bleiben erhalten'

'7) Fehlerfall: fdanalyze.exe fehlt im ZIP-Ordner (-Pause):'
Remove-Item -LiteralPath (Join-Path $src 'fdanalyze.exe')
$global:Answers.Clear()
$r = Invoke-Setup $src @('-Pause') 'error pause'
Assert ($r.Code -eq 1) "Exit-Code 1 ($($r.Code))"
Assert ($r.Text -match 'Fehler: fdanalyze.exe fehlt') 'Fehlermeldung'
Assert ($global:Prompts.Count -eq 1 -and $global:Prompts[0] -like '*Enter*') "wartet auf Enter: $($global:Prompts -join ' | ')"

''
Set-Location -LiteralPath $home0
if ($script:failures -eq 0) {
    'ALLE PRÜFUNGEN OK'
    Remove-Item -LiteralPath $root -Recurse -Force
} else {
    "$($script:failures) FEHLER – Ausgaben: $script:logFile"
}
exit $script:failures
