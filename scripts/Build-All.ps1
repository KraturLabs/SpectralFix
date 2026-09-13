[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Ashita416SdkPath,

    [Parameter(Mandatory)]
    [string]$Ashita430SdkPath
)

$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot 'Build-One.ps1') `
    -AshitaVersion '4.16' `
    -AshitaSdkPath $Ashita416SdkPath

& (Join-Path $PSScriptRoot 'Build-One.ps1') `
    -AshitaVersion '4.30' `
    -AshitaSdkPath $Ashita430SdkPath
