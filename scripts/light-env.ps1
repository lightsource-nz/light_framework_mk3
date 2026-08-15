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
Import-Module (Join-Path $PSScriptRoot 'lib/LightPlatform.psm1') -Force

#   every absolute path below used to be hardcoded to one developer's Windows home directory,
# which no second machine could satisfy and no Linux machine could even parse. They are now
# derived: from this script's location where that is possible, and from an overridable root
# otherwise. LIGHT_TOOLS_PATH names where third-party toolchains are unpacked
$tools = Get-LightToolRoot

#   LIGHT_PATH is the anchor for everything else -- CMake reads it from the environment
# (screen-test/CMakeLists.txt) and light_preinit.cmake resolves the framework against it.
# Derived from this script's own location so a moved or cloned checkout still works.
$frameworkRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $env:LIGHT_PATH) { $env:LIGHT_PATH = $frameworkRoot }

#   the sibling checkouts. Derived from the framework's own parent rather than named absolutely:
# all of these live beside light_framework_mk3 in a normal checkout, on any platform
$siblings = Split-Path $frameworkRoot -Parent

#   PICO_SDK_PATH MUST be set before the FIRST configure of any pico tree. The pico presets pass
# it through as $env{PICO_SDK_PATH}, and when it is empty pico_sdk_init() never runs. That fails
# late and misleadingly -- as "Unknown CMake command pico_enable_stdio_semihosting" -- and worse,
# the failed configure caches the HOST compiler, so a later correct configure still builds
# boot_stage2 with w64devkit's cc.exe. The only recovery is deleting the build tree.
if (-not $env:PICO_SDK_PATH) { $env:PICO_SDK_PATH = (Join-Path $siblings 'pico-sdk') }

#   without this, rend's own Findcrush.cmake defaults FONT_CRUSHER_PATH relative to ITSELF,
# misses the sibling checkout, and silently FetchContent-fetches font-crusher from GitHub --
# so local crush edits have no effect at all. screen-test works around it in CMake; crossfire
# does not, and every one of its build trees is currently building against a fetched copy.
if (-not $env:FONT_CRUSHER_PATH) { $env:FONT_CRUSHER_PATH = (Join-Path $siblings 'font-crusher') }

#   the toolchain directories to prepend, which are genuinely different per platform rather
# than the same thing spelled two ways.
#
#   on WINDOWS these are unpacked distributions under the tools root: w64devkit supplies the
# host gcc/cmake/ninja and must come FIRST, before anything else that ships a cmake.
#   on LINUX the host toolchain, cmake, ninja and openocd are normally distro packages already
# on PATH, so the list is near-empty by design -- an ARM toolchain unpacked under the tools
# root is still honoured if it is there, and nothing is warned about if it is not.
#
#   the RISC-V toolchain is deliberately ABSENT on both: it reaches the build through
# PICO_TOOLCHAIN_PATH in the riscv preset, and putting it on PATH breaks the ARM trees.
if ($IsWindows) {
        $required = @(
                "$tools/w64devkit/bin",
                "$tools/arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi/bin",
                "$tools/xpack-openocd-0.12.0-7/bin",
                "$env:LOCALAPPDATA/Microsoft/WinGet/Links"
        )
        $optional = @()
} else {
        $required = @()
        # honoured when present, silent when not -- a packaged arm-none-eabi-gcc is the norm
        $optional = @(
                "$tools/arm-gnu-toolchain/bin",
                "$tools/xpack-openocd/bin"
        )
}

$sep = Get-LightPathSeparator
$missing = @()
foreach ($dir in ($required + $optional)) {
        if (-not (Test-Path $dir)) {
                # only the required set is worth complaining about
                if ($required -contains $dir) { $missing += $dir }
                continue
        }
        $native = (Resolve-Path $dir).Path
        # prepend only if absent, so repeated dot-sourcing does not grow PATH without bound.
        # splitting on a hardcoded ';' would never match on Linux and would do exactly that
        if (($env:PATH -split [regex]::Escape($sep)) -notcontains $native) {
                $env:PATH = "$native$sep$env:PATH"
        }
}

if ($missing.Count -gt 0) {
        Write-Warning "toolchain directories not found (builds needing them will fail):`n  $($missing -join "`n  ")"
}

#   a build needs these three regardless of platform, and finding them missing here is a far
# better message than cmake's. Not fatal: a HOST-only build of one project may legitimately
# not have an ARM toolchain installed
foreach ($t in @('cmake', 'ninja')) {
        if (-not (Find-LightTool -Name $t)) {
                Write-Warning "'$t' is not on PATH -- configure and build will fail"
        }
}

if (-not $Quiet) {
        Write-Host "platform          = $(Get-LightPlatform)"
        Write-Host "LIGHT_PATH        = $env:LIGHT_PATH"
        Write-Host "LIGHT_TOOLS_PATH  = $tools"
        Write-Host "PICO_SDK_PATH     = $env:PICO_SDK_PATH"
        Write-Host "FONT_CRUSHER_PATH = $env:FONT_CRUSHER_PATH"
}
