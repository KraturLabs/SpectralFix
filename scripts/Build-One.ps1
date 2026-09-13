[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('4.16', '4.30')]
    [string]$AshitaVersion,

    [Parameter(Mandatory)]
    [string]$AshitaSdkPath
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sdkPath = (Resolve-Path -LiteralPath $AshitaSdkPath).Path
$ashitaHeader = Join-Path $sdkPath 'Ashita.h'
if (-not (Test-Path -LiteralPath $ashitaHeader)) {
    throw "Ashita.h was not found under: $sdkPath"
}

$interfaceLine = Select-String -LiteralPath $ashitaHeader -Pattern 'ASHITA_INTERFACE_VERSION' |
    Select-Object -First 1
if ($null -eq $interfaceLine -or $interfaceLine.Line -notmatch "=\s*$([regex]::Escape($AshitaVersion))\s*;") {
    throw "The SDK at '$sdkPath' is not Ashita interface $AshitaVersion."
}

$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($null -eq $cmakeCommand) {
    $knownCMake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $knownCMake)) {
        throw 'CMake was not found on PATH or in the Visual Studio 2022 Build Tools location.'
    }
    $cmakePath = $knownCMake
}
else {
    $cmakePath = $cmakeCommand.Source
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath)) {
    throw "ctest.exe was not found beside CMake: $cmakePath"
}

$suffix = $AshitaVersion.Replace('.', '')
$buildDir = Join-Path $projectRoot "out\build-ashita-$suffix"
$sdkForCMake = $sdkPath.Replace('\', '/')

& $cmakePath -S $projectRoot -B $buildDir -G 'Visual Studio 17 2022' -A Win32 `
    "-DASHITA_SDK_PATH=$sdkForCMake"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& $cmakePath --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

& $ctestPath --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

$binDir = Join-Path $buildDir 'bin\Release'
$pluginPath = Join-Path $binDir 'spectralfix.dll'
$exportSmoke = Join-Path $binDir 'spectralfix_export_smoke.exe'
& $exportSmoke $pluginPath
if ($LASTEXITCODE -ne 0) { throw 'Export smoke test failed.' }

$cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
if ($cmakeText -notmatch 'project\(SpectralFix VERSION ([0-9]+\.[0-9]+)') {
    throw 'Could not read the SpectralFix version from CMakeLists.txt.'
}
$version = $Matches[1]

$outRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))
$stageDir = [IO.Path]::GetFullPath((Join-Path $outRoot "stage-ashita-$suffix"))
if (-not $stageDir.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging path: $stageDir"
}
if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stageDir | Out-Null

Copy-Item -LiteralPath $pluginPath -Destination (Join-Path $stageDir 'spectralfix.dll')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $stageDir 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'packaging\INSTALL.txt') -Destination (Join-Path $stageDir 'INSTALL.txt')
@(
    "SpectralFix v$version"
    "Ashita plugin interface: $AshitaVersion"
) | Set-Content -LiteralPath (Join-Path $stageDir 'BUILD.txt') -Encoding ascii

$packageDir = Join-Path $outRoot 'packages'
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
$zipPath = Join-Path $packageDir "SpectralFix-v$version-Ashita-$AshitaVersion.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $zipPath -CompressionLevel Optimal

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $pluginPath
[pscustomobject]@{
    AshitaInterface = $AshitaVersion
    Plugin = $pluginPath
    PluginSha256 = $hash.Hash
    Package = $zipPath
}
