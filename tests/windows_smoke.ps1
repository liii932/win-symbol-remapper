param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [string]$ScratchDirectory = (Join-Path $PSScriptRoot '../build/smoke-tests')
)
$ErrorActionPreference = 'Stop'
$sourceExe = (Resolve-Path -LiteralPath $Executable).Path
$scratchRoot = [IO.Path]::GetFullPath($ScratchDirectory)
$runDirectory = Join-Path $scratchRoot ('配置-' + [Guid]::NewGuid().ToString('N'))
$exeDirectory = Join-Path $runDirectory 'app'
$workingDirectory = Join-Path $runDirectory 'cwd'
New-Item -ItemType Directory -Path $exeDirectory, $workingDirectory -Force | Out-Null
$smokeExe = Join-Path $exeDirectory 'RestoreChineseSymbol.exe'
Copy-Item -LiteralPath $sourceExe -Destination $smokeExe
$exeConfig = Join-Path $exeDirectory 'schemes.ini'
$cwdConfig = Join-Path $workingDirectory 'schemes.ini'
$utf8 = [Text.UTF8Encoding]::new($false)
$validConfig = "[app]`nversion=1`nstartup=local`n[scheme.local]`nname=本地方案`nbase=empty`nComma=U+FF0C`n"
$badConfig = "[app]`nversion=99`n"

function Test-Startup([string]$Description, [int]$ExpectedCode) {
    $smokeProcess = Start-Process -FilePath $smokeExe -ArgumentList '--smoke-test' `
        -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
    if (-not $smokeProcess.WaitForExit(10000)) {
        $smokeProcess.Kill()
        $smokeProcess.WaitForExit()
        throw "Smoke test timed out: $Description"
    }
    if ($smokeProcess.ExitCode -ne $ExpectedCode) {
        throw "${Description}: expected $ExpectedCode, got $($smokeProcess.ExitCode)"
    }
    Write-Output "PASS: $Description"
}

try {
    [IO.File]::WriteAllText($cwdConfig, $badConfig, $utf8)
    Test-Startup 'missing executable-side config ignores invalid working-directory config' 0
    [IO.File]::WriteAllText($exeConfig, $validConfig, $utf8)
    Test-Startup 'valid executable-side config loads from Unicode path' 0
    [IO.File]::WriteAllText($cwdConfig, $validConfig, $utf8)
    [IO.File]::WriteAllText($exeConfig, $badConfig, $utf8)
    Test-Startup 'invalid executable-side version falls back and reports error' 3
    [IO.File]::WriteAllText($exeConfig, $validConfig + "[scheme.bad]`nComma=U+D800`n", $utf8)
    Test-Startup 'invalid individual scheme reports partial-load error' 3
    [IO.File]::WriteAllText($exeConfig, (' ' * 65537), $utf8)
    Test-Startup 'oversized file is bounded and rejected' 3
    [IO.File]::WriteAllBytes($exeConfig, [byte[]](0xC0, 0xAF))
    Test-Startup 'malformed UTF-8 is rejected' 3
    [IO.File]::WriteAllText($exeConfig, "[app]`nversion=1`nnext_scheme_hotkey=Ctrl+Alt+F9`n", $utf8)
    Test-Startup 'conflicting configured shortcut falls back and reports error' 3
} finally {
    # Only remove this invocation's generated directory, never the caller's
    # workspace, executable, configuration, or shared scratch root.
    $resolvedRun = [IO.Path]::GetFullPath($runDirectory)
    $requiredPrefix = $scratchRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedRun.StartsWith($requiredPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the smoke-test scratch directory.'
    }
    if (Test-Path -LiteralPath $resolvedRun) {
        Remove-Item -LiteralPath $resolvedRun -Recurse -Force
    }
}
