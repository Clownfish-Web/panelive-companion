param(
  [Parameter(Mandatory = $true)]
  [string] $ObsBinaryDirectory,

  [Parameter(Mandatory = $true)]
  [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'

function New-ImportLibrary {
  param(
    [Parameter(Mandatory = $true)]
    [string] $DllName,

    [Parameter(Mandatory = $true)]
    [string] $LibraryName
  )

  $dllPath = Join-Path $ObsBinaryDirectory $DllName
  if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "OBS DLL not found: $dllPath"
  }

  $exports = & dumpbin.exe /nologo /exports $dllPath
  if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed for $dllPath"
  }

  $symbols = foreach ($line in $exports) {
    if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') {
      $matches[1]
    }
  }
  if (-not $symbols) {
    throw "No exported symbols found in $dllPath"
  }

  $definitionPath = Join-Path $OutputDirectory "$LibraryName.def"
  $definition = @("LIBRARY $DllName", 'EXPORTS') +
    ($symbols | Sort-Object -Unique | ForEach-Object { "  $_" })
  [System.IO.File]::WriteAllLines($definitionPath, $definition)

  $libraryPath = Join-Path $OutputDirectory "$LibraryName.lib"
  & lib.exe /nologo /machine:x64 "/def:$definitionPath" "/out:$libraryPath"
  if ($LASTEXITCODE -ne 0) {
    throw "lib.exe failed for $definitionPath"
  }
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

New-ImportLibrary -DllName 'obs.dll' -LibraryName 'obs'
New-ImportLibrary -DllName 'obs-frontend-api.dll' -LibraryName 'obs-frontend-api'

Write-Host "OBS import libraries generated in $resolvedOutput"
