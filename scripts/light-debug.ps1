# Starts an OpenOCD server, and optionally gdb, against a built target.
#
# WHY THIS EXISTS: the debug setup was reachable only through VS Code's cortex-debug extension,
# and its launch.json leaned on ${command:cmake.launchTargetPath} -- a variable only that
# extension can resolve. So there was no way to start a debug session from a shell, and no way
# for a script to do it either.
#
# It also encodes which OpenOCD config and SVD belong to which board, because getting that wrong
# is not a clean failure: attaching an rp2040 configuration to an rp2350 image produces confusing
# misbehaviour rather than an error, and screen-test's launch.json named the rp2040 SVD for every
# configuration including the RP2350 ones.
#
# NOT HARDWARE-VERIFIED. No CMSIS-DAP probe was connected when this was written, so the paths
# through to a live gdb session are untested; what is verified is that it resolves the right
# config, SVD and ELF, and refuses clearly when no probe is present.
#
# USAGE:  light-debug.ps1 -Target <name> [-Preset <name>] [-ServerOnly] [-Attach] [-NoBuild]
param(
        [Parameter(Mandatory)] [string]$Target,
        [string]$Preset,
        [switch]$ServerOnly,
        [switch]$Attach,
        [switch]$NoBuild,
        [string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/LightProject.psm1') -Force
. (Join-Path $PSScriptRoot 'light-env.ps1') -Quiet

$config = Get-LightProjectConfig -ProjectRoot ($ProjectRoot ? $ProjectRoot : (Get-LightProjectRoot))
if (-not $Preset) { $Preset = Resolve-LightTargetPreset -Config $config -Target $Target }
$tree = Resolve-LightTree -Config $config -Preset $Preset

if (-not $config.Debug -or -not $config.Debug.ContainsKey($Preset)) {
        throw "no Debug entry for preset '$Preset' in scripts/project.config.ps1 -- it must name the OpenOCD config and SVD for this board"
}
$debug = $config.Debug[$Preset]

if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'light-build.ps1') -Target $Target -Preset $Preset -ProjectRoot $config.Root
}

#   the executable is named differently per toolchain: pico_add_extra_outputs() gives the pico
# targets a .elf suffix, while the CMSIS targets are plain CMake executables and carry no suffix
# at all (their .bin and .hex are objcopy'd from this file by the post-build hook in cmsis.cmake)
$elf = @("module/$Target/$Target.elf", "module/$Target/$Target") |
        ForEach-Object { Join-Path $tree $_ } |
        Where-Object { Test-Path $_ -PathType Leaf } |
        Select-Object -First 1
if (-not $elf) {
        throw "no executable found for '$Target' in '$tree' (looked for $Target.elf and $Target) -- has it been built?"
}

$ocdConfig = Join-Path $config.Root $debug.Config
if (-not (Test-Path $ocdConfig)) { throw "no OpenOCD config at '$ocdConfig'" }

$openocd = $debug.Server ? $debug.Server : 'C:/Users/aful018/tools/xpack-openocd-0.12.0-7/bin/openocd.exe'
if (-not (Test-Path $openocd)) { throw "no openocd at '$openocd'" }

#   check a probe is actually present, so "openocd exits with a transport error" becomes
# something that names the real problem. Note VID_2E8A&PID_0009 is the RP2 board's own CDC
# interface and is NOT a debug port -- only PID_000C is a CMSIS-DAP probe. ST-Link is a
# different vendor entirely (VID_0483), which is what the STM32 board is debugged through.
$probes = Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.DeviceID -like '*VID_2E8A&PID_000C*' -or   # Raspberry Pi CMSIS-DAP / debugprobe
        $_.DeviceID -like '*VID_0483&PID_37*' -or      # ST-Link V2.1 / V3
        $_.DeviceID -like '*VID_1366*'                 # SEGGER J-Link
}
if ($probes) {
        # these enumerate as composite devices, so prefer a child whose name actually says what
        # it is over the generic "USB Composite Device" parent
        $named = $probes | Where-Object { $_.Name -notmatch 'Composite' } | Select-Object -First 1
        if (-not $named) { $named = $probes | Select-Object -First 1 }
        Write-Host "probe:   $($named.Name)"
} else {
        Write-Warning "no debug probe found (looked for CMSIS-DAP, ST-Link and J-Link). OpenOCD will fail to connect."
}

$searchDir = Split-Path (Split-Path $openocd -Parent) -Parent | Join-Path -ChildPath 'openocd/scripts'
$ocdArgs = @('-s', $searchDir, '-f', $ocdConfig)

Write-Host "openocd: $openocd"
Write-Host "config:  $ocdConfig"
Write-Host "elf:     $elf"
if ($debug.Svd) { Write-Host "svd:     $(Join-Path $config.Root $debug.Svd)" }

if ($ServerOnly) {
        Write-Host "`nstarting OpenOCD in the foreground -- attach a debugger to localhost:3333, Ctrl-C to stop"
        & $openocd @ocdArgs
        return
}

$server = Start-Process -FilePath $openocd -ArgumentList $ocdArgs -PassThru -NoNewWindow
try {
        Start-Sleep -Seconds 2
        if ($server.HasExited) { throw "OpenOCD exited immediately (code $($server.ExitCode)) -- is a probe connected and not already in use?" }

        $gdb = Join-Path $env:LIGHT_ARM_GDB_DIR 'arm-none-eabi-gdb.exe'
        if (-not (Test-Path $gdb)) {
                $gdb = 'C:/Users/aful018/tools/arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi/bin/arm-none-eabi-gdb.exe'
        }
        if (-not (Test-Path $gdb)) { throw "no arm-none-eabi-gdb found" }

        # -Attach leaves the image on the board alone, which is what you want when debugging
        # something already running; the default loads the ELF you just built
        $gdbArgs = @('-ex', 'target extended-remote localhost:3333')
        if (-not $Attach) {
                $gdbArgs += @('-ex', 'monitor reset init', '-ex', 'load')
        }
        $gdbArgs += $elf

        & $gdb @gdbArgs
} finally {
        if (-not $server.HasExited) {
                Write-Host "stopping OpenOCD"
                Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        }
}
