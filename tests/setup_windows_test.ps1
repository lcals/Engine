# No package manager or external program is run by these tests.
# Run: & .\tests\setup_windows_test.ps1
. "$PSScriptRoot\..\scripts\setup.ps1"

function Assert-SetupEqual($Actual, $Expected, [string]$Message)
{
    if ($Actual -ne $Expected)
    {
        throw "${Message}: expected=$Expected actual=$Actual"
    }
}

function Update-EngineProcessPath { }
function Test-EngineTool([string]$Name) { return $script:Ready.Contains($Name) }
function Get-Command
{
    param($Name, $CommandType, $ErrorAction)
    if ($Name -eq 'winget' -and $script:HasWinget)
    {
        return [PSCustomObject]@{ Source = 'Invoke-FakeWinget' }
    }
}
function Invoke-FakeWinget
{
    $script:InstallCalls += ($args -join ' ')
    $global:LASTEXITCODE = 0
    if ($script:InstallWorks)
    {
        $script:Ready += 'ninja'
    }
}

$originalOS = $env:OS
try
{
    $env:OS = 'Windows_NT'
    $script:Ready = @('cmake', 'git', 'ninja', 'sccache')
    $script:InstallCalls = @()
    $script:HasWinget = $false
    $script:InstallWorks = $true
    Assert-SetupEqual (Invoke-EngineSetup $true 'sccache') 0 'Ready tools do not need WinGet'
    Assert-SetupEqual $script:InstallCalls.Count 0 'No redundant installation'

    $script:Ready = @('cmake', 'git', 'sccache')
    Assert-SetupEqual (Invoke-EngineSetup $false 'sccache') 1 'Default check detects missing Ninja'
    Assert-SetupEqual $script:InstallCalls.Count 0 'Default check never installs'
    Assert-SetupEqual (Invoke-EngineSetup $true 'sccache') 2 'Missing WinGet is reported'

    $script:HasWinget = $true
    Assert-SetupEqual (Invoke-EngineSetup $true 'sccache') 0 'Install missing Ninja'
    Assert-SetupEqual $script:InstallCalls.Count 1 'Only one package requested'
    Assert-SetupEqual ($script:InstallCalls[0] -match '--id Ninja-build.Ninja --exact --source winget') `
        $true 'Exact package and source are required'

    $script:Ready = @('cmake', 'git', 'sccache')
    $script:InstallWorks = $false
    Assert-SetupEqual (Invoke-EngineSetup $true 'sccache') 1 'Post-install verification is mandatory'
    $script:Ready = @('cmake', 'git', 'ninja')
    Assert-SetupEqual (Invoke-EngineSetup $false 'none') 0 'Cache can be disabled explicitly'
    Write-Host 'Windows setup tests passed; all installer calls were simulated.'
}
finally
{
    $env:OS = $originalOS
}
