# Mutation check for the array-list helpers.
#
# WHY THIS EXISTS: list.c sat at 31% line coverage with every element-moving path untested, and
# all of those paths were broken -- a delete that never reduced the caller's count and wrote one
# past the last element, and an insert that shifted the wrong way and smeared one element across
# the tail. Tests written after a fix are easy to write so that they would have passed against
# the bug too, which is exactly the failure mode that let this survive. Each mutant below
# restores one of the original defects, or a plausible near-miss, and must make ctest go red.
#
# HOW IT WORKS: patches src/list.c in place, rebuilds, runs the light_list suite, restores. The
# source is always restored, including on Ctrl-C, but it IS edited in your working tree while
# this runs.
#
# USAGE:  pwsh test/test_light_list/mutants.ps1 [-BuildDir <path>]
param(
        [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build')
)

$ErrorActionPreference = 'Stop'
$src = (Resolve-Path (Join-Path $PSScriptRoot '..\..\module\light_core\src\list.c')).Path

if (-not (Test-Path $BuildDir)) {
        Write-Error "no build directory at $BuildDir -- configure a HOST build first, or pass -BuildDir"
}
$BuildDir = (Resolve-Path $BuildDir).Path

# single-line search strings only: the patcher is a literal replace, not a parser
$mutants = @(
 @('the original *count-- (pointer, not value)',
   '        (*count)--;',
   '        *count--;'),
 @('delete does not shorten the list',
   '        (*count)--;',
   '        ;'),
 @('delete drops the bounds check',
   '        if(index >= *count)',
   '        if(0)'),
 @('the original ascending insert shift',
   '        for(uint8_t i = *count; i > index; i--) {',
   '        for(uint8_t i = index; i < *count; i++) {'),
 @('insert does not grow the list',
   '        (*count)++;',
   '        ;'),
 @('insert drops the clamp',
   '        if(index > *count)',
   '        if(0)'),
 @('indexof always reports the first slot',
   '            return i;',
   '            return 0;'),
 @('indexof reports absent as 0 rather than -1',
   '    return -1;',
   '    return 0;')
)

$original = Get-Content $src -Raw
$restored = $false
function Restore-Source {
        if (-not $script:restored) {
                Set-Content $script:src -Value $script:original -NoNewline
                $script:restored = $true
        }
}
trap { Restore-Source; break }

$survivors = @()
try {
        foreach ($m in $mutants) {
                $name = $m[0]; $find = $m[1]; $replace = $m[2]

                if (-not $original.Contains($find)) {
                        Write-Host "SKIP  $name -- anchor not found, this harness has drifted from list.c" -ForegroundColor Yellow
                        $survivors += "$name (anchor missing)"
                        continue
                }
                Set-Content $src -Value $original.Replace($find, $replace) -NoNewline

                cmake --build $BuildDir --target test_light_list 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) {
                        # a mutant that will not compile is still killed -- it cannot ship
                        Write-Host "killed (build) $name" -ForegroundColor Green
                        continue
                }
                ctest --test-dir $BuildDir -R light_list 2>&1 | Out-Null
                if ($LASTEXITCODE -eq 0) {
                        Write-Host "SURVIVED       $name" -ForegroundColor Red
                        $survivors += $name
                } else {
                        Write-Host "killed         $name" -ForegroundColor Green
                }
        }
} finally {
        Restore-Source
        cmake --build $BuildDir --target test_light_list 2>&1 | Out-Null
}

Write-Host ""
if ($survivors.Count -gt 0) {
        Write-Host "$($survivors.Count) of $($mutants.Count) mutants survived:" -ForegroundColor Red
        $survivors | ForEach-Object { Write-Host "  $_" }
        exit 1
}
Write-Host "all $($mutants.Count) mutants killed" -ForegroundColor Green
