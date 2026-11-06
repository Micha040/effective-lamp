<#
  DeskCats – Installation für Windows 10/11
  Start: Setup.cmd doppelklicken.

  Optionen:
    -Uninstall   DeskCats vollständig entfernen
    -Quiet       keine Rückfragen
    -SkipMenu    Kontextmenü-Eintrag nicht einrichten
    -Pause       am Ende auf Enter warten (für den Aufruf aus "Installierte Apps")
#>
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$Quiet,
    [switch]$SkipMenu,
    [switch]$Pause
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$AppName        = 'DeskCats'
$SourceDir      = $PSScriptRoot
$InstallDir     = Join-Path $env:LOCALAPPDATA 'Programs\DeskCats'
$DataDir        = Join-Path $env:LOCALAPPDATA 'DeskCats'
$ExePath        = Join-Path $InstallDir 'DeskCats.exe'
$DllName        = 'DeskCatsMenu.dll'
$MsixName       = 'DeskCatsMenu.msix'
$PackageName    = 'DeskCats.Menu'
$PackageVersion = [version]'1.0.0.0'
$RegKey         = 'HKCU:\Software\DeskCats'
$RunKey         = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$ClassicKey     = 'HKCU:\Software\Classes\DesktopBackground\Shell\DeskCats'
$UninstallKey   = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DeskCats'
$AppVersion     = '1.0.1'
$LogPath        = Join-Path $DataDir 'DeskCats.log'
$PowerShellExe  = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'

# ------------------------------------------------------------------ Ausgabe & Fragen

function Write-Step([string]$Text) { Write-Host ''; Write-Host "» $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text) { Write-Host "  ✓ $Text" -ForegroundColor Green }
function Write-Note([string]$Text) { Write-Host "  · $Text" -ForegroundColor Gray }
function Write-Problem([string]$Text) { Write-Host "  ! $Text" -ForegroundColor Yellow }

# Schlüssel nur anlegen, wenn er fehlt. Achtung: "New-Item -Force" auf einen vorhandenen Registry-Schlüssel
# löscht ihn samt allen Werten und legt ihn leer neu an – beim Run-Schlüssel wären das alle Autostarts!
function New-KeyIfMissing([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { New-Item -Path $Path -Force | Out-Null }
}

# Eine Taste aus $Keys (z. B. 'JN'); Enter = $Default. Im -Quiet-Modus sofort $Default.
function Read-Choice([string]$Question, [string]$Keys, [string]$Default) {
    if ($Quiet) { return $Default }
    while ($true) {
        $answer = Read-Host $Question
        if ([string]::IsNullOrWhiteSpace($answer)) { return $Default }
        $c = $answer.Trim().Substring(0, 1).ToUpperInvariant()
        if ($Keys.ToUpperInvariant().Contains($c)) { return $c }
        Write-Note "Bitte eine dieser Tasten: $($Keys.ToCharArray() -join ', ')"
    }
}

# PowerShell-Zeichenkette in einfachen Anführungszeichen (auch typografische Apostrophe verdoppeln)
function ConvertTo-PsLiteral([string]$Value) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("'")
    foreach ($ch in $Value.ToCharArray()) {
        [void]$sb.Append($ch)
        if ("'‘’‚‛".IndexOf($ch) -ge 0) { [void]$sb.Append($ch) }
    }
    [void]$sb.Append("'")
    return $sb.ToString()
}

# Fehlermeldungen von Windows-Paketen kürzen (der Teil ab "NOTE:" ist nur ein Verweis aufs Ereignisprotokoll)
function Get-ShortError([string]$Message) {
    if ($null -eq $Message) { return '' }
    $i = $Message.IndexOf('NOTE: For additional information')
    if ($i -lt 0) { $i = $Message.IndexOf('HINWEIS: Weitere Informationen') }
    if ($i -ge 0) { $Message = $Message.Substring(0, $i) }
    return (($Message -split "[\r\n]+" | ForEach-Object { $_.Trim() } | Where-Object { $_ }) -join ' ')
}

# ------------------------------------------------------------------ Programm steuern

function Get-OsVersion { return [Environment]::OSVersion.Version }

function Get-DeskCatsProcess {
    return @(Get-Process -Name $AppName -ErrorAction SilentlyContinue)
}

# DeskCats.exe aufrufen und auf das Ende dieses Aufrufs warten (Rückgabe: Exit-Code)
function Invoke-DeskCats([string]$Arguments, [int]$TimeoutSeconds = 60) {
    $p = Start-Process -FilePath $ExePath -ArgumentList $Arguments -WorkingDirectory $InstallDir -PassThru
    $null = $p.Handle   # sonst ist ExitCode in Windows PowerShell 5.1 manchmal leer
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) { return -2 }
    return $p.ExitCode
}

# Laufendes DeskCats sauber beenden (falls nötig hart)
function Stop-DeskCats {
    if (@(Get-DeskCatsProcess).Count -eq 0) { return }
    if (Test-Path $ExePath) {
        try { $null = Invoke-DeskCats '--cmd exit' 10 } catch { Write-Note "Beenden per Befehl ging nicht: $($_.Exception.Message)" }
    }
    for ($i = 0; $i -lt 30 -and @(Get-DeskCatsProcess).Count -gt 0; $i++) { Start-Sleep -Milliseconds 200 }
    foreach ($p in Get-DeskCatsProcess) {
        try { $p.Kill(); $null = $p.WaitForExit(3000) } catch { Write-Note "Prozess $($p.Id) ließ sich nicht beenden." }
    }
}

# ------------------------------------------------------------------ Dateien

# Eine von Explorer geladene DLL lässt sich nicht überschreiben, aber umbenennen
function Copy-LockedFile([string]$From, [string]$To) {
    if (Test-Path -LiteralPath $To) {
        $same = $false
        try { $same = (Get-FileHash -LiteralPath $From).Hash -eq (Get-FileHash -LiteralPath $To).Hash } catch { $same = $false }
        if ($same) { return 'unverändert' }
        try {
            Remove-Item -LiteralPath $To -Force
        } catch {
            $old = $To + '.old-' + (Get-Date -Format 'yyyyMMddHHmmss')
            [IO.File]::Move($To, $old)
        }
    }
    Copy-Item -LiteralPath $From -Destination $To -Force
    return 'kopiert'
}

function Test-SamePath([string]$A, [string]$B) {
    try {
        $fa = [IO.Path]::GetFullPath($A).TrimEnd('\', '/')
        $fb = [IO.Path]::GetFullPath($B).TrimEnd('\', '/')
        return [string]::Equals($fa, $fb, [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

function Install-Files {
    if (Test-SamePath $SourceDir $InstallDir) {
        # Setup.ps1 aus dem Programmordner gestartet: Dateien sind schon da, nur den Rest neu einrichten
        if (-not (Test-Path -LiteralPath $ExePath)) { throw 'DeskCats.exe fehlt – bitte Setup.cmd aus dem entpackten ZIP starten.' }
        Write-Ok "Programm ist schon installiert: $InstallDir (Reparatur)"
        return
    }
    foreach ($name in @('DeskCats.exe', $DllName)) {
        if (-not (Test-Path -LiteralPath (Join-Path $SourceDir $name))) { throw "$name fehlt neben Setup.ps1 – bitte das ZIP komplett entpacken." }
    }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    $copied = $false
    for ($try = 1; $try -le 10 -and -not $copied; $try++) {
        try {
            Copy-Item -LiteralPath (Join-Path $SourceDir 'DeskCats.exe') -Destination $ExePath -Force
            $copied = $true
        } catch {
            if ($try -eq 10) { throw "DeskCats.exe ist noch in Benutzung: $($_.Exception.Message)" }
            Start-Sleep -Milliseconds 500
        }
    }
    $dllState = Copy-LockedFile (Join-Path $SourceDir $DllName) (Join-Path $InstallDir $DllName)
    foreach ($extra in @('README.md', 'Setup.ps1')) {
        $src = Join-Path $SourceDir $extra
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $InstallDir $extra) -Force }
    }
    # alte, umbenannte DLLs aufräumen (klappt, sobald Explorer sie nicht mehr geladen hat)
    Get-ChildItem -LiteralPath $InstallDir -Filter "$DllName.old-*" -ErrorAction SilentlyContinue | ForEach-Object {
        try { Remove-Item -LiteralPath $_.FullName -Force } catch { Write-Verbose "$($_.Exception.Message)" }
    }
    # "Aus dem Internet"-Markierung entfernen, sonst fragt Windows bei jedem Start nach
    Get-ChildItem -LiteralPath $InstallDir -File | ForEach-Object { try { Unblock-File -LiteralPath $_.FullName } catch { Write-Verbose "$($_.Exception.Message)" } }
    Write-Ok "Programm installiert: $InstallDir (Kontextmenü-DLL $dllState)"
}

# ------------------------------------------------------------------ Kontextmenü (Windows 11)

function Test-MenuPackage {
    $pkg = @(Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue)
    if ($pkg.Count -eq 0) { return 'fehlt' }
    $location = (Get-ItemProperty -Path $RegKey -Name MenuLocation -ErrorAction SilentlyContinue)
    $sameDir = $null -ne $location -and $location.MenuLocation -eq $InstallDir
    if ([version]$pkg[0].Version -ge $PackageVersion -and $sameDir) { return 'ok' }
    return 'veraltet'
}

function New-MenuPackage {
    $msix = Join-Path $InstallDir $MsixName
    Remove-Item -LiteralPath $msix -Force -ErrorAction SilentlyContinue
    $code = Invoke-DeskCats ('--pack-menu "' + $InstallDir + '"') 60
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $msix)) {
        throw "Das Paket für das Kontextmenü konnte nicht gebaut werden (Code $code). Details: $LogPath"
    }
    return $msix
}

# Die PowerShell-Befehlszeile für die Registrierung (wird mit Administratorrechten ausgeführt)
function Get-RegisterScript([string]$Msix, [string]$ErrorFile) {
    $cmd = "Add-AppxPackage -Path $(ConvertTo-PsLiteral $Msix) -ExternalLocation $(ConvertTo-PsLiteral $InstallDir) -AllowUnsigned -ForceUpdateFromAnyVersion"
    return "`$ErrorActionPreference = 'Stop'; try { $cmd; exit 0 } catch { [IO.File]::WriteAllText($(ConvertTo-PsLiteral $ErrorFile), `$_.Exception.Message); exit 1 }"
}

# Gibt $true zurück, wenn der Eintrag im neuen Kontextmenü eingerichtet ist
function Register-MenuPackage {
    $msix = New-MenuPackage
    $errorFile = Join-Path $env:TEMP ('DeskCats-menu-' + [guid]::NewGuid().ToString('N') + '.txt')
    $script = Get-RegisterScript $msix $errorFile
    if ($script.Contains('"')) { throw 'Interner Fehler: Skript enthält Anführungszeichen.' }
    Write-Note 'Windows fragt gleich einmal nach Administratorrechten (nur für den Kontextmenü-Eintrag).'
    $code = $null
    try {
        $p = Start-Process -FilePath $PowerShellExe -ArgumentList ('-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "' + $script + '"') -Verb RunAs -WindowStyle Hidden -PassThru
        $null = $p.Handle
        if (-not $p.WaitForExit(180000)) { Write-Problem 'Windows hat nicht rechtzeitig geantwortet.'; return $false }
        $code = $p.ExitCode
    } catch {
        Write-Problem 'Administratorrechte wurden nicht erteilt.'
        return $false
    }
    if ($code -ne 0) {
        $msg = ''
        if (Test-Path -LiteralPath $errorFile) { $msg = Get-ShortError ([IO.File]::ReadAllText($errorFile)); Remove-Item -LiteralPath $errorFile -Force -ErrorAction SilentlyContinue }
        if ($msg -eq '') { $msg = "Code $code" }
        Write-Problem "Windows hat das Paket abgelehnt: $msg"
        return $false
    }
    if (@(Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue).Count -eq 0) {
        Write-Problem 'Das Paket ist registriert, aber nicht für dieses Benutzerkonto (wurde ein anderes Administrator-Konto benutzt?).'
        return $false
    }
    New-KeyIfMissing $RegKey
    Set-ItemProperty -Path $RegKey -Name MenuLocation -Value $InstallDir
    return $true
}

# Ersatz ohne Adminrechte: klassischer Eintrag (Windows 11: unter "Weitere Optionen anzeigen")
function Install-ClassicMenu {
    Remove-ClassicMenu
    New-Item -Path $ClassicKey -Force | Out-Null
    Set-ItemProperty -Path $ClassicKey -Name 'MUIVerb' -Value 'Katzen'
    Set-ItemProperty -Path $ClassicKey -Name 'Icon' -Value ('"' + $ExePath + '",0')
    Set-ItemProperty -Path $ClassicKey -Name 'SubCommands' -Value ''
    $items = @(
        @('1feed', 'Füttern', 'feed'),
        @('2play', 'Spielen', 'play'),
        @('3call', 'Herrufen', 'call'),
        @('4sleep', 'Schlafen schicken', 'sleep'),
        @('5wake', 'Aufwecken', 'wake'),
        @('6toggle', 'Katzen ein-/ausblenden', 'toggle'),
        @('7settings', 'Einstellungen …', 'settings')
    )
    foreach ($it in $items) {
        $key = "$ClassicKey\shell\$($it[0])"
        New-Item -Path "$key\command" -Force | Out-Null
        Set-ItemProperty -Path $key -Name 'MUIVerb' -Value $it[1]
        Set-ItemProperty -Path "$key\command" -Name '(default)' -Value ('"' + $ExePath + '" --cmd ' + $it[2])
    }
}

function Remove-ClassicMenu {
    if (Test-Path -LiteralPath $ClassicKey) { Remove-Item -LiteralPath $ClassicKey -Recurse -Force }
}

# Eintrag unter Einstellungen > Apps > Installierte Apps (Deinstallieren ohne das ZIP)
function Register-Uninstaller {
    New-Item -Path $UninstallKey -Force | Out-Null
    $command = '"' + $PowerShellExe + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $InstallDir 'Setup.ps1') + '" -Uninstall -Pause'
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayName' -Value 'DeskCats'
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayVersion' -Value $AppVersion
    Set-ItemProperty -Path $UninstallKey -Name 'Publisher' -Value 'DeskCats'
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayIcon' -Value ('"' + $ExePath + '",0')
    Set-ItemProperty -Path $UninstallKey -Name 'InstallLocation' -Value $InstallDir
    Set-ItemProperty -Path $UninstallKey -Name 'UninstallString' -Value $command
    Set-ItemProperty -Path $UninstallKey -Name 'NoModify' -Value 1 -Type DWord
    Set-ItemProperty -Path $UninstallKey -Name 'NoRepair' -Value 1 -Type DWord
    $kb = 0
    Get-ChildItem -LiteralPath $InstallDir -File -ErrorAction SilentlyContinue | ForEach-Object { $kb += [int][Math]::Ceiling($_.Length / 1024.0) }
    Set-ItemProperty -Path $UninstallKey -Name 'EstimatedSize' -Value $kb -Type DWord
}

function Install-Menu {
    if ($SkipMenu) { Write-Note 'Kontextmenü übersprungen (-SkipMenu).'; return }
    $build = (Get-OsVersion).Build
    if ($build -lt 22000) {
        Install-ClassicMenu
        Write-Ok 'Kontextmenü-Eintrag "Katzen" eingerichtet (Windows 10: klassisches Menü).'
        return
    }
    $state = Test-MenuPackage
    if ($state -eq 'ok') {
        Remove-ClassicMenu
        Write-Ok 'Eintrag "Katzen" im Windows-11-Kontextmenü ist schon eingerichtet.'
        return
    }
    if ($state -eq 'veraltet') { Write-Note 'Der Kontextmenü-Eintrag wird aktualisiert.' }
    $ok = $false
    try { $ok = Register-MenuPackage } catch { Write-Problem $_.Exception.Message }
    if ($ok) {
        Remove-ClassicMenu
        Write-Ok 'Eintrag "Katzen" im Windows-11-Kontextmenü eingerichtet (Rechtsklick auf den Desktop).'
    } else {
        Install-ClassicMenu
        Write-Problem 'Ersatzweise eingerichtet: Rechtsklick auf den Desktop → "Weitere Optionen anzeigen" → "Katzen".'
        Write-Note 'Für den Eintrag direkt im neuen Menü Setup.cmd später noch einmal starten und Adminrechte erlauben.'
    }
}

function Remove-MenuPackage {
    $pkg = @(Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue)
    if ($pkg.Count -eq 0) { return $true }
    try {
        $pkg | Remove-AppxPackage -ErrorAction Stop
        return $true
    } catch {
        Write-Note 'Zum Entfernen des Kontextmenü-Pakets fragt Windows nach Administratorrechten.'
        try {
            $script = "Get-AppxPackage -AllUsers -Name '$PackageName' | Remove-AppxPackage -AllUsers"
            $p = Start-Process -FilePath $PowerShellExe -ArgumentList ('-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "' + $script + '"') -Verb RunAs -WindowStyle Hidden -PassThru
            $null = $p.Handle
            $null = $p.WaitForExit(180000)
        } catch {
            Write-Problem 'Ohne Administratorrechte bleibt das (leere) Kontextmenü-Paket registriert.'
        }
        return @(Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue).Count -eq 0
    }
}

# ------------------------------------------------------------------ Installieren / Entfernen

function Install-DeskCats {
    Write-Host ''
    Write-Host '  =^.^=  DeskCats – Fatos und Kosta auf dem Desktop' -ForegroundColor White
    if ((Get-OsVersion).Major -lt 10 -or -not [Environment]::Is64BitOperatingSystem) {
        throw 'DeskCats braucht Windows 10/11 (64 Bit).'
    }

    Write-Step 'Altes DeskCats beenden'
    Stop-DeskCats
    Write-Ok 'erledigt'

    Write-Step 'Dateien kopieren'
    Install-Files

    Write-Step 'Kontextmenü einrichten'
    Install-Menu

    Write-Step 'Autostart'
    New-KeyIfMissing $RunKey
    Set-ItemProperty -Path $RunKey -Name $AppName -Value ('"' + $ExePath + '" --autostart')
    Write-Ok 'DeskCats startet mit Windows (abschaltbar in den Einstellungen).'
    try { Register-Uninstaller; Write-Ok 'Unter Einstellungen > Apps > Installierte Apps eingetragen (zum Deinstallieren).' }
    catch { Write-Note "Eintrag unter Installierte Apps nicht möglich: $($_.Exception.Message)" }

    Write-Step 'DeskCats starten'
    # sichtbar starten, auch wenn sie bei einer früheren Installation ausgeblendet wurden
    New-KeyIfMissing $RegKey
    Set-ItemProperty -Path $RegKey -Name 'Visible' -Value 1 -Type DWord
    Start-Process -FilePath $ExePath -ArgumentList '--autostart' -WorkingDirectory $InstallDir
    for ($i = 0; $i -lt 50 -and @(Get-DeskCatsProcess).Count -eq 0; $i++) { Start-Sleep -Milliseconds 100 }
    Start-Sleep -Seconds 2
    if (@(Get-DeskCatsProcess).Count -eq 0) { throw "DeskCats ist nicht gestartet. Details: $LogPath" }
    Write-Ok 'läuft – Fatos und Kosta sitzen jetzt unten auf dem Bildschirm.'

    Write-Host ''
    Write-Host '  Fertig! Rechtsklick auf den Desktop → "Katzen" zum Füttern, Spielen, Rufen und für die Einstellungen.' -ForegroundColor Green
    Write-Host '  Katzen selbst: anklicken = streicheln, ziehen = tragen, Rechtsklick = Menü.' -ForegroundColor Gray
    Write-Host '  Entfernen: Uninstall.cmd oder Einstellungen > Apps > Installierte Apps > DeskCats' -ForegroundColor Gray
}

function Uninstall-DeskCats {
    # nicht im Programmordner stehen bleiben, sonst lässt er sich nicht löschen (Aufruf aus "Installierte Apps")
    try { Set-Location -LiteralPath $env:TEMP; [Environment]::CurrentDirectory = $env:TEMP } catch { Write-Verbose "$($_.Exception.Message)" }
    Write-Step 'DeskCats beenden'
    Stop-DeskCats
    Write-Ok 'erledigt'

    Write-Step 'Kontextmenü entfernen'
    if (Remove-MenuPackage) { Write-Ok 'Paket entfernt' }
    Remove-ClassicMenu
    Write-Ok 'Einträge entfernt'

    Write-Step 'Autostart und Registry'
    Remove-ItemProperty -Path $RunKey -Name $AppName -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $RegKey) { Remove-Item -LiteralPath $RegKey -Recurse -Force }
    if (Test-Path -LiteralPath $UninstallKey) { Remove-Item -LiteralPath $UninstallKey -Recurse -Force }
    Write-Ok 'entfernt'

    Write-Step 'Dateien löschen'
    if (Test-Path -LiteralPath $InstallDir) {
        # Die DLL kann noch in Explorer geladen sein: dann in den Temp-Ordner schieben (wird dort später aufgeräumt)
        Get-ChildItem -LiteralPath $InstallDir -Filter "$DllName*" -ErrorAction SilentlyContinue | ForEach-Object {
            $file = $_.FullName   # im catch-Block ist $_ der Fehler, nicht mehr die Datei
            try { Remove-Item -LiteralPath $file -Force } catch {
                try { [IO.File]::Move($file, (Join-Path $env:TEMP ('DeskCatsMenu-' + [guid]::NewGuid().ToString('N') + '.dll'))) } catch { Write-Verbose "$($_.Exception.Message)" }
            }
        }
        try { Remove-Item -LiteralPath $InstallDir -Recurse -Force; Write-Ok "$InstallDir gelöscht" }
        catch { Write-Problem "Ein Rest liegt noch in $InstallDir (wird von Explorer benutzt) – nach einem Neustart einfach löschen." }
    }
    if ((Test-Path -LiteralPath $DataDir) -and (Read-Choice '  Protokoll auch löschen? [J/N]' 'JN' 'J') -eq 'J') {
        Remove-Item -LiteralPath $DataDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok 'Protokoll gelöscht'
    }
    Write-Host ''
    Write-Host '  DeskCats ist entfernt. Tschüss von Fatos und Kosta!' -ForegroundColor Green
}

# ------------------------------------------------------------------ Einstieg

$exitCode = 0
try {
    if ($Uninstall) { Uninstall-DeskCats } else { Install-DeskCats }
} catch {
    Write-Host ''
    Write-Host "  Fehler: $($_.Exception.Message)" -ForegroundColor Red
    $exitCode = 1
}
if ($Pause -and -not $Quiet) {
    Write-Host ''
    try { $null = Read-Host '  Enter drücken zum Schließen' } catch { Write-Verbose "$($_.Exception.Message)" }
}
exit $exitCode
