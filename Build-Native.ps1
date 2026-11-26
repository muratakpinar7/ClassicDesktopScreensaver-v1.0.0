$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Join-Path $repoRoot 'src\ClassicDesktopScreensaver.Native'
$solution = Join-Path $projectRoot 'ClassicDesktopScreensaver.Native.sln'
$testProject = Join-Path $repoRoot 'tests\ClassicDesktopScreensaver.Native.Tests\NativeTests.vcxproj'
$publishDirectory = Join-Path $projectRoot 'bin\Portable\win-x64'
$executable = Join-Path $publishDirectory 'ClassicDesktopScreensaver.exe'
$settingsFile = Join-Path $publishDirectory 'settings.ini'
$settingsExample = Join-Path $projectRoot 'settings.ini.example'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
if (Test-Path -LiteralPath $vswhere) {
    $installationPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -eq 0 -and $installationPath) {
        $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (Test-Path -LiteralPath $candidate) { $msbuild = $candidate }
    }
}
if (-not $msbuild) {
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    if (Test-Path -LiteralPath $candidate) { $msbuild = $candidate }
}
if (-not $msbuild) {
    throw 'MSBuild with the MSVC v143 x64 toolchain was not found. Install the approved Visual Studio Build Tools components first.'
}

Write-Host 'Building native x64 Release application...'
& $msbuild $solution /m /p:Configuration=Release /p:Platform=x64 /p:WindowsTargetPlatformVersion=10.0.26100.0 /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Native application build failed (exit code $LASTEXITCODE)." }

Write-Host 'Building and running safe native regression tests...'
& $msbuild $testProject /m /p:Configuration=Release /p:Platform=x64 /p:WindowsTargetPlatformVersion=10.0.26100.0 /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Native test build failed (exit code $LASTEXITCODE)." }
$testExecutable = Join-Path (Split-Path -Parent $testProject) 'bin\x64\Release\ClassicDesktopScreensaver.Native.Tests.exe'
if (-not (Test-Path -LiteralPath $testExecutable)) { throw 'The native regression-test executable was not produced.' }
& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "Native regression tests failed (exit code $LASTEXITCODE)." }

if (-not (Test-Path -LiteralPath $executable)) { throw 'The native Release executable was not produced.' }
if (-not (Test-Path -LiteralPath $publishDirectory)) {
    New-Item -ItemType Directory -Path $publishDirectory | Out-Null
}
if (-not (Test-Path -LiteralPath $settingsFile)) {
    Copy-Item -LiteralPath $settingsExample -Destination $settingsFile
    Write-Host 'Created a default settings.ini from settings.ini.example.'
}

$unexpectedFiles = @(Get-ChildItem -LiteralPath $publishDirectory -File | Where-Object { $_.Name -notin @('ClassicDesktopScreensaver.exe', 'settings.ini') })
if ($unexpectedFiles.Count -gt 0) {
    $names = ($unexpectedFiles | ForEach-Object Name) -join ', '
    throw "Unexpected files exist in the portable folder; they were preserved: $names"
}

Write-Host ''
Write-Host 'Portable package:'
Get-ChildItem -LiteralPath $publishDirectory -File | Select-Object Name, Length
Write-Host "Executable: $executable"
Write-Host "File version: $((Get-Item -LiteralPath $executable).VersionInfo.FileVersion)"
Write-Host 'Existing settings.ini is never overwritten by this script.'
