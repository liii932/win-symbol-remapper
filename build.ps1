param(
    [Parameter(Mandatory = $true)]
    [string]$Compiler,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "Compiler does not exist: $Compiler"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$commonFlags = @('-std=c++14', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-static', '-finput-charset=UTF-8', '-fexec-charset=UTF-8')
$appPath = Join-Path $OutputDirectory 'RestoreChineseSymbol.exe'
$schemeSources = @((Join-Path $PSScriptRoot 'src/scheme_config.cpp'), (Join-Path $PSScriptRoot 'src/scheme_manager.cpp'))
& $Compiler @commonFlags '-DUNICODE' '-D_UNICODE' '-D_WIN32_WINNT=0x0A00' '-municode' '-mwindows' (Join-Path $PSScriptRoot 'src/main.cpp') @schemeSources '-o' $appPath '-luser32' '-lshell32'
if ($LASTEXITCODE -ne 0) { throw "Application build failed ($LASTEXITCODE)." }
$testPath = Join-Path $OutputDirectory 'remapper_test.exe'
& $Compiler @commonFlags (Join-Path $PSScriptRoot 'tests/remapper_test.cpp') '-I' (Join-Path $PSScriptRoot 'src') '-o' $testPath
if ($LASTEXITCODE -ne 0) { throw "Test build failed ($LASTEXITCODE)." }
& $testPath
if ($LASTEXITCODE -ne 0) { throw "Key-sequence tests failed ($LASTEXITCODE)." }
$configTestPath = Join-Path $OutputDirectory 'scheme_config_test.exe'
& $Compiler @commonFlags (Join-Path $PSScriptRoot 'tests/scheme_config_test.cpp') @schemeSources '-I' (Join-Path $PSScriptRoot 'src') '-o' $configTestPath
if ($LASTEXITCODE -ne 0) { throw "Config test build failed ($LASTEXITCODE)." }
& $configTestPath (Join-Path $PSScriptRoot 'examples/schemes.ini')
if ($LASTEXITCODE -ne 0) { throw "Config tests failed ($LASTEXITCODE)." }
$configPath = Join-Path $OutputDirectory 'schemes.ini'
if (-not (Test-Path -LiteralPath $configPath)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'examples/schemes.ini') -Destination $configPath
}
Write-Output "Built: $appPath"
