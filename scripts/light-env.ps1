# Sets up the toolchain environment every light project build depends on.
#
# WHY THIS EXISTS: this contract was only ever written down in .vscode/settings.json -- and in
# crossfire's case that file is gitignored, so the knowledge existed on exactly one machine.
# Anything driving a build from a plain shell had to rediscover it, and the failures are
# indirect enough to cost real time. Both rules below were learned by breaking them.
#
# USAGE:  . $env:LIGHT_PATH/scripts/light-env.ps1   [-Quiet]
#
# Dot-sourced, not run: it modifies the current session's environment. Running it as a child
# process would set variables that vanish when it exits.
param(
        [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

$tools = 'C:/Users/aful018/tools'

#   LIGHT_PATH is the anchor for everything else -- CMake reads it from the environment
# (screen-test/CMakeLists.txt) and light_preinit.cmake resolves the framework against it.
# Derived from this script's own location so a moved or cloned checkout still works.
$frameworkRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $env:LIGHT_PATH) { $env:LIGHT_PATH = $frameworkRoot }

#   PICO_SDK_PATH MUST be set before the FIRST configure of any pico tree. The pico presets pass
# it through as $env{PICO_SDK_PATH}, and when it is empty pico_sdk_init() never runs. That fails
# late and misleadingly -- as "Unknown CMake command pico_enable_stdio_semihosting" -- and worse,
# the failed configure caches the HOST compiler, so a later correct configure still builds
# boot_stage2 with w64devkit's cc.exe. The only recovery is deleting the build tree.
if (-not $env:PICO_SDK_PATH) { $env:PICO_SDK_PATH = 'C:/Users/aful018/projects/c/pico-sdk' }

#   without this, rend's own Findcrush.cmake defaults FONT_CRUSHER_PATH relative to ITSELF,
# misses the sibling checkout, and silently FetchContent-fetches font-crusher from GitHub --
# so local crush edits have no effect at all. screen-test works around it in CMake; crossfire
# does not, and every one of its build trees is currently building against a fetched copy.
if (-not $env:FONT_CRUSHER_PATH) { $env:FONT_CRUSHER_PATH = 'C:/Users/aful018/projects/c/font-crusher' }

#   order matters: w64devkit supplies the host gcc/cmake/ninja, and must come before anything
# else that ships a cmake. The RISC-V toolchain is deliberately ABSENT -- it reaches the build
# through PICO_TOOLCHAIN_PATH in the riscv preset, and putting it on PATH breaks the ARM trees.
$required = @(
        "$tools/w64devkit/bin",
        "$tools/arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi/bin",
        "$tools/xpack-openocd-0.12.0-7/bin",
        "$env:LOCALAPPDATA/Microsoft/WinGet/Links"
)

$missing = @()
foreach ($dir in $required) {
        if (-not (Test-Path $dir)) { $missing += $dir; continue }
        $native = (Resolve-Path $dir).Path
        # prepend only if absent, so repeated dot-sourcing does not grow PATH without bound
        if (($env:PATH -split ';') -notcontains $native) {
                $env:PATH = "$native;$env:PATH"
        }
}

if ($missing.Count -gt 0) {
        Write-Warning "toolchain directories not found (builds needing them will fail):`n  $($missing -join "`n  ")"
}

if (-not $Quiet) {
        Write-Host "LIGHT_PATH        = $env:LIGHT_PATH"
        Write-Host "PICO_SDK_PATH     = $env:PICO_SDK_PATH"
        Write-Host "FONT_CRUSHER_PATH = $env:FONT_CRUSHER_PATH"
}
