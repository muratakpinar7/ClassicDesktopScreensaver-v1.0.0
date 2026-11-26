$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$versionFile = Join-Path $repoRoot 'ReleaseVersion.txt'
$version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "ReleaseVersion.txt must contain a semantic version, got '$version'."
}

$nativeRoot = Join-Path $repoRoot 'src\ClassicDesktopScreensaver.Native'
$nativeExe = Join-Path $nativeRoot 'bin\Portable\win-x64\ClassicDesktopScreensaver.exe'
$settingsTemplate = Join-Path $nativeRoot 'settings.ini.example'
$distRoot = Join-Path $repoRoot 'dist'
$packageName = "ClassicDesktopScreensaver-$version-win-x64"
$packageRoot = Join-Path $distRoot $packageName
$zipPath = Join-Path $distRoot "$packageName.zip"

function Get-Sha256Hex([string]$path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

if (Test-Path -LiteralPath $packageRoot) {
    throw "Refusing to overwrite an existing release folder: $packageRoot"
}
if (Test-Path -LiteralPath $zipPath) {
    throw "Refusing to overwrite an existing release archive: $zipPath"
}

Write-Host "Release version: $version"
Write-Host 'Building native x64 Release and running the safe regression suite...'
& (Join-Path $repoRoot 'Build-Native.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Build-Native.ps1 returned exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $nativeExe)) { throw "Native EXE not found: $nativeExe" }
if (-not (Test-Path -LiteralPath $settingsTemplate)) { throw "Default settings template not found: $settingsTemplate" }

$binary = [IO.File]::ReadAllBytes($nativeExe)
if ($binary.Length -lt 256 -or [BitConverter]::ToUInt16($binary, 0) -ne 0x5A4D) {
    throw 'The release executable does not have a valid DOS/PE header.'
}
$peOffset = [BitConverter]::ToInt32($binary, 0x3C)
if ($peOffset -lt 0 -or $peOffset + 26 -gt $binary.Length -or
    [BitConverter]::ToUInt32($binary, $peOffset) -ne 0x00004550) {
    throw 'The release executable has an invalid PE header offset.'
}
$machine = [BitConverter]::ToUInt16($binary, $peOffset + 4)
$optionalHeaderOffset = $peOffset + 24
$optionalHeaderSize = [BitConverter]::ToUInt16($binary, $peOffset + 20)
$optionalMagic = [BitConverter]::ToUInt16($binary, $optionalHeaderOffset)
if ($machine -ne 0x8664 -or $optionalMagic -ne 0x020B -or $optionalHeaderSize -lt 240) {
    throw ('Expected PE32+ x64 (machine 0x8664, optional header 0x020B), got 0x{0:X4}/0x{1:X4}.' -f $machine, $optionalMagic)
}
$clrDirectoryRva = [BitConverter]::ToUInt32($binary, $optionalHeaderOffset + 112 + (14 * 8))
if ($clrDirectoryRva -ne 0) {
    throw 'The release executable has a CLR header and would require .NET runtime support.'
}

$expectedFileVersion = "$version.0"
$fileVersion = (Get-Item -LiteralPath $nativeExe).VersionInfo.FileVersion
if ($fileVersion -ne $expectedFileVersion) {
    throw "EXE version '$fileVersion' does not match ReleaseVersion '$expectedFileVersion'."
}

New-Item -ItemType Directory -Path $packageRoot | Out-Null
$packageFiles = @(
    @{ Source = $nativeExe; Name = 'ClassicDesktopScreensaver.exe' },
    @{ Source = (Join-Path $repoRoot 'Setup.cmd'); Name = 'Setup.cmd' },
    @{ Source = (Join-Path $repoRoot 'SetupConsole.ps1'); Name = 'SetupConsole.ps1' },
    @{ Source = (Join-Path $repoRoot 'README.md'); Name = 'README.md' },
    @{ Source = (Join-Path $repoRoot 'CHANGELOG.md'); Name = 'CHANGELOG.md' },
    @{ Source = (Join-Path $repoRoot 'LICENSE'); Name = 'LICENSE' },
    @{ Source = $versionFile; Name = 'ReleaseVersion.txt' }
)
foreach ($file in $packageFiles) {
    if (-not (Test-Path -LiteralPath $file.Source)) { throw "Required release file missing: $($file.Source)" }
    Copy-Item -LiteralPath $file.Source -Destination (Join-Path $packageRoot $file.Name)
}
# Ship pristine defaults, never the workstation's portable/user settings file.
Copy-Item -LiteralPath $settingsTemplate -Destination (Join-Path $packageRoot 'settings.ini')

$requiredNames = @(
    'CHANGELOG.md', 'ClassicDesktopScreensaver.exe', 'LICENSE', 'README.md',
    'ReleaseVersion.txt', 'Setup.cmd', 'SetupConsole.ps1', 'settings.ini'
)
$actualNames = @(Get-ChildItem -LiteralPath $packageRoot -File | ForEach-Object Name | Sort-Object)
if (Compare-Object ($requiredNames | Sort-Object) $actualNames) {
    throw "Unexpected files in release folder: $($actualNames -join ', ')"
}

Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zipPath -CompressionLevel Optimal
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $archiveNames = @($archive.Entries | ForEach-Object FullName | Sort-Object)
    if (Compare-Object ($requiredNames | Sort-Object) $archiveNames) {
        throw "Release ZIP contents do not match the expected file set: $($archiveNames -join ', ')"
    }
}
finally {
    $archive.Dispose()
}

$exeInPackage = Join-Path $packageRoot 'ClassicDesktopScreensaver.exe'
$defaultIniHash = Get-Sha256Hex (Join-Path $packageRoot 'settings.ini')
$templateHash = Get-Sha256Hex $settingsTemplate
if ($defaultIniHash -ne $templateHash) { throw 'Release settings.ini is not identical to the default template.' }

Write-Host ''
Write-Host 'Release package verified:'
Get-ChildItem -LiteralPath $packageRoot -File | Sort-Object Name | Select-Object Name, Length | Format-Table -AutoSize
Write-Host "PE machine: x64 (0x$('{0:X4}' -f $machine)); file version: $fileVersion"
Write-Host "EXE: $exeInPackage ($((Get-Item -LiteralPath $exeInPackage).Length) bytes)"
Write-Host "ZIP: $zipPath ($((Get-Item -LiteralPath $zipPath).Length) bytes)"
Write-Host "ZIP SHA-256: $(Get-Sha256Hex $zipPath)"
Write-Host 'The package contains only the EXE, default settings, setup files, documentation, license, and release version.'
