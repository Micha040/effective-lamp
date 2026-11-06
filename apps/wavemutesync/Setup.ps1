<#
    WaveMuteSync – Setup
    --------------------
    Installiert bzw. aktualisiert WaveMuteSync für den aktuell angemeldeten Windows-Benutzer:
      1. kompiliert WaveMuteSync.cs lokal zu WaveMuteSync.exe (C#-Compiler aus .NET Framework)
      2. legt %LOCALAPPDATA%\WaveMuteSync\config.json an (u. a. Discord-Zugangsdaten)
      3. richtet den Autostart ein (Aufgabenplanung: bei Anmeldung, unsichtbar, Neustart bei Absturz)
    Keine Administratorrechte nötig. Start am einfachsten per Doppelklick auf Setup.cmd.

    Direktaufruf ohne Menü:  Setup.ps1 -Action Install | Discord | Log | Restart | Verbose | Uninstall
#>
[CmdletBinding()]
param(
    [ValidateSet('Menu', 'Install', 'Discord', 'Log', 'Restart', 'Verbose', 'Uninstall')]
    [string]$Action = 'Menu'
)

$ErrorActionPreference = 'Stop'

$AppName    = 'WaveMuteSync'
$InstallDir = Join-Path $env:LOCALAPPDATA $AppName
$ExePath    = Join-Path $InstallDir 'WaveMuteSync.exe'
$ConfigPath = Join-Path $InstallDir 'config.json'
$LogPath    = Join-Path $InstallDir 'WaveMuteSync.log'
$TokenPath  = Join-Path $InstallDir 'discord-token.dat'
$TaskName   = 'WaveMuteSync'
$RunKey     = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$SourcePath = Join-Path $PSScriptRoot 'WaveMuteSync.cs'
$DevPortal  = 'https://discord.com/developers/applications'

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

# ------------------------------------------------------------------------------ Konfiguration

function Add-MissingProperty($Object, [string]$Name, $Value) {
    if (-not $Object.PSObject.Properties[$Name]) {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Get-Config {
    $cfg = $null
    if (Test-Path -LiteralPath $ConfigPath) {
        try {
            $cfg = [System.IO.File]::ReadAllText($ConfigPath) | ConvertFrom-Json
        } catch {
            Write-Hint 'Die vorhandene config.json ist fehlerhaft und wird neu angelegt.'
            $cfg = $null
        }
    }
    if ($null -eq $cfg) { $cfg = New-Object psobject }

    foreach ($section in 'discord', 'teams', 'waveLink') {
        if (-not $cfg.PSObject.Properties[$section] -or $null -eq $cfg.$section) {
            $cfg | Add-Member -NotePropertyName $section -NotePropertyValue (New-Object psobject) -Force
        }
    }
    Add-MissingProperty $cfg.discord 'enabled' $false
    Add-MissingProperty $cfg.discord 'clientId' ''
    Add-MissingProperty $cfg.discord 'clientSecret' ''
    Add-MissingProperty $cfg.teams 'enabled' $true
    Add-MissingProperty $cfg.waveLink 'deviceNameFilter' ''
    Add-MissingProperty $cfg 'verboseLog' $false
    return $cfg
}

function Save-Config($Config) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $json = $Config | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($ConfigPath, $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Test-DiscordConfigured($Config) {
    return ($Config.discord.enabled -eq $true -and "$($Config.discord.clientId)".Trim() -ne '' -and "$($Config.discord.clientSecret)".Trim() -ne '')
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
    Write-Info 'Beende laufendes WaveMuteSync …'
    if (Test-Path -LiteralPath $ExePath) {
        try { Start-Process -FilePath $ExePath -ArgumentList '--stop' -Wait -WindowStyle Hidden } catch { <# egal – unten wird notfalls hart beendet #> }
    }
    $running | Wait-Process -Timeout 6 -ErrorAction SilentlyContinue
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
                        -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
                        -MultipleInstances IgnoreNew -Priority 6
        Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings `
            -Description 'Spiegelt den Mute des Elgato Wave:3 (Wave Link) nach Discord und Microsoft Teams.' -Force | Out-Null
        Remove-ItemProperty -Path $RunKey -Name $AppName -ErrorAction SilentlyContinue
        Write-Ok "Autostart eingerichtet (Aufgabenplanung: „$TaskName“, bei jeder Anmeldung)."
    } catch {
        # Fallback ohne Aufgabenplanung: Autostart-Eintrag des Benutzers (taucht unter Einstellungen > Apps > Autostart auf)
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
        throw "WaveMuteSync.cs nicht gefunden. Die Datei muss im selben Ordner wie Setup.ps1 liegen ($SourcePath)."
    }
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $newExe = Join-Path $InstallDir 'WaveMuteSync.new.exe'
    Remove-Item -LiteralPath $newExe -Force -ErrorAction SilentlyContinue

    Add-Type -AssemblyName System.Core, System.Security, UIAutomationClient, UIAutomationTypes, WindowsBase
    $references = @(
        [System.IO.Pipes.NamedPipeClientStream].Assembly.Location,        # System.Core
        [System.Security.Cryptography.ProtectedData].Assembly.Location,   # System.Security
        [System.Windows.Automation.AutomationElement].Assembly.Location,  # UIAutomationClient
        [System.Windows.Automation.AutomationProperty].Assembly.Location, # UIAutomationTypes
        [System.Windows.Threading.Dispatcher].Assembly.Location           # WindowsBase
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
    Copy-Item -LiteralPath $SourcePath -Destination (Join-Path $InstallDir 'WaveMuteSync.cs') -Force
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

# ------------------------------------------------------------------------------ Discord

function ConvertFrom-SecureText([System.Security.SecureString]$Secure) {
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}

function Test-DiscordCredentials([string]$ClientId, [string]$ClientSecret) {
    # "client_credentials" prüft nur, ob ID und Secret zusammenpassen – ändert nichts.
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("${ClientId}:${ClientSecret}"))
        Invoke-RestMethod -Method Post -Uri 'https://discord.com/api/oauth2/token' -UseBasicParsing -TimeoutSec 15 `
            -Headers @{ Authorization = "Basic $basic" } -ContentType 'application/x-www-form-urlencoded' `
            -Body @{ grant_type = 'client_credentials'; scope = 'identify' } | Out-Null
        return 'ok'
    } catch {
        $status = 0
        try { $status = [int]$_.Exception.Response.StatusCode } catch { <# keine HTTP-Antwort (offline o. ä.) #> }
        if ($status -eq 401) { return 'invalid' }
        return 'unknown'
    }
}

function Invoke-DiscordSetup {
    Write-Title 'Discord einrichten (einmalig, ca. 3 Minuten)'
    Write-Info 'Damit das Programm Discord stumm schalten darf, legst du eine eigene „App“ im'
    Write-Info 'Discord Developer Portal an. Die gehört nur dir und wird nirgends veröffentlicht.'
    Write-Host ''
    Write-Host '  1. Im Developer Portal mit deinem Discord-Konto anmelden.' -ForegroundColor White
    Write-Host '  2. „New Application“ → Name z. B. „Wave Mute Sync“ → Haken setzen → „Create“.' -ForegroundColor White
    Write-Host '  3. Links auf „OAuth2“ klicken:' -ForegroundColor White
    Write-Host '       - „Client ID“ kopieren (steht ganz oben).' -ForegroundColor White
    Write-Host '       - Bei „Client Secret“ auf „Reset Secret“ klicken und den Wert kopieren.' -ForegroundColor White
    Write-Host '       - Unter „Redirects“ auf „Add Redirect“ und genau  http://localhost  eintragen.' -ForegroundColor White
    Write-Host '       - Unten auf „Save Changes“ klicken (wichtig!).' -ForegroundColor White
    Write-Host '  4. Client ID und Client Secret hier einfügen (Rechtsklick oder Strg+V).' -ForegroundColor White
    Write-Host ''
    if (Read-YesNo 'Developer Portal jetzt im Browser öffnen?') { Start-Process $DevPortal }

    $cfg = Get-Config
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        Write-Host ''
        while ($true) {
            $clientId = (Read-Host '  Client ID').Trim()
            if ($clientId -match '^\d{15,22}$') { break }
            Write-Hint 'Die Client ID besteht nur aus Ziffern (meist 18–19 Stück). Bitte nochmal einfügen.'
        }
        while ($true) {
            $clientSecret = (ConvertFrom-SecureText (Read-Host '  Client Secret (bleibt beim Einfügen unsichtbar)' -AsSecureString)).Trim()
            if ($clientSecret.Length -ge 16) { break }
            Write-Hint 'Das Secret ist zu kurz. Im Portal auf „Reset Secret“ klicken und den neuen Wert einfügen.'
        }

        Write-Info 'Prüfe die Zugangsdaten bei Discord …'
        $check = Test-DiscordCredentials $clientId $clientSecret
        if ($check -eq 'ok') {
            Write-Ok 'Client ID und Secret passen zusammen.'
            break
        }
        if ($check -eq 'unknown') {
            Write-Hint 'Konnte die Daten gerade nicht prüfen (Internet?). Sie werden trotzdem gespeichert.'
            break
        }
        Write-Bad 'Discord lehnt diese Kombination aus Client ID und Secret ab.'
        if ($attempt -eq 3 -or -not (Read-YesNo 'Nochmal eingeben?')) {
            Write-Hint 'Discord wurde nicht eingerichtet. Du kannst das später im Setup-Menü nachholen.'
            return $false
        }
    }

    if (-not (Read-YesNo 'Hast du unter OAuth2 → Redirects  http://localhost  eingetragen und gespeichert?')) {
        Write-Hint 'Bitte nachholen – sonst schlägt die Anmeldung fehl. (Portal: OAuth2 → Redirects → Add Redirect → Save Changes)'
    }

    if ("$($cfg.discord.clientId)" -ne $clientId -and (Test-Path -LiteralPath $TokenPath)) {
        Remove-Item -LiteralPath $TokenPath -Force   # Token gehört zur alten App
    }
    $cfg.discord.enabled = $true
    $cfg.discord.clientId = $clientId
    $cfg.discord.clientSecret = $clientSecret
    Save-Config $cfg
    Write-Ok 'Discord-Daten gespeichert.'
    Write-Info 'Gleich erscheint IN DISCORD ein Fenster: „Wave Mute Sync“ möchte sich verbinden → „Autorisieren“.'
    Write-Info '(Nur beim ersten Mal. Falls du es nicht siehst: Discord in den Vordergrund holen.)'
    return $true
}

# ------------------------------------------------------------------------------ Aktionen

function Invoke-Install {
    Write-Title 'WaveMuteSync installieren / aktualisieren'

    Write-Step 'Voraussetzungen'
    if ($PSVersionTable.PSVersion.Major -lt 5) { throw 'Windows PowerShell 5.1 wird benötigt.' }
    if ([Environment]::OSVersion.Version.Major -lt 10) { throw 'Windows 10 oder 11 wird benötigt.' }
    Write-Ok "Windows $([Environment]::OSVersion.Version), PowerShell $($PSVersionTable.PSVersion)"
    $waveLink = Get-AppxPackage -Name 'Elgato.WaveLink*' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($waveLink) { Write-Ok "Elgato Wave Link $($waveLink.Version) gefunden." }
    else { Write-Hint 'Elgato Wave Link 3 wurde nicht gefunden. Ohne Wave Link kann der Mute-Status nicht gelesen werden – bitte installieren (elgato.com).' }
    $teams = Get-AppxPackage -Name 'MSTeams' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($teams) { Write-Ok "Microsoft Teams $($teams.Version) gefunden." } else { Write-Info 'Microsoft Teams (neu) nicht gefunden – der Teams-Teil wartet einfach, bis Teams da ist.' }

    Write-Step 'Programm kompilieren (auf deinem PC, aus WaveMuteSync.cs)'
    Build-App
    Write-Ok "Erstellt: $ExePath"

    Write-Step 'Einstellungen'
    $cfg = Get-Config
    Save-Config $cfg
    Write-Ok "config.json: $ConfigPath"
    if (Test-DiscordConfigured $cfg) {
        Write-Ok 'Discord ist bereits eingerichtet.'
    } elseif (Read-YesNo 'Discord jetzt einrichten? (sonst nur Teams)') {
        Invoke-DiscordSetup | Out-Null
    } else {
        Write-Info 'Discord übersprungen – später im Menü: „Discord einrichten“.'
    }

    Write-Step 'Autostart'
    Register-Autostart

    Write-Step 'Starten'
    $since = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
    Start-App
    Show-NewLogLines $since 8
    if ((Get-RunningProcess).Count -gt 0) {
        Write-Ok 'WaveMuteSync läuft im Hintergrund und startet ab jetzt automatisch mit Windows.'
    } else {
        Write-Bad 'WaveMuteSync läuft nicht – bitte das Log prüfen (Menüpunkt „Log live anzeigen“).'
    }
    Write-Host ''
    Write-Info 'Test: Oben auf das Wave:3 tippen → Discord zeigt „stumm“ (in Teams im laufenden Anruf).'
    Write-Info "Log-Datei: $LogPath"
}

function Invoke-ShowLog {
    if (-not (Test-Path -LiteralPath $LogPath)) { Write-Hint 'Noch keine Log-Datei vorhanden.'; return }
    Write-Title 'Log live (beenden mit Strg+C)'
    Get-Content -LiteralPath $LogPath -Encoding UTF8 -Tail 40 -Wait
}

function Invoke-Restart {
    if (-not (Test-Installed)) { Write-Hint 'WaveMuteSync ist noch nicht installiert.'; return }
    Stop-App
    $since = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
    Start-App
    Show-NewLogLines $since 6
    if ((Get-RunningProcess).Count -gt 0) { Write-Ok 'Neu gestartet.' } else { Write-Bad 'Start fehlgeschlagen – siehe Log.' }
}

function Invoke-ToggleVerbose {
    $cfg = Get-Config
    $cfg.verboseLog = -not [bool]$cfg.verboseLog
    Save-Config $cfg
    if ($cfg.verboseLog) { Write-Ok 'Ausführliches Diagnose-Log ist AN (zeigt u. a. die Beschriftung des Teams-Mikro-Buttons).' }
    else { Write-Ok 'Ausführliches Diagnose-Log ist AUS.' }
    if (Test-Installed) { Invoke-Restart }
}

function Invoke-Uninstall {
    Write-Title 'WaveMuteSync deinstallieren'
    if (-not (Read-YesNo 'Wirklich entfernen (Programm, Autostart, Einstellungen, Discord-Anmeldung)?' $false)) { return }
    Stop-App
    Unregister-Autostart
    if (Test-Path -LiteralPath $InstallDir) { Remove-Item -LiteralPath $InstallDir -Recurse -Force }
    Write-Ok 'Entfernt.'
    Write-Info "Optional: Die Discord-App kannst du unter $DevPortal löschen."
}

function Show-Status {
    $state = 'nicht installiert'
    $color = 'Yellow'
    if (Test-Installed) {
        if ((Get-RunningProcess).Count -gt 0) { $state = 'installiert, läuft'; $color = 'Green' }
        else { $state = 'installiert, läuft gerade nicht'; $color = 'Yellow' }
    }
    Write-Host "  Status: $state" -ForegroundColor $color
    if (Test-Installed) {
        $cfg = Get-Config
        $discord = 'nicht eingerichtet'
        if (Test-DiscordConfigured $cfg) { $discord = 'eingerichtet' }
        $teams = 'aus'
        if ($cfg.teams.enabled) { $teams = 'an' }
        $verbose = 'aus'
        if ($cfg.verboseLog) { $verbose = 'an' }
        Write-Info "Discord: $discord  |  Teams: $teams  |  Diagnose-Log: $verbose"
    }
}

function Show-Menu {
    while ($true) {
        Write-Title "WaveMuteSync – Setup"
        Show-Status
        Write-Host ''
        Write-Host '  [1] Installieren / Aktualisieren'
        Write-Host '  [2] Discord einrichten / ändern'
        Write-Host '  [3] Log live anzeigen'
        Write-Host '  [4] Neu starten'
        Write-Host '  [5] Diagnose-Log an/aus'
        Write-Host '  [6] Deinstallieren'
        Write-Host '  [0] Beenden'
        $choice = (Read-Host "`n  Auswahl").Trim()
        try {
            switch ($choice) {
                '1' { Invoke-Install }
                '2' { if (Invoke-DiscordSetup) { if (Test-Installed) { Invoke-Restart } else { Write-Hint 'Jetzt noch [1] Installieren ausführen.' } } }
                '3' { Invoke-ShowLog }
                '4' { Invoke-Restart }
                '5' { Invoke-ToggleVerbose }
                '6' { Invoke-Uninstall }
                '0' { return }
                default { Write-Hint 'Bitte 0–6 eingeben.' }
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
        'Discord'   { if (Invoke-DiscordSetup) { if (Test-Installed) { Invoke-Restart } } }
        'Log'       { Invoke-ShowLog }
        'Restart'   { Invoke-Restart }
        'Verbose'   { Invoke-ToggleVerbose }
        'Uninstall' { Invoke-Uninstall }
    }
} catch {
    Write-Bad $_.Exception.Message
    exit 1
}
