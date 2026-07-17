[CmdletBinding()]
param(
    [string]$ConfigFile = $env:WEBAPP_CONFIG_FILE,
    [string]$BuildDir = $env:BUILD_DIR,
    [string]$Architecture = $env:WEBAPP_WINDOWS_ARCHITECTURE,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildType = 'Release'
)

$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'This script must run on Windows.' }
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ConfigFile)) { $ConfigFile = Join-Path $RootDir 'config\app.json' }
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $RootDir 'build-windows' }

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found in PATH."
    }
}

function Invoke-Native([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE: $Command $($Arguments -join ' ')"
    }
}

Require-Command git
Require-Command cmake
Require-Command python

$ChocRevision = (Get-Content (Join-Path $RootDir '.choc-version') -Raw).Trim()
$ChocDir = Join-Path $RootDir 'subprojects\choc'
$ChocHeader = Join-Path $ChocDir 'choc\gui\choc_WebView.h'

if (-not (Test-Path $ChocHeader)) {
    if (Test-Path (Join-Path $RootDir '.git')) {
        Invoke-Native 'git' @('-C', $RootDir, 'submodule', 'sync', '--', 'subprojects/choc')
        Invoke-Native 'git' @('-C', $RootDir, 'submodule', 'update', '--init', '--recursive', '--', 'subprojects/choc')
    }

    if (-not (Test-Path $ChocHeader)) {
        if (Test-Path $ChocDir) { Remove-Item $ChocDir -Recurse -Force }
        Invoke-Native 'git' @('clone', 'https://github.com/Tracktion/choc.git', $ChocDir)
        Invoke-Native 'git' @('-C', $ChocDir, 'checkout', '--detach', $ChocRevision)
    }
}

if (-not (Test-Path $ChocHeader)) { throw "CHOC could not be prepared at $ChocDir" }

Invoke-Native 'python' @('-c', 'import PIL')
Invoke-Native 'python' @(
    (Join-Path $RootDir 'tools\configure_app.py'),
    '--root', $RootDir,
    '--config', $ConfigFile,
    '--output', (Join-Path $RootDir 'generated')
)

$Config = Get-Content (Join-Path $RootDir 'generated\config.resolved.json') -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($Architecture)) { $Architecture = $Config.windows.architecture }
if ($Architecture -notin @('x64', 'arm64')) { throw "Windows architecture must be x64 or arm64, got '$Architecture'." }
if ($Config.windows.architecture -ne $Architecture) {
    throw "Configured Windows architecture '$($Config.windows.architecture)' does not match requested build architecture '$Architecture'."
}

$CMakeArchitecture = if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x64' }
Invoke-Native 'cmake' @('-S', $RootDir, '-B', $BuildDir, '-A', $CMakeArchitecture, '-DWEBAPP_USE_GENERATED=ON')
Invoke-Native 'cmake' @('--build', $BuildDir, '--config', $BuildType, '--parallel')

$Executable = Join-Path $BuildDir "$BuildType\$($Config.app.slug).exe"
if (-not (Test-Path $Executable)) { throw "Expected Windows executable was not created: $Executable" }
Write-Host "Windows build complete: $Executable"
