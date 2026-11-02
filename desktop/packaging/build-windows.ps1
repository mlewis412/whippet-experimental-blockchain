param([Parameter(Mandatory=$true)][string]$QtPrefix)
$ErrorActionPreference = 'Stop'
$desktopRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $desktopRoot 'build'
$stageDir = Join-Path $desktopRoot 'dist/Whippet'
python (Join-Path $PSScriptRoot 'fetch_node.py') windows
if ($LASTEXITCODE -ne 0) { throw 'Node verification failed' }
cmake -S $desktopRoot -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DWHIPPET_NODE_DIR=$desktopRoot/vendor/windows-node"
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
cmake --build $buildDir --config Release --target whippet-desktop desktop-tests desktop-ui-tests -- /m:1 /p:BuildProjectReferences=false
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
ctest --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
cmake --install $buildDir --config Release --prefix $stageDir
if ($LASTEXITCODE -ne 0) { throw 'Deployment failed' }
$compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Install Inno Setup 6 to create the Windows installer.' }
& $compiler "/DSourceRoot=$stageDir" "/DOutputRoot=$desktopRoot/dist" (Join-Path $PSScriptRoot 'windows.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer creation failed' }
Push-Location $buildDir
try { cpack -C Release -G ZIP -B (Join-Path $desktopRoot 'dist'); if ($LASTEXITCODE -ne 0) { throw 'Portable package failed' } }
finally { Pop-Location }
