<#
    TriggerSwitch – Setup
    ---------------------
    Installiert bzw. aktualisiert TriggerSwitch für den aktuell angemeldeten Windows-Benutzer:
      1. kompiliert TriggerSwitch.cs lokal zu TriggerSwitch.exe (C#-Compiler aus .NET Framework)
      2. legt %LOCALAPPDATA%\TriggerSwitch\config.json an (Hotkey, Overlay, Belegungen)
      3. richtet den Autostart ein (Aufgabenplanung: bei Anmeldung, unsichtbar)
    Keine Administratorrechte nötig. Start am einfachsten per Doppelklick auf Setup.cmd.

    Direktaufruf ohne Menü:
      Setup.ps1 -Action Install | Hotkey | Overlay | Log | Restart | Pause | Verbose | Uninstall
#>
[CmdletBinding()]
param(
    [ValidateSet('Menu', 'Install', 'Hotkey', 'Overlay', 'Log', 'Restart', 'Pause', 'Verbose', 'Uninstall')]
    [string]$Action = 'Menu'
)

$ErrorActionPreference = 'Stop'

$AppName       = 'TriggerSwitch'
$InstallDir    = Join-Path $env:LOCALAPPDATA $AppName
$ExePath       = Join-Path $InstallDir 'TriggerSwitch.exe'
$ConfigPath    = Join-Path $InstallDir 'config.json'
$StatePath     = Join-Path $InstallDir 'state.json'
$LogPath       = Join-Path $InstallDir 'TriggerSwitch.log'
$TaskName      = 'TriggerSwitch'
$RunKey        = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$SourcePath    = Join-Path $PSScriptRoot 'TriggerSwitch.cs'
$DefaultHotkey = 'Ctrl+Alt+F11'
$Positions     = [ordered]@{
    'bottomRight'  = 'unten rechts'
    'bottomLeft'   = 'unten links'
    'topRight'     = 'oben rechts'
    'topLeft'      = 'oben links'
    'bottomCenter' = 'unten Mitte'
    'topCenter'    = 'oben Mitte'
}

# ------------------------------------------------------------------------------ Ausgabe

function Write-Title([string]$Text) {
    Write-Host ''
    Write-Host ('=' * 64) -ForegroundColor DarkCyan
    Write-Host "  $Text" -ForegroundColor Cyan
    Write-Host ('=' * 64) -ForegroundColor DarkCyan
}
function Write-Step([string]$Text) { Write-Host "`n> $Text" -ForegroundColor White }
function Write-Ok([string]$Text)   { Write-Host "  [OK] $Text" -ForegroundColor Green }
function Write-Info([string]$Text) { Write-Host "  $Text" -ForegroundColor Gray }
function Write-Hint([string]$Text) { Write-Host "  [!] $Text" -ForegroundColor Yellow }
function Write-Bad([string]$Text)  { Write-Host "  [X] $Text" -ForegroundColor Red }

function Read-YesNo([string]$Question, [bool]$Default = $true) {
    $suffix = '[J/n]'
    if (-not $Default) { $suffix = '[j/N]' }
    $answer = (Read-Host "  $Question $suffix").Trim()
    if ($answer -eq '') { return $Default }
    return ($answer -match '^(j|ja|y|yes)$')
}

function Get-Timestamp {
    return (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
}

# ------------------------------------------------------------------------------ Konfiguration

function Add-MissingProperty($Object, [string]$Name, $Value) {
    if (-not $Object.PSObject.Properties[$Name]) {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function New-DefaultModes {
    # Entspricht deinen Synapse-Belegungen: Leertaste-Turbo bzw. Rechtsklick-Turbo, je alle 50 ms (20/s)
    return @(
        [pscustomobject][ordered]@{ name = 'Leertaste';   fn = 13; size = 4; data = '00 2C 00 32 00'; color = '#4ADE80' },
        [pscustomobject][ordered]@{ name = 'Rechtsklick'; fn = 14; size = 3; data = '02 00 32 00 00'; color = '#60A5FA' }
    )
}

function Get-Config([switch]$NoRepair) {
    $cfg = $null
    if (Test-Path -LiteralPath $ConfigPath) {
        try {
            $cfg = [System.IO.File]::ReadAllText($ConfigPath) | ConvertFrom-Json
        } catch {
            if ($NoRepair) { return $null }
            $backup = "$ConfigPath.bak"
            Copy-Item -LiteralPath $ConfigPath -Destination $backup -Force
            Write-Bad "Die config.json ist fehlerhaft: $($_.Exception.Message)"
            Write-Info "Sicherungskopie: $backup"
            if (-not (Read-YesNo 'config.json mit Standardwerten neu anlegen?' $false)) {
                throw 'Abgebrochen – bitte die config.json korrigieren (Hinweise in der README) oder löschen.'
            }
            $cfg = $null
        }
    }
    if ($null -eq $cfg) { $cfg = New-Object psobject }

    Add-MissingProperty $cfg 'hotkey' $DefaultHotkey
    if (-not $cfg.PSObject.Properties['overlay'] -or $null -eq $cfg.overlay) {
        $cfg | Add-Member -NotePropertyName 'overlay' -NotePropertyValue (New-Object psobject) -Force
    }
    Add-MissingProperty $cfg.overlay 'enabled' $true
    Add-MissingProperty $cfg.overlay 'seconds' 3
    Add-MissingProperty $cfg.overlay 'position' 'bottomRight'
    Add-MissingProperty $cfg 'modes' (New-DefaultModes)
    Add-MissingProperty $cfg 'watchdogMs' 2500
    Add-MissingProperty $cfg 'verboseLog' $false
    return $cfg
}

function Save-Config($Config) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $json = $Config | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($ConfigPath, $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Get-ModeNames($Config) {
    $names = @()
    foreach ($m in @($Config.modes)) {
        if ($null -ne $m -and $m.PSObject.Properties['name']) { $names += "$($m.name)" }
    }
    return $names
}

function Get-CurrentModeName {
    if (-not (Test-Path -LiteralPath $StatePath)) { return $null }
    try {
        $state = [System.IO.File]::ReadAllText($StatePath) | ConvertFrom-Json
        if ($state.PSObject.Properties['modeName'] -and "$($state.modeName)" -ne '') { return "$($state.modeName)" }
    } catch { <# Zustandsdatei unlesbar – egal, nur Anzeige #> }
    return $null
}

# ------------------------------------------------------------------------------ Hotkey

# Virtueller Tastencode -> Name, den TriggerSwitch.exe versteht (null = Zusatztaste allein)
function Get-KeyName([int]$Vk) {
    if ($Vk -ge 0x70 -and $Vk -le 0x87) { return 'F' + ($Vk - 0x6F) }
    if (($Vk -ge 0x41 -and $Vk -le 0x5A) -or ($Vk -ge 0x30 -and $Vk -le 0x39)) { return [string][char]$Vk }
    if ($Vk -ge 0x60 -and $Vk -le 0x69) { return 'NumPad' + ($Vk - 0x60) }
    if (($Vk -ge 0x10 -and $Vk -le 0x12) -or ($Vk -ge 0xA0 -and $Vk -le 0xA5) -or $Vk -eq 0x5B -or $Vk -eq 0x5C -or $Vk -eq 0x14 -or $Vk -lt 0x08) { return $null }
    switch ($Vk) {
        0x13 { return 'Pause' }
        0x91 { return 'ScrollLock' }
        0x2D { return 'Insert' }
        0x2E { return 'Delete' }
        0x24 { return 'Home' }
        0x23 { return 'End' }
        0x21 { return 'PageUp' }
        0x22 { return 'PageDown' }
        0x26 { return 'Up' }
        0x28 { return 'Down' }
        0x25 { return 'Left' }
        0x27 { return 'Right' }
        0x20 { return 'Space' }
        0x09 { return 'Tab' }
        0x0D { return 'Enter' }
        0x1B { return 'Esc' }
        0x08 { return 'Backspace' }
        0x2C { return 'PrintScreen' }
        0x6A { return 'Multiply' }
        0x6B { return 'Add' }
        0x6D { return 'Subtract' }
        0x6E { return 'Decimal' }
        0x6F { return 'Divide' }
        0x5D { return 'Apps' }
    }
    if ($Vk -gt 0xFE) { return $null }
    return ('0x{0:X2}' -f $Vk)
}

function ConvertFrom-KeyInfo([System.ConsoleKeyInfo]$KeyInfo) {
    $name = Get-KeyName ([int]$KeyInfo.Key)
    if ($null -eq $name) { return $null }
    $parts = @()
    if ($KeyInfo.Modifiers -band [ConsoleModifiers]::Control) { $parts += 'Ctrl' }
    if ($KeyInfo.Modifiers -band [ConsoleModifiers]::Alt) { $parts += 'Alt' }
    if ($KeyInfo.Modifiers -band [ConsoleModifiers]::Shift) { $parts += 'Shift' }
    $parts += $name
    return ($parts -join '+')
}

# Eingetippter Text (auch deutsch: "Strg+Umschalt+F9", "Rollen") -> einheitliche Schreibweise, sonst $null
function ConvertTo-CanonicalHotkey([string]$Text) {
    $tokens = @("$Text" -split '\+' | ForEach-Object { $_.Trim() })
    if ($tokens.Count -eq 0 -or @($tokens | Where-Object { $_ -eq '' }).Count -gt 0) { return $null }
    $ctrl = $false; $alt = $false; $shift = $false; $win = $false
    for ($i = 0; $i -lt $tokens.Count - 1; $i++) {
        switch -regex ($tokens[$i]) {
            '^(ctrl|strg|control)$' { $ctrl = $true; continue }
            '^alt$'                 { $alt = $true; continue }
            '^(shift|umschalt)$'    { $shift = $true; continue }
            '^(win|windows)$'       { $win = $true; continue }
            default                 { return $null }
        }
    }
    $k = ($tokens[-1] -replace '\s', '').ToLowerInvariant()
    $key = $null
    if ($k -match '^f([1-9]|1[0-9]|2[0-4])$') { $key = 'F' + $Matches[1] }
    elseif ($k -match '^[a-z0-9]$') { $key = $k.ToUpperInvariant() }
    elseif ($k -match '^(numpad|num)([0-9])$') { $key = 'NumPad' + $Matches[2] }
    elseif ($k -match '^0x[0-9a-f]{2}$') { $key = '0x' + $k.Substring(2).ToUpperInvariant() }
    else {
        $names = @{
            'pause' = 'Pause'; 'rollen' = 'ScrollLock'; 'scroll' = 'ScrollLock'; 'scrolllock' = 'ScrollLock'
            'einfg' = 'Insert'; 'insert' = 'Insert'; 'entf' = 'Delete'; 'delete' = 'Delete'
            'pos1' = 'Home'; 'home' = 'Home'; 'ende' = 'End'; 'end' = 'End'
            'bildauf' = 'PageUp'; 'pageup' = 'PageUp'; 'bildab' = 'PageDown'; 'pagedown' = 'PageDown'
            'leertaste' = 'Space'; 'space' = 'Space'
        }
        if ($names.ContainsKey($k)) { $key = $names[$k] }
    }
    if ($null -eq $key) { return $null }
    $parts = @()
    if ($ctrl) { $parts += 'Ctrl' }
    if ($alt) { $parts += 'Alt' }
    if ($shift) { $parts += 'Shift' }
    if ($win) { $parts += 'Win' }
    $parts += $key
    return ($parts -join '+')
}

# Anzeige auf Deutsch (wie im Programm-Log)
function Format-Hotkey([string]$Text) {
    $map = @{
        'Ctrl' = 'Strg'; 'Shift' = 'Umschalt'; 'ScrollLock' = 'Rollen'; 'Insert' = 'Einfg'; 'Delete' = 'Entf'
        'Home' = 'Pos1'; 'End' = 'Ende'; 'PageUp' = 'Bild auf'; 'PageDown' = 'Bild ab'; 'Space' = 'Leertaste'
        'Up' = 'Pfeil hoch'; 'Down' = 'Pfeil runter'; 'Left' = 'Pfeil links'; 'Right' = 'Pfeil rechts'
        'PrintScreen' = 'Druck'; 'Backspace' = 'Rücktaste'; 'Apps' = 'Menütaste'
    }
    $parts = foreach ($p in ("$Text" -split '\+')) {
        if ($map.ContainsKey($p)) { $map[$p] }
        elseif ($p -match '^NumPad(\d)$') { 'Num ' + $Matches[1] }
        else { $p }
    }
    return ($parts -join '+')
}

# @{ Block = '...' } = nicht erlaubt, @{ Warn = '...' } = nachfragen, @{} = in Ordnung
function Get-HotkeyIssue([string]$Text) {
    $parts = @("$Text" -split '\+')
    $key = $parts[-1]
    $mods = @()
    if ($parts.Count -gt 1) { $mods = @($parts[0..($parts.Count - 2)]) }
    $ctrl = $mods -contains 'Ctrl'; $alt = $mods -contains 'Alt'; $shift = $mods -contains 'Shift'; $win = $mods -contains 'Win'
    $isF = $key -match '^F([1-9]|1[0-9]|2[0-4])$'
    $isChar = $key -match '^([A-Z0-9]|0x[0-9A-F]{2})$'

    if ($mods.Count -eq 0) {
        if ($key -eq 'Pause' -or $key -eq 'ScrollLock') { return @{} }
        if ($isF) {
            if ([int]$key.Substring(1) -ge 13) { return @{} }
            return @{ Warn = ('{0} ohne Zusatztaste fehlt dann in allen anderen Programmen (z. B. F5 im Browser).' -f $key) }
        }
        return @{ Block = 'Ohne Strg/Alt/Umschalt geht nur eine F-Taste, Pause oder Rollen – sonst fehlt dir die Taste überall.' }
    }
    if ($alt -and -not $ctrl -and -not $shift -and -not $win -and ($key -eq 'F4' -or $key -eq 'Tab' -or $key -eq 'Esc')) {
        return @{ Block = ('Alt+{0} gehört Windows – bitte etwas anderes.' -f $key) }
    }
    if ($shift -and -not $ctrl -and -not $alt -and -not $win -and ($isChar -or $key -eq 'Space')) {
        return @{ Block = 'Umschalt + Buchstabe/Ziffer brauchst du zum Schreiben – bitte mit Strg oder Alt.' }
    }
    if ($ctrl -and $alt -and -not $shift -and -not $win -and $isChar) {
        return @{ Warn = 'Strg+Alt+Buchstabe/Ziffer ist auf deutschen Tastaturen dasselbe wie AltGr (z. B. AltGr+Q = @, AltGr+E = €). Diese Zeichen wären dann weg.' }
    }
    if ($win) {
        return @{ Warn = 'Kombinationen mit der Windows-Taste sind oft schon von Windows belegt.' }
    }
    return @{}
}

function Read-KeyPress {
    return [Console]::ReadKey($true)
}

# Liefert den neuen Hotkey (einheitliche Schreibweise) oder $null = unverändert
function Read-Hotkey([string]$Current) {
    Write-Info ('Aktuell: {0}' -f (Format-Hotkey $Current))
    Write-Info 'Drück jetzt die neue Kombination (z. B. Strg+Alt+F11 oder Pause).'
    Write-Info 'Enter = so lassen  |  Esc = eintippen statt drücken (nötig z. B. für Rollen)'
    while ($true) {
        $text = $null
        $typed = $false
        try {
            $k = Read-KeyPress
            if ($k.Key -eq [ConsoleKey]::Enter -and $k.Modifiers -eq 0) { return $null }
            if ($k.Key -eq [ConsoleKey]::Escape -and $k.Modifiers -eq 0) { $typed = $true }
            else { $text = ConvertFrom-KeyInfo $k }
        } catch {
            $typed = $true   # keine echte Konsole (z. B. ISE) -> eintippen
        }
        if ($typed) {
            $answer = (Read-Host '  Kombination eintippen (z. B. Strg+Alt+F11, Rollen; leer = so lassen)').Trim()
            if ($answer -eq '') { return $null }
            $text = ConvertTo-CanonicalHotkey $answer
            if ($null -eq $text) {
                Write-Hint 'Das verstehe ich nicht. Beispiele: Strg+Alt+F11, Strg+Umschalt+F9, Pause, Rollen'
                continue
            }
        }
        if ($null -eq $text) { continue }
        Write-Info ('Erkannt: {0}' -f (Format-Hotkey $text))
        $issue = Get-HotkeyIssue $text
        if ($issue.Block) {
            Write-Hint $issue.Block
            Write-Info 'Nochmal drücken (Enter = so lassen):'
            continue
        }
        if ($issue.Warn) {
            Write-Hint $issue.Warn
            if (-not (Read-YesNo 'Trotzdem nehmen?' $false)) {
                Write-Info 'Nochmal drücken (Enter = so lassen):'
                continue
            }
        }
        return $text
    }
}

# ------------------------------------------------------------------------------ Prozess & Autostart

function Test-Installed { return (Test-Path -LiteralPath $ExePath) }

function Get-RunningProcess {
    return @(Get-Process -Name $AppName -ErrorAction SilentlyContinue |
        Where-Object { $_.SessionId -eq (Get-Process -Id $PID).SessionId })
}

function Stop-App {
    $running = Get-RunningProcess
    if ($running.Count -eq 0) { return }
    Write-Info 'Beende laufendes TriggerSwitch …'
    if (Test-Path -LiteralPath $ExePath) {
        try { Start-Process -FilePath $ExePath -ArgumentList '--stop' -Wait -WindowStyle Hidden } catch { <# egal – unten wird notfalls hart beendet #> }
    }
    $running | Wait-Process -Timeout 8 -ErrorAction SilentlyContinue
    try { Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue } catch { <# Aufgabe existiert evtl. noch nicht #> }
    Get-RunningProcess | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
}

function Start-App {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($task) {
        Start-ScheduledTask -TaskName $TaskName
    } else {
        Start-Process -FilePath $ExePath -WorkingDirectory $InstallDir
    }
}

function Register-Autostart {
    $userId = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    try {
        $action    = New-ScheduledTaskAction -Execute $ExePath -WorkingDirectory $InstallDir
        $trigger   = New-ScheduledTaskTrigger -AtLogOn -User $userId
        $principal = New-ScheduledTaskPrincipal -UserId $userId -LogonType Interactive -RunLevel Limited
        $settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                        -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 99 -RestartInterval (New-TimeSpan -Minutes 1) `
                        -MultipleInstances IgnoreNew -Priority 6
        Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings `
            -Description 'Schaltet per Hotkey die Trigger-Belegung der Razer Basilisk V3 Pro um.' -Force | Out-Null
        Remove-ItemProperty -Path $RunKey -Name $AppName -ErrorAction SilentlyContinue
        Write-Ok ('Autostart eingerichtet (Aufgabenplanung: „{0}“, bei jeder Anmeldung).' -f $TaskName)
    } catch {
        # Fallback ohne Aufgabenplanung: Autostart-Eintrag des Benutzers (Einstellungen > Apps > Autostart)
        Write-Hint "Aufgabenplanung nicht verfügbar ($($_.Exception.Message)) – nutze stattdessen den Autostart-Eintrag."
        New-ItemProperty -Path $RunKey -Name $AppName -Value "`"$ExePath`"" -PropertyType String -Force | Out-Null
        Write-Ok 'Autostart eingerichtet (Einstellungen > Apps > Autostart).'
    }
}

function Unregister-Autostart {
    if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    }
    Remove-ItemProperty -Path $RunKey -Name $AppName -ErrorAction SilentlyContinue
}

function Build-App {
    if (-not (Test-Path -LiteralPath $SourcePath)) {
        throw "TriggerSwitch.cs nicht gefunden. Die Datei muss im selben Ordner wie Setup.ps1 liegen ($SourcePath)."
    }
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $newExe = Join-Path $InstallDir 'TriggerSwitch.new.exe'
    Remove-Item -LiteralPath $newExe -Force -ErrorAction SilentlyContinue

    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $references = @(
        [System.Windows.Forms.NativeWindow].Assembly.Location,   # System.Windows.Forms
        [System.Drawing.Bitmap].Assembly.Location                 # System.Drawing
    ) | Select-Object -Unique

    try {
        Add-Type -Path $SourcePath -ReferencedAssemblies $references -OutputAssembly $newExe `
                 -OutputType WindowsApplication -IgnoreWarnings
    } catch {
        throw "Kompilieren fehlgeschlagen: $($_.Exception.Message)"
    }
    if (-not (Test-Path -LiteralPath $newExe)) { throw 'Kompilieren fehlgeschlagen (keine .exe erzeugt).' }

    Stop-App
    Move-Item -LiteralPath $newExe -Destination $ExePath -Force
    Copy-Item -LiteralPath $SourcePath -Destination (Join-Path $InstallDir 'TriggerSwitch.cs') -Force
}

function Show-NewLogLines([string]$Since, [int]$Seconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    $shown = 0
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-Path -LiteralPath $LogPath)) { continue }
        $lines = @(Get-Content -LiteralPath $LogPath -Encoding UTF8 -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -ge 23 -and $_.Substring(0, 23) -ge $Since -and $_ -notmatch '  DEBUG ' })
        for ($i = $shown; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            $color = 'Gray'
            if ($line -match '  WARN ') { $color = 'Yellow' }
            elseif ($line -match '  FEHLER ') { $color = 'Red' }
            Write-Host "    $($line.Substring(11))" -ForegroundColor $color
        }
        $shown = $lines.Count
    }
}

# ------------------------------------------------------------------------------ Prüfungen

function Test-CodeIntegrity {
    # Selbst kompilierte Programme brauchen den normalen PowerShell-Sprachmodus
    # und werden von Smart App Control (Windows 11) blockiert.
    $mode = "$($ExecutionContext.SessionState.LanguageMode)"
    if ($mode -ne 'FullLanguage') {
        throw ('PowerShell läuft im eingeschränkten Modus ({0}) – vermutlich wegen Smart App Control oder einer Firmenrichtlinie. So kann das Setup nicht kompilieren.' -f $mode)
    }
    $sac = $null
    try {
        $sac = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' -Name 'VerifiedAndReputablePolicyState' -ErrorAction Stop).VerifiedAndReputablePolicyState
    } catch { <# Wert fehlt = Smart App Control aus #> }
    if ($sac -eq 1) {
        Write-Bad 'Smart App Control ist eingeschaltet.'
        Write-Info 'Es blockiert selbst kompilierte (unsignierte) Programme – TriggerSwitch würde nicht starten.'
        Write-Info 'Einstellung: Windows-Sicherheit → App- und Browsersteuerung → Smart App Control.'
        if (-not (Read-YesNo 'Trotzdem fortfahren?' $false)) { throw 'Abgebrochen (Smart App Control).' }
    } elseif ($sac -eq 2) {
        Write-Hint 'Smart App Control ist im Auswertungsmodus. Schaltet Windows es später ein, wird TriggerSwitch blockiert (siehe README).'
    } else {
        Write-Ok 'Keine Blockade durch Smart App Control.'
    }
}

function Find-Basilisk {
    $labels = @{
        '00AA' = 'per Kabel'; '00AB' = 'per 2,4-GHz-Dongle'
        '00CC' = '35K, per Kabel'; '00CD' = '35K, per Dongle'
        '00D6' = '35K Phantom Green, per Kabel'; '00D7' = '35K Phantom Green, per Dongle'
    }
    $found = @()
    try {
        $devices = @(Get-CimInstance -ClassName Win32_PnPEntity -Filter "PNPDeviceID LIKE 'USB\\VID_1532%'" -ErrorAction Stop)
    } catch {
        return $null
    }
    foreach ($d in $devices) {
        if ("$($d.PNPDeviceID)" -match 'PID_([0-9A-F]{4})') {
            $productId = $Matches[1].ToUpperInvariant()
            if ($labels.ContainsKey($productId) -and $found -notcontains $labels[$productId]) { $found += $labels[$productId] }
        }
    }
    return $found
}

# ------------------------------------------------------------------------------ Aktionen

function Invoke-Install {
    Write-Title 'TriggerSwitch installieren / aktualisieren'

    Write-Step 'Voraussetzungen'
    if ($PSVersionTable.PSVersion.Major -lt 5) { throw 'Windows PowerShell 5.1 wird benötigt.' }
    if ([Environment]::OSVersion.Version.Major -lt 10) { throw 'Windows 10 oder 11 wird benötigt.' }
    Write-Ok "Windows $([Environment]::OSVersion.Version), PowerShell $($PSVersionTable.PSVersion)"
    Test-CodeIntegrity
    $mouse = Find-Basilisk
    if ($null -eq $mouse) { Write-Info 'Gerätesuche nicht möglich – egal, TriggerSwitch sucht die Maus selbst.' }
    elseif ($mouse.Count -gt 0) { Write-Ok ('Basilisk V3 Pro gefunden ({0}).' -f ($mouse -join ', ')) }
    else { Write-Hint 'Keine Basilisk V3 Pro gefunden – Dongle eingesteckt? Installieren geht trotzdem, TriggerSwitch wartet auf die Maus.' }
    if (@(Get-Process -Name 'RazerAppEngine', 'Razer Synapse 3', 'Razer Synapse' -ErrorAction SilentlyContinue).Count -gt 0) {
        Write-Ok 'Razer Synapse läuft (passt – das Synapse-Profil bleibt unverändert).'
    }

    Write-Step 'Einstellungen'
    $isNew = -not (Test-Path -LiteralPath $ConfigPath)
    $cfg = Get-Config
    if ($isNew) {
        Write-Info ('Hotkey zum Umschalten: {0}' -f (Format-Hotkey "$($cfg.hotkey)"))
        if (-not (Read-YesNo 'Passt der Hotkey?')) {
            Stop-App
            $new = Read-Hotkey "$($cfg.hotkey)"
            if ($null -ne $new) { $cfg.hotkey = $new }
        }
    }
    Save-Config $cfg
    Write-Ok ('Hotkey: {0}  |  Modi: {1}' -f (Format-Hotkey "$($cfg.hotkey)"), ((Get-ModeNames $cfg) -join ' / '))
    Write-Info "config.json: $ConfigPath"

    Write-Step 'Programm kompilieren (auf deinem PC, aus TriggerSwitch.cs)'
    Build-App
    Write-Ok "Erstellt: $ExePath"

    Write-Step 'Autostart'
    Register-Autostart

    Write-Step 'Starten'
    $since = Get-Timestamp
    Start-App
    Show-NewLogLines $since 6
    if ((Get-RunningProcess).Count -gt 0) {
        Write-Ok 'TriggerSwitch läuft im Hintergrund und startet ab jetzt automatisch mit Windows.'
    } else {
        Write-Bad 'TriggerSwitch läuft nicht – bitte das Log prüfen (Menüpunkt „Log anzeigen“).'
    }
    Write-Host ''
    Write-Info ('Test: {0} drücken – in der Ecke erscheint das Overlay, der Trigger wechselt.' -f (Format-Hotkey "$($cfg.hotkey)"))
    Write-Info 'Overlay vorab ansehen oder verschieben: Menüpunkt [3].'
}

function Invoke-HotkeySetup {
    Write-Title 'Hotkey ändern'
    $cfg = Get-Config
    if ((Get-RunningProcess).Count -gt 0) {
        Write-Info 'TriggerSwitch wird kurz angehalten, damit dein Tastendruck hier ankommt.'
        Stop-App
    }
    $old = "$($cfg.hotkey)"
    $new = Read-Hotkey $old
    if ($null -ne $new -and $new -ne $old) {
        $cfg.hotkey = $new
        Save-Config $cfg
        Write-Ok ('Neuer Hotkey: {0}' -f (Format-Hotkey $new))
    } else {
        Write-Info 'Hotkey unverändert.'
    }
    if (Test-Installed) {
        $since = Get-Timestamp
        Start-App
        Show-NewLogLines $since 4
    }
}

function Invoke-OverlayPreview {
    if (-not (Test-Installed)) { Write-Hint 'Erst installieren (Menüpunkt [1]).'; return }
    Write-Info 'Schau in die Ecke dieses Bildschirms – die Overlays kommen nacheinander (ca. 15 Sekunden) …'
    $p = Start-Process -FilePath $ExePath -ArgumentList '--overlay-test' -WorkingDirectory $InstallDir -PassThru
    if (-not $p.WaitForExit(60000)) { try { $p.Kill() } catch { <# schon beendet #> } }
    Write-Ok 'Vorschau fertig.'
}

function Invoke-OverlaySetup {
    Write-Title 'Overlay'
    $cfg = Get-Config
    $o = $cfg.overlay
    $posName = $Positions["$($o.position)"]
    if (-not $posName) { $posName = "$($o.position)" }
    $state = 'aus'
    if ($o.enabled) { $state = 'an' }
    Write-Info ('Aktuell: {0}, {1}, {2} Sekunden' -f $state, $posName, $o.seconds)
    if ($o.enabled -and (Test-Installed) -and (Read-YesNo 'Vorschau zeigen?')) { Invoke-OverlayPreview }

    $changed = $false
    $enabled = Read-YesNo 'Overlay beim Umschalten anzeigen?' ([bool]$o.enabled)
    if ($enabled -ne [bool]$o.enabled) { $o.enabled = $enabled; $changed = $true }
    if ($enabled) {
        $keys = @($Positions.Keys)
        Write-Info 'Position:'
        for ($i = 0; $i -lt $keys.Count; $i++) {
            $mark = ''
            if ($keys[$i] -eq "$($o.position)") { $mark = '  (aktuell)' }
            Write-Info ('  [{0}] {1}{2}' -f ($i + 1), $Positions[$keys[$i]], $mark)
        }
        $answer = (Read-Host '  Auswahl (Enter = so lassen)').Trim()
        if ($answer -match '^[1-6]$') {
            $pick = $keys[[int]$answer - 1]
            if ($pick -ne "$($o.position)") { $o.position = $pick; $changed = $true }
        }
        $answer = (Read-Host ('  Anzeigedauer in Sekunden, 1–10 (Enter = {0})' -f $o.seconds)).Trim().Replace(',', '.')
        $seconds = 0.0
        if ($answer -ne '' -and [double]::TryParse($answer, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$seconds)) {
            $seconds = [Math]::Max(1.0, [Math]::Min(10.0, $seconds))   # 1.0/10.0: sonst nimmt PowerShell die int-Variante (2,5 -> 2)
            if ($seconds -ne [double]$o.seconds) { $o.seconds = $seconds; $changed = $true }
        } elseif ($answer -ne '') {
            Write-Hint 'Keine Zahl – Dauer bleibt.'
        }
    }
    if (-not $changed) { Write-Info 'Nichts geändert.'; return }
    Save-Config $cfg
    Write-Ok 'Gespeichert.'
    if (Test-Installed) {
        Invoke-Restart
        if ($enabled -and (Read-YesNo 'Neue Einstellung als Vorschau ansehen?')) { Invoke-OverlayPreview }
    }
}

function Invoke-ShowLog {
    if (-not (Test-Path -LiteralPath $LogPath)) { Write-Hint 'Noch keine Log-Datei vorhanden.'; return }
    # Eigenes Fenster: Schließen beendet nur die Anzeige, nicht das Setup.
    $path = $LogPath.Replace("'", "''")
    $command = "`$Host.UI.RawUI.WindowTitle = 'TriggerSwitch – Log (zum Beenden Fenster schließen)'; " +
               "Get-Content -LiteralPath '$path' -Encoding UTF8 -Tail 40 -Wait"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    Start-Process -FilePath 'powershell.exe' -ArgumentList "-NoProfile -NoExit -EncodedCommand $encoded"
    Write-Ok 'Log in einem eigenen Fenster geöffnet (zum Beenden einfach schließen).'
}

function Invoke-Restart {
    if (-not (Test-Installed)) { Write-Hint 'TriggerSwitch ist noch nicht installiert.'; return }
    Stop-App
    $since = Get-Timestamp
    Start-App
    Show-NewLogLines $since 5
    if ((Get-RunningProcess).Count -gt 0) { Write-Ok 'Läuft.' } else { Write-Bad 'Start fehlgeschlagen – siehe Log.' }
}

# Beendet TriggerSwitch und gibt die Maus an Synapse zurück. Rückgabe: 0 ok, 2 Maus nicht erreichbar, 1 Fehler
function Invoke-Release {
    if (-not (Test-Installed)) { Write-Hint 'TriggerSwitch ist nicht installiert.'; return 0 }
    Write-Info 'Bitte die Maus kurz bewegen, damit sie wach ist …'
    $since = Get-Timestamp
    $p = Start-Process -FilePath $ExePath -ArgumentList '--release' -WorkingDirectory $InstallDir -WindowStyle Hidden -PassThru
    $null = $p.Handle   # sonst liefert Windows PowerShell später keinen ExitCode
    if (-not $p.WaitForExit(60000)) { try { $p.Kill() } catch { <# schon beendet #> } }
    $code = 1
    try { $code = $p.ExitCode } catch { <# kein Exit-Code #> }
    Show-NewLogLines $since 1
    switch ($code) {
        0 { Write-Ok 'Maus an Synapse zurückgegeben (Trigger wie in Synapse eingestellt).' }
        2 { Write-Hint 'Die Maus hat nicht geantwortet – bitte bewegen bzw. einschalten und nochmal versuchen.' }
        default { Write-Bad 'Zurückgeben hat nicht geklappt – Details stehen im Log.' }
    }
    return $code
}

function Invoke-Pause {
    Write-Title 'Pausieren (Maus an Synapse zurückgeben)'
    if (-not (Test-Installed)) { Write-Hint 'TriggerSwitch ist nicht installiert.'; return }
    $code = Invoke-Release
    if ($code -eq 0) {
        Write-Info 'TriggerSwitch ist jetzt aus. Es startet bei der nächsten Anmeldung wieder – sofort: Menüpunkt [5] Neu starten.'
    }
}

function Invoke-ToggleVerbose {
    $cfg = Get-Config
    $cfg.verboseLog = -not [bool]$cfg.verboseLog
    Save-Config $cfg
    if ($cfg.verboseLog) { Write-Ok 'Ausführliches Diagnose-Log ist AN (jede Prüfung wird protokolliert).' }
    else { Write-Ok 'Ausführliches Diagnose-Log ist AUS.' }
    if (Test-Installed) { Invoke-Restart }
}

function Invoke-Uninstall {
    Write-Title 'TriggerSwitch deinstallieren'
    if (-not (Read-YesNo 'Wirklich entfernen (Programm, Autostart, Einstellungen)?' $false)) { return }
    if (Test-Installed) {
        $code = Invoke-Release
        if ($code -ne 0 -and -not (Read-YesNo 'Die Maus wurde nicht zurückgegeben. Trotzdem deinstallieren?' $false)) { return }
    }
    Stop-App
    Unregister-Autostart
    if (Test-Path -LiteralPath $InstallDir) { Remove-Item -LiteralPath $InstallDir -Recurse -Force }
    Write-Ok 'Entfernt. Dein Synapse-Profil war nie verändert.'
}

function Show-Status {
    $state = 'nicht installiert'
    $color = 'Yellow'
    if (Test-Installed) {
        if ((Get-RunningProcess).Count -gt 0) { $state = 'installiert, läuft'; $color = 'Green' }
        else { $state = 'installiert, läuft gerade nicht'; $color = 'Yellow' }
    }
    Write-Host "  Status: $state" -ForegroundColor $color
    if (-not (Test-Installed)) { return }
    $cfg = Get-Config -NoRepair
    if ($null -eq $cfg) {
        Write-Hint 'config.json ist fehlerhaft – [1] Installieren hilft beim Reparieren.'
        return
    }
    $overlay = 'aus'
    if ($cfg.overlay.enabled) {
        $pos = $Positions["$($cfg.overlay.position)"]
        if (-not $pos) { $pos = "$($cfg.overlay.position)" }
        $overlay = 'an, ' + $pos
    }
    $line = 'Hotkey: {0}  |  Overlay: {1}' -f (Format-Hotkey "$($cfg.hotkey)"), $overlay
    $current = Get-CurrentModeName
    if ($current) { $line += "  |  Trigger: $current" }
    Write-Info $line
}

function Show-Menu {
    while ($true) {
        Write-Title 'TriggerSwitch – Setup'
        Show-Status
        Write-Host ''
        Write-Host '  [1] Installieren / Aktualisieren'
        Write-Host '  [2] Hotkey ändern'
        Write-Host '  [3] Overlay ansehen / einstellen'
        Write-Host '  [4] Log anzeigen (eigenes Fenster)'
        Write-Host '  [5] Neu starten'
        Write-Host '  [6] Pausieren (Maus an Synapse zurückgeben)'
        Write-Host '  [7] Diagnose-Log an/aus'
        Write-Host '  [8] Deinstallieren'
        Write-Host '  [0] Beenden'
        $choice = (Read-Host "`n  Auswahl").Trim()
        try {
            switch ($choice) {
                '1' { Invoke-Install }
                '2' { Invoke-HotkeySetup }
                '3' { Invoke-OverlaySetup }
                '4' { Invoke-ShowLog }
                '5' { Invoke-Restart }
                '6' { Invoke-Pause }
                '7' { Invoke-ToggleVerbose }
                '8' { Invoke-Uninstall }
                '0' { return }
                default { Write-Hint 'Bitte 0–8 eingeben.' }
            }
        } catch {
            Write-Bad $_.Exception.Message
        }
    }
}

# ------------------------------------------------------------------------------ Einstieg

try {
    switch ($Action) {
        'Menu'      { Show-Menu }
        'Install'   { Invoke-Install }
        'Hotkey'    { Invoke-HotkeySetup }
        'Overlay'   { Invoke-OverlaySetup }
        'Log'       { Invoke-ShowLog }
        'Restart'   { Invoke-Restart }
        'Pause'     { Invoke-Pause }
        'Verbose'   { Invoke-ToggleVerbose }
        'Uninstall' { Invoke-Uninstall }
    }
} catch {
    Write-Bad $_.Exception.Message
    exit 1
}
