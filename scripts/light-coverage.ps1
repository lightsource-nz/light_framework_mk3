# Measures which code paths a project's test suite actually exercises.
#
# WHY THIS EXISTS: the suites report pass/fail and nothing about reach. A green run says the
# tested paths work; it says nothing about how much of the module was tested at all, and that
# gap is where the expensive bugs live -- the heap corruption fixed in font-crusher sat in a
# lookup function with no test touching it.
#
# WHY IT USES WSL *ON WINDOWS*: there is no usable coverage reporting on the Windows toolchain.
# gcov is present but gcovr/lcov are not, and `python` there is the Microsoft Store stub rather
# than an interpreter. clang's -fprofile-instr-generate with llvm-cov is installed under WSL
# (see the ASan work) and gives region, function, line AND branch coverage plus browsable HTML.
#
# ON LINUX there is nothing to cross into: the same worker script runs directly, against the
# same clang and llvm-cov, with no path translation because none is needed. The WSL hop is an
# implementation detail of being on Windows, not part of what this script does -- so the
# platform decision is made once, here, and the worker is identical either way.
#
# The trade to be aware of: this measures a CLANG build of the code, not the toolchain you ship
# with. For "which paths does the suite reach" that is the same answer; for anything
# compiler-specific it is not.
#
# USAGE:  light-coverage.ps1 [-ProjectRoot <path>] [-NoHtml] [-Distro Debian]
#                            [-Functions <glob>] [-Reuse]
#
#   -Functions takes a source filename glob (stream.c, rend_*.c) and adds a PER-FUNCTION
# breakdown for the files that match, on top of the usual per-file table. That is the view you
# actually want when deciding what to test next: a file at 60% tells you there is work to do,
# the function list tells you where, and which of the gaps are worth closing.
#
#   -Reuse skips configure/build/test and reports off the profile data already in the tree.
# The full run is minutes, and when you are reading the same numbers several ways in a row --
# whole project, then one file's functions -- repeating it buys nothing.
param(
        [string]$ProjectRoot,
        [string]$Distro = 'Debian',
        [switch]$NoHtml,
        [string]$Functions,
        [switch]$Reuse
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/LightProject.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'lib/LightPlatform.psm1') -Force

$config = Get-LightProjectConfig -ProjectRoot ($ProjectRoot ? $ProjectRoot : (Get-LightProjectRoot))
if (-not $config.Coverage) {
        throw "project '$($config.Name)' declares no Coverage section in scripts/project.config.ps1"
}
$cov = $config.Coverage

#   the one decision this script makes about where it is running. On Windows the worker has to
# be reached through WSL and every path handed to it translated; on Linux it is simply run, and
# ConvertTo-LightWslPath is the identity there, so the rest of this script is written once
$useWsl = $IsWindows
if ($useWsl -and -not (Test-LightWsl -Distro $Distro)) {
        throw "coverage on Windows needs a WSL distro named '$Distro' with clang and llvm-cov installed (see the header). Found no such distro -- 'wsl -l -q' lists what is available, and -Distro selects a different one."
}

$srcWsl = ConvertTo-LightWslPath $config.Root
$lightWsl = ConvertTo-LightWslPath (Join-Path $PSScriptRoot '..')
#   on Windows the build tree lives on the WSL filesystem, because building onto /mnt/c is
# dramatically slower. On Linux there is no such crossing, so it goes beside the other build
# trees where it belongs -- under the project, which is also where `light-clean -All` looks
$buildWsl = if ($useWsl) { "`$HOME/cov-$($config.Name)" } else { Join-Path $config.Root 'build-coverage/tree' }
# the HTML goes under the project either way, so it can be opened from the host's browser.
# Created here because path resolution needs a real directory and this will not exist first time
$htmlWin = Join-Path $config.Root 'build-coverage/html'
$covRoot = Split-Path $htmlWin -Parent
if (-not (Test-Path $covRoot)) { New-Item -ItemType Directory -Path $covRoot -Force | Out-Null }
$htmlWsl = "$(ConvertTo-LightWslPath $covRoot)/html"

$extra = @("-DLIGHT_PATH=$lightWsl")
foreach ($kv in @($cov.CMakeArgs)) { if ($kv) { $extra += $kv } }

$worker = "$lightWsl/scripts/light-coverage.sh"
$ignore = $cov.IgnoreRegex ? $cov.IgnoreRegex : '(/lib/|/usr/|sanitizers/|_deps/)'

#   parameters go in a sourced file rather than on the command line. wsl.exe joins its arguments
# into a single command line which bash re-parses, so an ignore-regex containing '(' or '|' is
# taken as shell syntax and the run dies before it starts. Written with LF endings because it is
# read by bash
$paramsWin = Join-Path $covRoot 'params.sh'
$lines = @(
        "src='$srcWsl'",
        "build=`"$buildWsl`"",          # double quotes: $HOME must expand inside WSL
        "html='$htmlWsl'",
        "objglob='$($cov.Objects)'",
        "ignore='$ignore'",
        "light='$lightWsl'",
        "functions='$Functions'",
        "reuse='$($Reuse ? 1 : 0)'",
        "extra_args=($(($extra | ForEach-Object { "'$_'" }) -join ' '))"
)
[System.IO.File]::WriteAllText($paramsWin, ($lines -join "`n") + "`n")
$paramsWsl = "$(ConvertTo-LightWslPath $covRoot)/params.sh"

Write-Host "platform: $(Get-LightPlatform)$(if ($useWsl) { " (worker runs in WSL '$Distro')" })"
Write-Host "project : $($config.Name)"
Write-Host "source  : $srcWsl"
Write-Host "build   : $buildWsl$(if ($useWsl) { ' (WSL filesystem)' })"
Write-Host ""

if ($useWsl) {
        & wsl.exe -d $Distro -- bash $worker $paramsWsl
} else {
        #   the same worker, run directly. `bash` explicitly rather than relying on the file's
        # execute bit, which does not survive a checkout onto a filesystem that does not carry it
        $bash = Find-LightTool -Name 'bash'
        if (-not $bash) { throw "coverage needs bash to run $worker, and it is not on PATH" }
        & $bash $worker $paramsWsl
}
$rc = $LASTEXITCODE

if ($rc -ne 0) {
        throw "coverage run failed with exit code $rc"
}
if (-not $NoHtml -and (Test-Path (Join-Path $htmlWin 'index.html'))) {
        Write-Host "html: $(Join-Path $htmlWin 'index.html')"
}
