param(
  [Parameter(Mandatory = $true)][string]$PackageRoot,
  [Parameter(Mandatory = $true)][string]$MetadataPath,
  [Parameter(Mandatory = $true)][string]$DlibPath,
  [Parameter(Mandatory = $true)][string]$SignToolPath,
  [Parameter(Mandatory = $true)][string]$IsccPath,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$MetadataPath = (Resolve-Path -LiteralPath $MetadataPath).Path
$DlibPath = (Resolve-Path -LiteralPath $DlibPath).Path
$SignToolPath = (Resolve-Path -LiteralPath $SignToolPath).Path
$IsccPath = (Resolve-Path -LiteralPath $IsccPath).Path
$version = (Get-Content (Join-Path $repositoryRoot 'vcpkg.json') -Raw | ConvertFrom-Json).'version-semver'
$baseName = "panelive-companion-$version-windows-x64"
if ((Split-Path $PackageRoot -Leaf) -ne $baseName) {
  throw "Stage the package in a directory named $baseName before signing."
}
if (Test-Path -LiteralPath $OutputDirectory) {
  throw 'Use a new output directory so a failed signing run cannot mix old and new artifacts.'
}
$metadata = Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json
foreach ($field in @('Endpoint', 'CodeSigningAccountName', 'CertificateProfileName')) {
  if (-not $metadata.$field) { throw "Signing metadata is missing $field" }
}
$binaries = @(
  (Join-Path $PackageRoot 'bin/panelive-companion.exe'),
  (Join-Path $PackageRoot 'obs-plugin/obs-plugins/64bit/panelive-companion-obs-plugin.dll')
)
foreach ($binary in $binaries) {
  if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing package binary: $binary" }
}
$OutputDirectory = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$installerDirectory = Join-Path $OutputDirectory 'installer'

function Assert-Signature([string]$Path) {
  & $SignToolPath verify /pa /all /v /tw $Path
  if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $Path" }
  $signature = Get-AuthenticodeSignature -LiteralPath $Path
  if ($signature.Status -ne 'Valid' -or -not $signature.TimeStamperCertificate) {
    throw "A valid timestamped signature is required: $Path"
  }
}

foreach ($binary in $binaries) {
  & $SignToolPath sign /v /fd SHA256 /tr 'http://timestamp.acs.microsoft.com' /td SHA256 `
    /dlib $DlibPath /dmdf $MetadataPath $binary
  if ($LASTEXITCODE -ne 0) { throw "Signing failed: $binary" }
  Assert-Signature $binary
}

# Inno expands $q/$f itself; avoid shell quoting and preserve literal dollars in paths.
$signCommand = '$q{0}$q sign /v /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib $q{1}$q /dmdf $q{2}$q $f' -f `
  $SignToolPath.Replace('$', '$$'), $DlibPath.Replace('$', '$$'), $MetadataPath.Replace('$', '$$')
$signToolName = 'paneliveArtifactSigning'
& $IsccPath "/DPackageRoot=$PackageRoot" "/DAppVersion=$version" `
  "/DInstallerOutputDir=$installerDirectory" "/DInstallerSignTool=$signToolName" `
  "/DInstallerSignToolCommand=$signCommand" "/S$signToolName=$signCommand" `
  (Join-Path $repositoryRoot 'packaging/windows/panelive-companion.iss')
if ($LASTEXITCODE -ne 0) { throw 'Signed installer compilation failed.' }

$integration = Join-Path $PackageRoot 'installer-tools/panelive-companion-integrations.exe'
$installer = Join-Path $installerDirectory "$baseName-setup.exe"
Assert-Signature $integration
Assert-Signature $installer
Copy-Item -LiteralPath $installer -Destination $OutputDirectory
$archive = Join-Path $OutputDirectory "$baseName.zip"
Compress-Archive -LiteralPath $PackageRoot -DestinationPath $archive -CompressionLevel Optimal

$checksumLines = foreach ($name in @("$baseName-setup.exe", "$baseName.zip")) {
  $hash = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()
  $line = "$hash  $name`n"
  [IO.File]::WriteAllText((Join-Path $OutputDirectory "$name.sha256"), $line, [Text.Encoding]::ASCII)
  $line
}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'SHA256SUMS-windows'), ($checksumLines -join ''), [Text.Encoding]::ASCII)
Write-Output "Signed release artifacts are ready in $OutputDirectory"
