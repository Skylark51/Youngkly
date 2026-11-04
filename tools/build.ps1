param([string]$ToolRoot = 'D:/yc-tools', [string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$qtRoot = Join-Path $ToolRoot 'msys64/ucrt64'
$env:PATH = "$qtRoot/bin;$env:PATH"
cmake -S $projectRoot -B "$projectRoot/$BuildDirectory" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ToolRoot/ninja-windows/ninja.exe" "-DCMAKE_CXX_COMPILER=$qtRoot/bin/g++.exe" "-DCMAKE_PREFIX_PATH=$qtRoot" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build "$projectRoot/$BuildDirectory" --parallel 4
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir "$projectRoot/$BuildDirectory" --output-on-failure
exit $LASTEXITCODE
