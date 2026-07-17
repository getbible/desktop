[CmdletBinding()]
param(
    [string]$ConfigFile = $env:WEBAPP_CONFIG_FILE,
    [string]$BuildDir = $env:BUILD_DIR,
    [string]$DistDir = $env:DIST_DIR,
    [string]$Architecture = $env:WEBAPP_WINDOWS_ARCHITECTURE,
    [string]$PfxPath = $env:WINDOWS_PFX_PATH,
    [string]$PfxPassword = $env:WINDOWS_PFX_PASSWORD,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This script must run on Windows.' }
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ConfigFile)) { $ConfigFile = Join-Path $RootDir 'config\app.json' }
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $RootDir 'build-windows' }
if ([string]::IsNullOrWhiteSpace($DistDir)) { $DistDir = Join-Path $RootDir 'dist\windows' }

function Invoke-Native([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed with exit code {0}: {1} {2}" -f $LASTEXITCODE, $Command, ($Arguments -join ' '))
    }
}

if (-not $SkipBuild) {
    $BuildParameters = @{ ConfigFile = $ConfigFile; BuildDir = $BuildDir; BuildType = 'Release' }
    if (-not [string]::IsNullOrWhiteSpace($Architecture)) { $BuildParameters.Architecture = $Architecture }
    & (Join-Path $PSScriptRoot 'build-windows.ps1') @BuildParameters
} else {
    Invoke-Native 'python' @((Join-Path $RootDir 'tools\configure_app.py'), '--root', $RootDir, '--config', $ConfigFile, '--output', (Join-Path $RootDir 'generated'))
}

$Config = Get-Content (Join-Path $RootDir 'generated\config.resolved.json') -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($Architecture)) { $Architecture = $Config.windows.architecture }
if ($Architecture -notin @('x64', 'arm64')) { throw "Windows architecture must be x64 or arm64, got '$Architecture'." }
if ($Config.windows.architecture -ne $Architecture) { throw "Configured Windows architecture '$($Config.windows.architecture)' does not match requested build architecture '$Architecture'." }

$Executable = Join-Path $BuildDir "Release\$($Config.app.slug).exe"
if (-not (Test-Path $Executable)) { throw "Windows executable not found: $Executable" }

$StageDir = Join-Path $DistDir 'msix-stage'
if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item $StageDir -ItemType Directory -Force | Out-Null
Copy-Item $Executable (Join-Path $StageDir "$($Config.app.slug).exe")
Copy-Item (Join-Path $RootDir 'generated\windows\AppxManifest.xml') (Join-Path $StageDir 'AppxManifest.xml')
Copy-Item (Join-Path $RootDir 'generated\windows\Assets') (Join-Path $StageDir 'Assets') -Recurse
New-Item $DistDir -ItemType Directory -Force | Out-Null

$WindowsKits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$MakeAppx = Get-ChildItem $WindowsKits -Filter MakeAppx.exe -Recurse -ErrorAction Stop |
    Where-Object { $_.FullName -match '\\x64\\MakeAppx\.exe$' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $MakeAppx) { throw 'MakeAppx.exe was not found. Install the Windows 10/11 SDK.' }

$MsixPath = Join-Path $DistDir "$($Config.app.slug)-$($Config.app.version)-windows-$Architecture.msix"
if (Test-Path $MsixPath) { Remove-Item $MsixPath -Force }
Invoke-Native $MakeAppx.FullName @('pack', '/d', $StageDir, '/p', $MsixPath, '/o')

if (-not [string]::IsNullOrWhiteSpace($PfxPath)) {
    if (-not (Test-Path $PfxPath)) { throw "PFX certificate not found: $PfxPath" }
    $Certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($PfxPath, $PfxPassword)
    if ($Certificate.Subject -ne $Config.windows.publisher) { throw "Certificate subject '$($Certificate.Subject)' does not match windows.publisher '$($Config.windows.publisher)'." }
    $SignTool = Get-ChildItem $WindowsKits -Filter SignTool.exe -Recurse -ErrorAction Stop |
        Where-Object { $_.FullName -match '\\x64\\SignTool\.exe$' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $SignTool) { throw 'SignTool.exe was not found. Install the Windows 10/11 SDK.' }
    Invoke-Native $SignTool.FullName @('sign', '/fd', 'SHA256', '/f', $PfxPath, '/p', $PfxPassword, '/tr', 'http://timestamp.digicert.com', '/td', 'SHA256', $MsixPath)
    Invoke-Native $SignTool.FullName @('verify', '/pa', '/v', $MsixPath)
} else {
    Write-Warning 'WINDOWS_PFX_PATH is not set; the MSIX is unsigned and cannot be installed normally.'
}

Write-Host "Windows package complete: $MsixPath"
