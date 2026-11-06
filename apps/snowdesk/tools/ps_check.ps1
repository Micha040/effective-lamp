# Prüft die geänderten Teile von Setup.ps1 unter StrictMode 2 (pwsh unter Linux, Windows-Befehle/Registry gemockt).
# Läuft eine Testkopie von Setup.ps1, in der nur Get-OsVersion auf $global:FakeOsVersion umgelenkt ist.
param([Parameter(Mandatory = $true)][string]$SetupPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$root = Join-Path $PSScriptRoot 'pscheck-run'
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
$null = New-Item -ItemType Directory -Path $root
$env:LOCALAPPDATA = Join-Path $root 'LocalAppData'
$env:SystemRoot = Join-Path $root 'Windows'
$env:TEMP = Join-Path $root 'Temp'
foreach ($d in $env:LOCALAPPDATA, $env:SystemRoot, $env:TEMP) { $null = New-Item -ItemType Directory -Path $d }
$src = Join-Path $root 'zip'
$null = New-Item -ItemType Directory -Path $src
'exe' | Set-Content -LiteralPath (Join-Path $src 'SnowDesk.exe')
'dll' | Set-Content -LiteralPath (Join-Path $src 'SnowDeskMenu.dll')
'readme' | Set-Content -LiteralPath (Join-Path $src 'README.md')
$text = [IO.File]::ReadAllText($SetupPath)
$expectedVersion = ([regex]::Match($text, '\$AppVersion\s*=\s*''([^'']+)''')).Groups[1].Value
$origLine = 'function Get-OsVersion { return [Environment]::OSVersion.Version }'
if ([regex]::Matches($text, [regex]::Escape($origLine)).Count -ne 1) { throw 'Get-OsVersion-Zeile nicht gefunden' }
$text = $text.Replace($origLine, 'function Get-OsVersion { return $global:FakeOsVersion }')
[IO.File]::WriteAllText((Join-Path $src 'Setup.ps1'), $text, (New-Object System.Text.UTF8Encoding $true))

$installed = Join-Path $env:LOCALAPPDATA 'Programs\SnowDesk'
$dataDir = Join-Path $env:LOCALAPPDATA 'SnowDesk'
$psExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$ukey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SnowDesk'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$classicKey = 'HKCU:\Software\Classes\DesktopBackground\Shell\SnowDesk'
$regKeyApp = 'HKCU:\Software\SnowDesk'

$global:FakeOsVersion = [version]'10.0.19045.0'
$global:Reg = @{}
$global:Procs = New-Object System.Collections.ArrayList
$global:Calls = New-Object System.Collections.ArrayList
$global:Prompts = New-Object System.Collections.ArrayList
$global:Answers = New-Object System.Collections.Queue

# ------------------------------------------------------------------ Mocks
function global:Test-RegPath([string]$P) { return ($P -like 'HKCU:*' -or $P -like 'HKLM:*') }
function global:New-FakeProcess([int]$ExitCode = 0, [string]$Name = 'SnowDesk') {
    $o = [pscustomobject]@{ Id = (Get-Random -Minimum 1000 -Maximum 9999); ProcessName = $Name; ExitCode = $ExitCode; Handle = [IntPtr]1 }
    $o | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { param($ms) return $true }
    $o | Add-Member -MemberType ScriptMethod -Name Kill -Value { $global:Procs.Remove($this) }
    return $o
}
function global:New-Item {
    [CmdletBinding()]
    param([Parameter(Position = 0)][string[]]$Path, [string]$ItemType, [switch]$Force, [object]$Value)
    $regs = @($Path | Where-Object { Test-RegPath $_ })
    if ($regs.Count -eq 0) { return Microsoft.PowerShell.Management\New-Item @PSBoundParameters }
    foreach ($p in $regs) {
        if ($global:Reg.ContainsKey($p)) {
            # wie der echte Registry-Provider: ein vorhandener Schlüssel wird mit -Force samt Werten und
            # Unterschlüsseln gelöscht und leer neu angelegt (ohne -Force: Fehler)
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
function global:Get-ItemProperty {
    [CmdletBinding()]
    param([string]$Path, [string]$Name)
    if (-not (Test-RegPath $Path)) { return Microsoft.PowerShell.Management\Get-ItemProperty @PSBoundParameters }
    if ($global:Reg.ContainsKey($Path) -and $global:Reg[$Path].ContainsKey($Name)) { return [pscustomobject]@{ $Name = $global:Reg[$Path][$Name] } }
    Write-Error "Wert fehlt: $Path $Name"
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
    $argText = ($ArgumentList -join ' ')
    [void]$global:Calls.Add(([IO.Path]::GetFileName($FilePath) + ' ' + $argText))
    if ($FilePath -like '*SnowDesk.exe') {
        if ($argText -like '--autostart*') { [void]$global:Procs.Add((New-FakeProcess)) }
        elseif ($argText -like '--cmd exit*') { $global:Procs.Clear() }
        elseif ($argText -match '^--pack-menu "(.+)"$') { Set-Content -LiteralPath (Join-Path $Matches[1] 'SnowDeskMenu.msix') -Value 'msix' }
    }
    if ($PassThru) { return (New-FakeProcess -Name 'helper') }
}
function global:Get-AppxPackage { [CmdletBinding()] param([string]$Name, [switch]$AllUsers) }
function global:Remove-AppxPackage { [CmdletBinding()] param([Parameter(ValueFromPipeline = $true)]$Package, [switch]$AllUsers) process { } }
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
        $o | Add-Member -MemberType ScriptMethod -Name MinimizeAll -Value { [void]$global:Calls.Add('MinimizeAll') }
        $o | Add-Member -MemberType ScriptMethod -Name UndoMinimizeALL -Value { [void]$global:Calls.Add('UndoMinimizeAll') }
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

# Andere Autostarts und vorhandene Einstellungen müssen jede Installation überstehen
foreach ($k in 'HKCU:', 'HKCU:\Software', 'HKCU:\Software\Microsoft', 'HKCU:\Software\Microsoft\Windows', 'HKCU:\Software\Microsoft\Windows\CurrentVersion', $runKey) {
    if (-not $global:Reg.ContainsKey($k)) { $global:Reg[$k] = @{} }
}
$global:Reg[$runKey]['Steam'] = '"C:\Program Files (x86)\Steam\steam.exe" -silent'
$global:Reg[$regKeyApp] = @{ RealWeather = 1; Place = 'Hamburg' }

"1) Installation -Quiet aus dem ZIP-Ordner (Windows 10, klassisches Menü):"
$r = Invoke-Setup $src @('-Quiet') 'install quiet'
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert ($r.Text -notmatch 'Fehler:') 'keine Fehlermeldung'
foreach ($n in 'SnowDesk.exe', 'SnowDeskMenu.dll', 'README.md', 'Setup.ps1') { Assert (Test-Path -LiteralPath (Join-Path $installed $n)) "$n im Programmordner" }
Assert ($global:Reg.ContainsKey($ukey)) 'Eintrag unter Installierte Apps angelegt'
if ($global:Reg.ContainsKey($ukey)) {
    $u = $global:Reg[$ukey]
    $expected = '"' + $psExe + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $installed 'Setup.ps1') + '" -Uninstall -Pause'
    Assert ($u['DisplayName'] -eq 'SnowDesk' -and $u['DisplayVersion'] -eq $expectedVersion -and $u['Publisher'] -eq 'SnowDesk') 'Name/Version/Herausgeber'
    Assert ($u['UninstallString'] -eq $expected) "UninstallString = $($u['UninstallString'])"
    Assert ($u['DisplayIcon'] -eq ('"' + (Join-Path $installed 'SnowDesk.exe') + '",0')) 'DisplayIcon'
    Assert ($u['InstallLocation'] -eq $installed) 'InstallLocation'
    Assert ($u['NoModify'] -eq 1 -and $u['NoModify#type'] -eq 'DWord' -and $u['NoRepair'] -eq 1 -and $u['NoRepair#type'] -eq 'DWord') 'NoModify/NoRepair als DWord'
    Assert ($u['EstimatedSize'] -ge 4 -and $u['EstimatedSize#type'] -eq 'DWord') "EstimatedSize = $($u['EstimatedSize']) KB (4 Dateien)"
}
Assert ($global:Reg.ContainsKey($runKey) -and $global:Reg[$runKey]['SnowDesk'] -like '*--autostart') 'Autostart eingetragen'
Assert ($global:Reg[$runKey]['Steam'] -like '*steam.exe*') 'anderer Autostart (Steam) bleibt erhalten'
Assert ($global:Reg.ContainsKey($regKeyApp) -and $global:Reg[$regKeyApp]['Place'] -eq 'Hamburg') 'vorhandene Einstellungen (Ort) bleiben erhalten'
Assert ($global:Reg.ContainsKey($classicKey)) 'klassisches Kontextmenü eingetragen'
Assert ($global:Prompts.Count -eq 0) 'keine Rückfrage bei -Quiet'
Assert (@($global:Procs).Count -eq 1) 'SnowDesk läuft'

"2) Reparatur: Setup.ps1 aus dem Programmordner gestartet (-Quiet):"
$r = Invoke-Setup $installed @('-Quiet') 'repair'
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert ($r.Text -match 'Reparatur') 'Reparatur-Weg (nichts kopiert)'
Assert (Test-Path -LiteralPath (Join-Path $installed 'SnowDesk.exe')) 'Programm noch da'
Assert ($global:Reg.ContainsKey($ukey)) 'Eintrag unter Installierte Apps noch da'
Assert (@($global:Procs).Count -eq 1) 'SnowDesk läuft wieder (genau einmal)'

"3) Installation unter Windows 11 (-Quiet, Paket-Registrierung schlägt fehl -> klassisches Menü):"
$global:FakeOsVersion = [version]'10.0.26200.0'
$r = Invoke-Setup $src @('-Quiet') 'install win11 quiet'
$global:FakeOsVersion = [version]'10.0.19045.0'
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert (@($global:Calls | Where-Object { $_ -like 'SnowDesk.exe --pack-menu*' }).Count -eq 1) 'Paket gebaut (--pack-menu)'
Assert (@($global:Calls | Where-Object { $_ -like 'powershell.exe*Add-AppxPackage*' }).Count -eq 1) 'Registrierung mit Adminrechten versucht'
Assert ($global:Reg.ContainsKey($classicKey)) 'Ersatz: klassisches Menü'

"4) Sichttest: Antwort S (schwarz) -> direkt 'vor den Symbolen':"
$global:Answers.Clear(); $global:Answers.Enqueue('S'); $global:Answers.Enqueue('J')
$r = Invoke-Setup $src @() 'install selftest S'
$renders = @($global:Calls | Where-Object { $_ -match '--cmd render:' } | ForEach-Object { ($_ -split 'render:')[1] })
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert (($renders -join ',') -eq '0,2') "Varianten: $($renders -join ',') (erwartet 0,2)"
Assert ($r.Text -match 'Variante: vor den Symbolen') 'Schnee sichtbar in Variante vor den Symbolen'

"5) Sichttest: dreimal N -> alle Varianten, dann Diagnose:"
$global:Answers.Clear(); foreach ($a in 'N', 'N', 'N') { $global:Answers.Enqueue($a) }
$r = Invoke-Setup $src @() 'install selftest NNN'
$renders = @($global:Calls | Where-Object { $_ -match '--cmd render:' } | ForEach-Object { ($_ -split 'render:')[1] })
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert (($renders -join ',') -eq '0,1,2') "Varianten: $($renders -join ',') (erwartet 0,1,2)"
Assert (@($global:Calls | Where-Object { $_ -like 'SnowDesk.exe --cmd diag*' }).Count -eq 1) 'Diagnose geschrieben'

"6) Deinstallation wie aus 'Installierte Apps' (installierte Setup.ps1 -Uninstall -Pause):"
$null = New-Item -ItemType Directory -Path $dataDir -Force
'x' | Set-Content -LiteralPath (Join-Path $dataDir 'settings.ini')
$global:Answers.Clear()
Set-Location -LiteralPath $installed   # wie ein Aufruf mit Arbeitsordner = Programmordner
$r = Invoke-Setup $installed @('-Uninstall', '-Pause') 'uninstall pause'
Set-Location -LiteralPath $home0
Assert ($r.Code -eq 0) "Exit-Code 0 ($($r.Code))"
Assert ($r.Text -notmatch 'Fehler:') 'keine Fehlermeldung'
Assert ($r.Location -eq $env:TEMP) "Arbeitsordner auf TEMP gewechselt ($($r.Location))"
Assert (-not (Test-Path -LiteralPath $installed)) 'Programmordner gelöscht'
Assert (-not $global:Reg.ContainsKey($ukey)) 'Eintrag unter Installierte Apps entfernt'
Assert (-not $global:Reg.ContainsKey($classicKey)) 'klassisches Menü entfernt'
Assert (-not ($global:Reg.ContainsKey($runKey) -and $global:Reg[$runKey].ContainsKey('SnowDesk'))) 'Autostart entfernt'
Assert (Test-Path -LiteralPath $dataDir) 'Einstellungen bleiben (Standardantwort N)'
Assert ($global:Prompts.Count -eq 2 -and $global:Prompts[1] -like '*Enter*') "Rückfragen: $($global:Prompts -join ' | ')"
Assert (@($global:Procs).Count -eq 0) 'SnowDesk beendet'

"7) Fehlerfall mit -Pause (SnowDesk.exe fehlt im ZIP-Ordner):"
Remove-Item -LiteralPath (Join-Path $src 'SnowDesk.exe')
$global:Answers.Clear()
$r = Invoke-Setup $src @('-Pause', '-SkipMenu') 'error pause'
Assert ($r.Code -eq 1) "Exit-Code 1 ($($r.Code))"
Assert ($r.Text -match 'Fehler: SnowDesk.exe fehlt') 'Fehlermeldung'
Assert ($global:Prompts.Count -eq 1 -and $global:Prompts[0] -like '*Enter*') "wartet auf Enter: $($global:Prompts -join ' | ')"
$r = Invoke-Setup $src @('-Pause', '-Quiet') 'error pause quiet'
Assert ($r.Code -eq 1) "Exit-Code 1 ($($r.Code))"
Assert ($global:Prompts.Count -eq 0) 'mit -Quiet keine Pause'

""
"Ausgaben: $script:logFile"
if ($script:failures -eq 0) { 'ALLE PRÜFUNGEN OK' } else { "$($script:failures) FEHLER" }
exit $script:failures
