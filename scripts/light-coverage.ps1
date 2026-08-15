# Measures which code paths a project's test suite actually exercises.
#
# WHY THIS EXISTS: the suites report pass/fail and nothing about reach. A green run says the
# tested paths work; it says nothing about how much of the module was tested at all, and that
# gap is where the expensive bugs live -- the heap corruption fixed in font-crusher sat in a
# lookup function with no test touching it.
#
# WHY IT RUNS IN WSL: there is no usable coverage reporting on the Windows toolchain. gcov is
# present but gcovr/lcov are not, and `python` there is the Microsoft Store stub rather than an
# interpreter. clang's -fprofile-instr-generate with llvm-cov is already installed under WSL
# (see the ASan work) and gives region, function, line AND branch coverage plus browsable HTML.
#
# The trade to be aware of: this measures a CLANG build of the code, not the w64devkit build you
# ship. For "which paths does the suite reach" that is the same answer; for anything
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

$config = Get-LightProjectConfig -ProjectRoot ($ProjectRoot ? $ProjectRoot : (Get-LightProjectRoot))
if (-not $config.Coverage) {
        throw "project '$($config.Name)' declares no Coverage section in scripts/project.config.ps1"
}
$cov = $config.Coverage

# C:\Users\x\y -> /mnt/c/Users/x/y, which is how WSL sees the same tree
function ConvertTo-WslPath {
        param([string]$Path)
        $full = (Resolve-Path $Path).Path
        $drive = $full.Substring(0, 1).ToLower()
        return "/mnt/$drive" + ($full.Substring(2) -replace '\\', '/')
}

$srcWsl = ConvertTo-WslPath $config.Root
$lightWsl = ConvertTo-WslPath (Join-Path $PSScriptRoot '..')
# the build tree lives on the WSL filesystem: building onto /mnt/c is dramatically slower
$buildWsl = "`$HOME/cov-$($config.Name)"
# ...but the HTML goes back under the project so it can be opened from Windows. Created here
# because ConvertTo-WslPath resolves a real path, and build-coverage/ will not exist on a first run
$htmlWin = Join-Path $config.Root 'build-coverage/html'
$covRoot = Split-Path $htmlWin -Parent
if (-not (Test-Path $covRoot)) { New-Item -ItemType Directory -Path $covRoot -Force | Out-Null }
$htmlWsl = "$(ConvertTo-WslPath $covRoot)/html"

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
$paramsWsl = "$(ConvertTo-WslPath $covRoot)/params.sh"

Write-Host "project : $($config.Name)"
Write-Host "source  : $srcWsl"
Write-Host "build   : $buildWsl (WSL filesystem)"
Write-Host ""

& wsl.exe -d $Distro -- bash $worker $paramsWsl
$rc = $LASTEXITCODE

if ($rc -ne 0) {
        throw "coverage run failed with exit code $rc"
}
if (-not $NoHtml -and (Test-Path (Join-Path $htmlWin 'index.html'))) {
        Write-Host "windows path: $(Join-Path $htmlWin 'index.html')"
}
