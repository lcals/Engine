#requires -Version 5.1
# Keep this file ASCII so Windows PowerShell 5.1 reads it without a UTF-8 BOM.
# Package IDs are from https://github.com/microsoft/winget-pkgs/tree/master/manifests
[CmdletBinding()]
param(
    [switch]$Install,
    [ValidateSet('sccache', 'ccache', 'none')]
    [string]$Cache = 'sccache'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Update-EngineProcessPath
{
    # Keep Developer PowerShell's compiler/SDK paths. Do not write registry or profiles.
    $entries = @($env:Path -split ';')
    $entries += @([Environment]::GetEnvironmentVariable('Path', 'Machine') -split ';')
    $entries += @([Environment]::GetEnvironmentVariable('Path', 'User') -split ';')
    if ($env:LOCALAPPDATA)
    {
        $entries += (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links')
    }
    if ($env:ProgramFiles)
    {
        $entries += (Join-Path $env:ProgramFiles 'WinGet\Links')
        $entries += (Join-Path $env:ProgramFiles 'CMake\bin')
        $entries += (Join-Path $env:ProgramFiles 'Git\cmd')
    }
    $env:Path = ($entries | Where-Object { $_ } | Select-Object -Unique) -join ';'
}

function Test-EngineTool
{
    param([string]$Name)
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $command)
    {
        Write-Host "Missing tool: $Name"
        return $false
    }
    try
    {
        $versionOutput = @(& $command.Source --version 2>&1)
        if ($LASTEXITCODE -ne 0 -or $versionOutput.Count -eq 0)
        {
            Write-Host "Tool failed --version: $Name [$($command.Source)]"
            return $false
        }
    }
    catch
    {
        Write-Host "Cannot run ${Name}: $($_.Exception.Message)"
        return $false
    }
    $firstLine = [string]$versionOutput[0]
    if ($Name -eq 'cmake')
    {
        if ($firstLine -notmatch '^cmake version (\d+)\.(\d+)')
        {
            Write-Host "Cannot parse CMake version: $firstLine"
            return $false
        }
        if ([int]$Matches[1] -lt 3 -or
            ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -lt 25))
        {
            Write-Host "CMake >= 3.25 required; found: $firstLine"
            return $false
        }
    }
    Write-Host "Ready: $firstLine [$($command.Source)]"
    return $true
}

function Invoke-EngineSetup
{
    param([bool]$InstallTools, [string]$CacheTool)
    if ($env:OS -ne 'Windows_NT')
    {
        Write-Host 'This entry point requires Windows. Use bash scripts/setup.sh on macOS/Linux.'
        return 2
    }
    Update-EngineProcessPath
    $tools = @('cmake', 'git', 'ninja')
    if ($CacheTool -ne 'none')
    {
        $tools += $CacheTool
    }
    $packages = @{
        cmake = 'Kitware.CMake'
        git = 'Git.Git'
        ninja = 'Ninja-build.Ninja'
        sccache = 'Mozilla.sccache'
        ccache = 'Ccache.Ccache'
    }
    $missing = @($tools | Where-Object { -not (Test-EngineTool $_) })
    if ($missing.Count -eq 0)
    {
        Write-Host 'Host build tools are ready.'
        return 0
    }
    if (-not $InstallTools)
    {
        Write-Host 'Run & .\scripts\setup.ps1 -Install (preserve your -Cache option), then recheck.'
        return 1
    }
    $winget = Get-Command winget -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $winget)
    {
        Write-Host 'WinGet is missing. Install Microsoft App Installer, then retry:'
        Write-Host 'https://learn.microsoft.com/windows/package-manager/winget/'
        return 2
    }
    foreach ($tool in $missing)
    {
        $packageId = $packages[$tool]
        Write-Host "Installing $packageId from the winget source."
        & $winget.Source install --id $packageId --exact --source winget --silent `
            --accept-package-agreements --accept-source-agreements --disable-interactivity |
            Out-Host
        $installStatus = $LASTEXITCODE
        Update-EngineProcessPath
        # An already-installed package may return nonzero; actual usability decides success.
        if (-not (Test-EngineTool $tool))
        {
            Write-Host "Installation/verification failed: $packageId (winget exit $installStatus)."
            Write-Host 'Check WinGet output and PATH; remove stale PATH entries or reopen Developer PowerShell.'
            return 1
        }
    }
    foreach ($tool in $tools)
    {
        if (-not (Test-EngineTool $tool))
        {
            Write-Host 'Final verification failed; fix PATH/tool versions and rerun this script.'
            return 1
        }
    }
    Write-Host 'Host build tools installed and verified. Current PowerShell PATH has been refreshed.'
    Write-Host 'Compiler, platform SDK and vcpkg prerequisites remain documented in README.md.'
    return 0
}

# Dot sourcing exposes functions for isolated tests without installing anything.
if ($MyInvocation.InvocationName -ne '.')
{
    try
    {
        exit (Invoke-EngineSetup -InstallTools $Install.IsPresent -CacheTool $Cache)
    }
    catch
    {
        Write-Host "Setup failed: $($_.Exception.Message)"
        exit 1
    }
}
