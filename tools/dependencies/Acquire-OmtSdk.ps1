[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$version = 'v1.0.0.16'
$archiveName = "OpenMediaTransport.Binaries.Release.$version.zip"
$expectedHash = 'C70E67F7E2A7ED5B4C389D99AF62796A8C9C7BE23C8DEBFAE3FD8020C1DC66B9'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildToolsDirectory = Join-Path $repositoryRoot '.buildtools'
$downloadDirectory = Join-Path $buildToolsDirectory 'downloads'
$archivePath = Join-Path $downloadDirectory $archiveName
$sdkDirectory = Join-Path $buildToolsDirectory "omt-$version"
$downloadUrl =
  "https://github.com/openmediatransport/libomtnet/releases/download/$version/$archiveName"

New-Item -ItemType Directory -Force -Path $downloadDirectory | Out-Null

if (-not (Test-Path -LiteralPath $archivePath)) {
  Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath
}

$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) {
  throw "OMT SDK archive hash mismatch. Expected $expectedHash but found $actualHash."
}

if (-not (Test-Path -LiteralPath $sdkDirectory)) {
  Expand-Archive -LiteralPath $archivePath -DestinationPath $sdkDirectory
}

$requiredFiles = @(
  'LICENSE.txt',
  'Libraries\Winx64\libomt.dll',
  'Libraries\Winx64\libomt.h',
  'Libraries\Winx64\libomt.lib'
)
foreach ($relativePath in $requiredFiles) {
  $requiredPath = Join-Path $sdkDirectory $relativePath
  if (-not (Test-Path -LiteralPath $requiredPath)) {
    throw "The OMT SDK is incomplete. Missing: $requiredPath"
  }
}

Write-Host "OMT SDK $version is ready at $sdkDirectory"
Write-Host "Archive SHA-256: $actualHash"
