param(
    [string]$BuildDir = "build/windows-compatible",
    [Parameter(Mandatory = $true)][string]$QtBin,
    [Parameter(Mandatory = $true)][string]$IcuBin,
    [string]$Nsis = "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = (Resolve-Path (Join-Path $root $BuildDir)).Path
$qt = (Resolve-Path $QtBin).Path
$icu = (Resolve-Path $IcuBin).Path
$deploy = Join-Path $qt 'windeployqt.exe'
if (!(Test-Path $deploy) -or !(Test-Path $Nsis)) { throw 'windeployqt.exe or makensis.exe was not found.' }
$match = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(NyaEdit\s+VERSION\s+([0-9.]+)')
if (!$match.Success) { throw 'Cannot determine project version.' }
$version = $match.Groups[1].Value
& cmake --build $build --config Release --target NyaEdit
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$exe = Join-Path $build 'NyaEdit.exe'
if (!(Test-Path $exe)) { $exe = Join-Path $build 'Release/NyaEdit.exe' }
if (!(Test-Path $exe)) { throw 'NyaEdit.exe not found.' }
$stage = Join-Path $build ('package-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $exe $stage
$icuFiles = @(Get-ChildItem -Path $icu -Filter 'icu*.dll' -File)
if ($icuFiles.Count -eq 0) { throw 'ICU DLLs not found.' }
$icuFiles | Copy-Item -Destination $stage
& $deploy --release --no-compiler-runtime --no-translations (Join-Path $stage 'NyaEdit.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
if (!$env:VCToolsRedistDir) { throw 'Run this script from an x64 Visual Studio developer command prompt.' }
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Sort-Object Name -Descending | Select-Object -First 1
if (!$crt) { throw 'MSVC x64 redistributable DLLs not found.' }
Get-ChildItem $crt.FullName -Filter '*.dll' -File | Copy-Item -Destination $stage
$uninstall = Join-Path $build ('uninstall-files-' + [guid]::NewGuid().ToString('N') + '.nsh')
$lines = @()
Get-ChildItem $stage -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace('$', '$$')
    $lines += 'Delete "$INSTDIR\' + $relative + '"'
}
Get-ChildItem $stage -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace('$', '$$')
    $lines += 'RMDir "$INSTDIR\' + $relative + '"'
}
[IO.File]::WriteAllLines($uninstall, $lines, [Text.UTF8Encoding]::new($false))
$output = Join-Path $build "NyaEdit-$version-Setup.exe"
& $Nsis "/DVERSION=$version" "/DSTAGE=$stage" "/DOUTPUT=$output" "/DUNINSTALL_FILES=$uninstall" "/DICON=$(Join-Path $root 'assets/logo.ico')" (Join-Path $root 'packaging/windows.nsi')
if ($LASTEXITCODE -ne 0) { throw 'Installer creation failed.' }
Write-Host "Installer: $output"
Write-Host "Staging directory: $stage"
