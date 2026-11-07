<#
  FragDesk – Installation für Windows 10/11
  Start: Setup.cmd doppelklicken.

  Optionen:
    -Uninstall   FragDesk entfernen (deine Highlight-Videos bleiben immer erhalten)
    -Quiet       keine Rückfragen
    -NoStart     FragDesk nach der Installation nicht starten
    -Pause       am Ende auf Enter warten (für den Aufruf aus "Installierte Apps")
#>
[CmdletBinding()]
param(
    [switch]$Uninstall,
    [switch]$Quiet,
    [switch]$NoStart,
    [switch]$Pause
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$AppName       = 'FragDesk'
$AppVersion    = '1.0.2'
$SourceDir     = $PSScriptRoot
$InstallDir    = Join-Path $env:LOCALAPPDATA 'Programs\FragDesk'
$DataDir       = Join-Path $env:LOCALAPPDATA 'FragDesk'
$ExePath       = Join-Path $InstallDir 'FragDesk.exe'
$Files         = @('FragDesk.exe', 'fdanalyze.exe')
$Extras        = @('README.md', 'THIRD-PARTY-NOTICES.txt', 'Setup.ps1')
$UninstallKey  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FragDesk'
$ProgId        = 'FragDesk.Demo'
$ClassesKey    = 'HKCU:\Software\Classes'
$StartMenuLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'FragDesk.lnk'
$PowerShellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$DemoTypes     = @('.dem')

# ------------------------------------------------------------------ Ausgabe & Fragen

function Write-Step([string]$Text) { Write-Host ''; Write-Host "» $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text) { Write-Host "  ✓ $Text" -ForegroundColor Green }
function Write-Note([string]$Text) { Write-Host "  · $Text" -ForegroundColor Gray }
function Write-Problem([string]$Text) { Write-Host "  ! $Text" -ForegroundColor Yellow }

# Schlüssel nur anlegen, wenn er fehlt ("New-Item -Force" leert einen vorhandenen Schlüssel samt Werten!)
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

function Get-OsVersion { return [Environment]::OSVersion.Version }

function Test-SamePath([string]$A, [string]$B) {
    try {
        $fa = [IO.Path]::GetFullPath($A).TrimEnd('\', '/')
        $fb = [IO.Path]::GetFullPath($B).TrimEnd('\', '/')
        return [string]::Equals($fa, $fb, [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

# ------------------------------------------------------------------ Programm steuern

function Get-FragDeskProcess {
    # nur Prozesse aus dem Programmordner (oder ohne lesbaren Pfad) – nie fremde Programme
    return @(Get-Process -Name 'FragDesk', 'fdanalyze' -ErrorAction SilentlyContinue | Where-Object {
            $path = $null
            try { $path = $_.Path } catch { $path = $null }
            $null -eq $path -or $path.StartsWith($InstallDir, [StringComparison]::OrdinalIgnoreCase)
        })
}

# Laufendes FragDesk freundlich schließen (Fenster), sonst beenden
function Stop-FragDesk {
    $procs = @(Get-FragDeskProcess)
    if ($procs.Count -eq 0) { return }
    foreach ($p in $procs) { try { $null = $p.CloseMainWindow() } catch { Write-Verbose "$($_.Exception.Message)" } }
    for ($i = 0; $i -lt 40 -and @(Get-FragDeskProcess).Count -gt 0; $i++) { Start-Sleep -Milliseconds 250 }
    foreach ($p in Get-FragDeskProcess) {
        try { $p.Kill(); $null = $p.WaitForExit(3000) } catch { Write-Note "Prozess $($p.Id) ließ sich nicht beenden." }
    }
}

# ------------------------------------------------------------------ Dateien

function Install-Files {
    if (Test-SamePath $SourceDir $InstallDir) {
        if (-not (Test-Path -LiteralPath $ExePath)) { throw 'FragDesk.exe fehlt – bitte Setup.cmd aus dem entpackten ZIP starten.' }
        Write-Ok "Programm ist schon installiert: $InstallDir (Reparatur)"
        return
    }
    foreach ($name in $Files) {
        if (-not (Test-Path -LiteralPath (Join-Path $SourceDir $name))) { throw "$name fehlt neben Setup.ps1 – bitte das ZIP komplett entpacken." }
    }
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    foreach ($name in $Files) {
        $copied = $false
        for ($try = 1; $try -le 10 -and -not $copied; $try++) {
            try {
                Copy-Item -LiteralPath (Join-Path $SourceDir $name) -Destination (Join-Path $InstallDir $name) -Force
                $copied = $true
            } catch {
                if ($try -eq 10) { throw "$name ist noch in Benutzung: $($_.Exception.Message)" }
                Start-Sleep -Milliseconds 500
            }
        }
    }
    foreach ($extra in $Extras) {
        $src = Join-Path $SourceDir $extra
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $InstallDir $extra) -Force }
    }
    # "Aus dem Internet"-Markierung entfernen, sonst fragt Windows bei jedem Start nach
    Get-ChildItem -LiteralPath $InstallDir -File | ForEach-Object { try { Unblock-File -LiteralPath $_.FullName } catch { Write-Verbose "$($_.Exception.Message)" } }
    Write-Ok "Programm installiert: $InstallDir"
}

# ------------------------------------------------------------------ Startmenü, "Öffnen mit", Installierte Apps

function Install-Shortcut {
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($StartMenuLink)
    $link.TargetPath = $ExePath
    $link.WorkingDirectory = $InstallDir
    $link.IconLocation = "$ExePath,0"
    $link.Description = 'CS2-Stats und Highlights aus deinen Demos'
    $link.Save()
}

# FragDesk erscheint bei .dem-Dateien unter "Öffnen mit" – die Standard-App für Demos bleibt unverändert
function Register-OpenWith {
    $cmd = '"' + $ExePath + '" "%1"'
    New-KeyIfMissing "$ClassesKey\$ProgId\shell\open\command"
    Set-ItemProperty -Path "$ClassesKey\$ProgId" -Name '(default)' -Value 'CS2-Demo'
    Set-ItemProperty -Path "$ClassesKey\$ProgId" -Name 'FriendlyTypeName' -Value 'CS2-Demo'
    New-KeyIfMissing "$ClassesKey\$ProgId\DefaultIcon"
    Set-ItemProperty -Path "$ClassesKey\$ProgId\DefaultIcon" -Name '(default)' -Value ('"' + $ExePath + '",0')
    Set-ItemProperty -Path "$ClassesKey\$ProgId\shell\open\command" -Name '(default)' -Value $cmd
    $appKey = "$ClassesKey\Applications\FragDesk.exe"
    New-KeyIfMissing "$appKey\shell\open\command"
    Set-ItemProperty -Path "$appKey\shell\open\command" -Name '(default)' -Value $cmd
    Set-ItemProperty -Path $appKey -Name 'FriendlyAppName' -Value 'FragDesk'
    New-KeyIfMissing "$appKey\SupportedTypes"
    foreach ($ext in $DemoTypes) {
        Set-ItemProperty -Path "$appKey\SupportedTypes" -Name $ext -Value ''
        New-KeyIfMissing "$ClassesKey\$ext\OpenWithProgids"
        Set-ItemProperty -Path "$ClassesKey\$ext\OpenWithProgids" -Name $ProgId -Value ''
    }
}

function Remove-OpenWith {
    foreach ($ext in $DemoTypes) {
        Remove-ItemProperty -Path "$ClassesKey\$ext\OpenWithProgids" -Name $ProgId -ErrorAction SilentlyContinue
    }
    foreach ($k in @("$ClassesKey\$ProgId", "$ClassesKey\Applications\FragDesk.exe")) {
        if (Test-Path -LiteralPath $k) { Remove-Item -LiteralPath $k -Recurse -Force }
    }
}

# Eintrag unter Einstellungen > Apps > Installierte Apps (Deinstallieren ohne das ZIP)
function Register-Uninstaller {
    New-Item -Path $UninstallKey -Force | Out-Null
    $command = '"' + $PowerShellExe + '" -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $InstallDir 'Setup.ps1') + '" -Uninstall -Pause'
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayName' -Value 'FragDesk'
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayVersion' -Value $AppVersion
    Set-ItemProperty -Path $UninstallKey -Name 'Publisher' -Value 'FragDesk'
    Set-ItemProperty -Path $UninstallKey -Name 'DisplayIcon' -Value ('"' + $ExePath + '",0')
    Set-ItemProperty -Path $UninstallKey -Name 'InstallLocation' -Value $InstallDir
    Set-ItemProperty -Path $UninstallKey -Name 'UninstallString' -Value $command
    Set-ItemProperty -Path $UninstallKey -Name 'NoModify' -Value 1 -Type DWord
    Set-ItemProperty -Path $UninstallKey -Name 'NoRepair' -Value 1 -Type DWord
    $kb = 0
    Get-ChildItem -LiteralPath $InstallDir -File -ErrorAction SilentlyContinue | ForEach-Object { $kb += [int][Math]::Ceiling($_.Length / 1024.0) }
    Set-ItemProperty -Path $UninstallKey -Name 'EstimatedSize' -Value $kb -Type DWord
}

# ------------------------------------------------------------------ Installieren / Entfernen

function Install-FragDesk {
    Write-Host ''
    Write-Host '  ⌖  FragDesk – CS2-Stats und Highlights aus deinen Demos' -ForegroundColor White
    $os = Get-OsVersion
    if ($os.Major -lt 10 -or -not [Environment]::Is64BitOperatingSystem) { throw 'FragDesk braucht Windows 10/11 (64 Bit).' }

    Write-Step 'Laufendes FragDesk schließen'
    Stop-FragDesk
    Write-Ok 'erledigt'

    Write-Step 'Dateien kopieren'
    Install-Files

    Write-Step 'Startmenü und „Öffnen mit“'
    try { Install-Shortcut; Write-Ok 'Eintrag im Startmenü angelegt.' } catch { Write-Problem "Startmenü-Eintrag nicht möglich: $($_.Exception.Message)" }
    try { Register-OpenWith; Write-Ok 'Bei .dem-Dateien unter „Öffnen mit“ verfügbar (deine Standard-App bleibt unverändert).' }
    catch { Write-Problem "„Öffnen mit“ nicht eingerichtet: $($_.Exception.Message)" }
    try { Register-Uninstaller; Write-Ok 'Unter Einstellungen > Apps > Installierte Apps eingetragen (zum Deinstallieren).' }
    catch { Write-Note "Eintrag unter Installierte Apps nicht möglich: $($_.Exception.Message)" }
    if ($os.Build -lt 19041) {
        Write-Note 'Hinweis: Den Spielton nimmt FragDesk erst ab Windows 10 Version 2004 auf – Videos wären hier stumm.'
    }

    Write-Host ''
    Write-Host '  Fertig! FragDesk findet deine Demos selbst (CS2-Downloads und Downloads-Ordner).' -ForegroundColor Green
    Write-Host '  Videos landen in Videos\FragDesk. Kein Autostart – FragDesk läuft nur, wenn du es öffnest.' -ForegroundColor Gray
    Write-Host '  Entfernen: Uninstall.cmd oder Einstellungen > Apps > Installierte Apps > FragDesk' -ForegroundColor Gray

    if (-not $NoStart -and (Read-Choice '  FragDesk jetzt öffnen? [J/N]' 'JN' 'J') -eq 'J') {
        Start-Process -FilePath $ExePath -WorkingDirectory $InstallDir
    }
}

function Uninstall-FragDesk {
    # nicht im Programmordner stehen bleiben, sonst lässt er sich nicht löschen (Aufruf aus "Installierte Apps")
    try { Set-Location -LiteralPath $env:TEMP; [Environment]::CurrentDirectory = $env:TEMP } catch { Write-Verbose "$($_.Exception.Message)" }
    Write-Step 'FragDesk schließen'
    Stop-FragDesk
    Write-Ok 'erledigt'

    Write-Step 'Startmenü, „Öffnen mit“ und Registry'
    Remove-Item -LiteralPath $StartMenuLink -Force -ErrorAction SilentlyContinue
    Remove-OpenWith
    if (Test-Path -LiteralPath $UninstallKey) { Remove-Item -LiteralPath $UninstallKey -Recurse -Force }
    Write-Ok 'entfernt'

    Write-Step 'Dateien löschen'
    if (Test-Path -LiteralPath $InstallDir) {
        try { Remove-Item -LiteralPath $InstallDir -Recurse -Force; Write-Ok "$InstallDir gelöscht" }
        catch { Write-Problem "Ein Rest liegt noch in $InstallDir – nach einem Neustart einfach löschen." }
    }
    if ((Test-Path -LiteralPath $DataDir) -and (Read-Choice '  Auswertungen, Einstellungen und Protokoll auch löschen? [J/N]' 'JN' 'N') -eq 'J') {
        Remove-Item -LiteralPath $DataDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Ok 'Auswertungen und Einstellungen gelöscht'
    }
    Write-Note 'Deine Demos und Highlight-Videos (Videos\FragDesk) bleiben erhalten.'
    Write-Host ''
    Write-Host '  FragDesk ist entfernt.' -ForegroundColor Green
}

# ------------------------------------------------------------------ Einstieg

$exitCode = 0
try {
    if ($Uninstall) { Uninstall-FragDesk } else { Install-FragDesk }
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
