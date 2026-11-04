param([string]$ToolRoot='D:/yc-tools', [string]$BuildDirectory='build', [string]$OutputPath='outputs/package/Youngkly.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
try {
  $buildRoot=Join-Path $root $BuildDirectory
  $target=[IO.Path]::GetFullPath((Join-Path $root $OutputPath))
  if(Test-Path -LiteralPath $target){throw 'Package target exists; choose a new OutputPath'}
  $qtRoot=Join-Path $ToolRoot 'msys64/ucrt64'
  if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin/windeployqt.exe'))) {
    $deployer = Get-Command windeployqt.exe -ErrorAction Stop
    $qtRoot = Split-Path -Parent (Split-Path -Parent $deployer.Source)
  }
  $env:PATH="$qtRoot/bin;$env:PATH"
  # The Qt deployment tools use narrow Windows paths; stage under an ASCII temp path.
  $stage=Join-Path ([IO.Path]::GetTempPath()) ('Youngkly-package-'+[guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $buildRoot 'Youngkly.exe') -Destination "$stage/Youngkly-cli.exe" -Force
  Copy-Item -LiteralPath (Join-Path $buildRoot 'youngkly_app.exe'),(Join-Path $buildRoot 'youngkly_render.exe') -Destination $stage -Force
  & "$qtRoot/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "$stage/youngkly_app.exe" "$stage/Youngkly-cli.exe" "$stage/youngkly_render.exe"
  if($LASTEXITCODE){throw 'Qt deployment failed'}
  # Include dependencies of Qt plugins and MinGW runtime as well as the executables.
  $seen=@{}
  while($true){
    $copied=$false
    foreach($file in Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {$_.Extension -in '.exe','.dll'}){
      if($seen.ContainsKey($file.FullName)){continue};$seen[$file.FullName]=$true
      $imports=& "$qtRoot/bin/objdump.exe" -p $file.FullName
      foreach($line in $imports){if($line -match 'DLL Name:\s*(\S+)'){$name=$Matches[1];$source=Join-Path "$qtRoot/bin" $name;$destination=Join-Path $stage $name;if((Test-Path -LiteralPath $source)-and -not(Test-Path -LiteralPath $destination)){Copy-Item -LiteralPath $source -Destination $destination;$copied=$true}}}
    }
    if(-not $copied){break}
  }
  New-Item -ItemType Directory -Path "$stage/defaults/config" -Force | Out-Null
  Copy-Item -LiteralPath 'config/youngkly.json','config/phrases.ko.json' -Destination "$stage/defaults/config" -Force
  $config=Get-Content 'config/youngkly.json' -Raw | ConvertFrom-Json
  Copy-Item -LiteralPath $config.reference_docx -Destination "$stage/defaults" -Force
  Set-Content -LiteralPath "$stage/defaults/config/future-work.json" -Value '{"schema_version":1,"items":[]}' -Encoding ascii
  $zip=Join-Path $buildRoot 'payload.zip'
  if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip}
  [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
  Copy-Item -LiteralPath (Join-Path $root 'resources/youngkly.ico') -Destination (Join-Path $buildRoot 'youngkly.ico') -Force
  Set-Content -LiteralPath (Join-Path $buildRoot 'payload.rc') -Value @('1 ICON "youngkly.ico"','101 RCDATA "payload.zip"') -Encoding ascii
  Push-Location -LiteralPath $buildRoot
  try {
    & "$qtRoot/bin/windres.exe" -i payload.rc -o payload.o
    if($LASTEXITCODE){throw 'Resource compilation failed'}
    & "$qtRoot/bin/g++.exe" -std=c++20 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ -s '../src/launcher.cpp' payload.o -o 'Youngkly-next.exe' -lz -lshell32 -lole32 -luuid -lgdi32
    if($LASTEXITCODE){throw 'Launcher build failed'}
  } finally {Pop-Location}
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
  Copy-Item -LiteralPath (Join-Path $buildRoot 'Youngkly-next.exe') -Destination $target
  Get-Item -LiteralPath $target | Select-Object FullName,Length
} finally {Pop-Location}
