[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$WingetUpdateNotApplicableExitCode = -1978335189

function Write-Step {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "[Basilisk bootstrap] $Message"
}

function Refresh-ProcessPath {
    $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machinePath;$userPath"
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Name,
        [string[]]$AdditionalArguments = @()
    )

    Write-Step "Ensuring $Name is installed..."
    $arguments = @(
        'install', '--exact', '--id', $Id,
        '--accept-package-agreements', '--accept-source-agreements',
        '--disable-interactivity'
    ) + $AdditionalArguments

    & winget @arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq $WingetUpdateNotApplicableExitCode) {
        Write-Step "$Name is already installed and no update is available."
        return
    }
    if ($exitCode -ne 0) {
        throw "winget could not install or upgrade $Name (package $Id, exit code $exitCode)."
    }
}

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'winget is required. Install or update Microsoft App Installer from the Microsoft Store, then rerun this script.'
}

Install-WingetPackage -Id 'Git.Git' -Name 'Git'
Install-WingetPackage -Id 'Kitware.CMake' -Name 'CMake'
Install-WingetPackage -Id 'Python.Python.3.13' -Name 'Python 3'

# The VCTools workload installs the MSVC compiler. --includeRecommended adds
# its matching Windows SDK and the CMake integration used by VS developer shells.
Install-WingetPackage `
    -Id 'Microsoft.VisualStudio.2022.BuildTools' `
    -Name 'Visual Studio 2022 Build Tools with MSVC and Windows SDK' `
    -AdditionalArguments @(
        '--override',
        '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    )

# SDL3 is fetched and built by the project when BASILISK_BUILD_GAME is enabled;
# no system-wide SDL installation is required.

Refresh-ProcessPath
Write-Step 'Verifying development tools...'

& git --version
if ($LASTEXITCODE -ne 0) { throw 'Git verification failed. Open a new PowerShell terminal and rerun the script.' }

$cmakeVersionOutput = & cmake --version
$cmakeExitCode = $LASTEXITCODE
if ($cmakeExitCode -ne 0) { throw 'CMake verification failed. Open a new PowerShell terminal and rerun the script.' }
$cmakeVersionLine = $cmakeVersionOutput | Select-Object -First 1
Write-Host $cmakeVersionLine
$cmakeVersionText = $cmakeVersionLine -replace '^cmake version\s+', ''
if ([Version]$cmakeVersionText -lt [Version]'3.25') {
    throw "CMake 3.25 or newer is required (found $cmakeVersionText). Upgrade Kitware.CMake with winget and rerun this script."
}

$python = Get-Command python -ErrorAction SilentlyContinue
if ($python) {
    & python --version
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py --version
} else {
    throw 'Python verification failed. Open a new PowerShell terminal and rerun the script.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Installer verification tool (vswhere.exe) was not found.'
}

$installationPath = & $vswhere `
    -latest `
    -products 'Microsoft.VisualStudio.Product.BuildTools' `
    -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
    -property installationPath
if (-not $installationPath) {
    throw 'MSVC C++ tools were not found. Rerun this script, or modify Visual Studio Build Tools and add the Desktop development with C++ workload.'
}

$cl = Get-ChildItem -Path (Join-Path $installationPath 'VC\Tools\MSVC') `
    -Filter cl.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $cl) {
    throw 'Visual Studio Build Tools is present, but cl.exe could not be found.'
}

Write-Host "MSVC compiler: $($cl.FullName)"
$windowsSdkInclude = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$windowsHeader = Get-ChildItem -Path $windowsSdkInclude -Filter windows.h -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $windowsHeader) {
    throw 'The Windows SDK headers were not found. Modify Visual Studio Build Tools and add a Windows 10 or Windows 11 SDK.'
}
Write-Host "Windows SDK: $($windowsHeader.Directory.Parent.FullName)"
Write-Step 'Dependencies are ready. Open a new terminal if newly installed commands are not visible there.'
