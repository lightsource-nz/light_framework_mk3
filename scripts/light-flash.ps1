# Builds and flashes an RP2 target over USB, without touching the board.
#
# WHY THIS EXISTS: this loop was run by hand dozens of times in a single session, and it has
# four ways of failing quietly. Each is handled below and each cost real debugging time before
# it was understood:
#
#   1. matching the USB VID alone selects a CMSIS-DAP probe instead of the board, so the reset
#      goes to the wrong device and nothing happens -- which looks exactly like the board
#      ignoring it. Match the PID too.
#   2. SerialPort.Open() at 1200 baud THROWS while successfully triggering the reset. Treating
#      that exception as failure aborts a flash that was working.
#   3. Get-Volume can return a volume object with no DriveLetter. Building a destination path
#      from it yields nonsense, Copy-Item fails, and a script that reports success anyway will
#      happily leave you testing a stale image. This is not hypothetical -- it happened, and the
#      wrong firmware then looked like a regression in the code under test.
#   4. once a board has panicked or halted, the 1200-baud reset is gone with it: that route is
#      served by the firmware's own USB stack. Only the physical BOOT button can recover it, and
#      saying so plainly beats retrying.
#
# STM32 targets are not handled -- those flash over SWD from a .bin/.hex and have no UF2 path.
#
# USAGE:  light-flash.ps1 -Target <name> [-Preset <name>] [-NoBuild] [-TimeoutSeconds 15]
param(
        [Parameter(Mandatory)] [string]$Target,
        [string]$Preset,
        [switch]$NoBuild,
        [int]$TimeoutSeconds = 15,
        [string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'lib/LightProject.psm1') -Force

$config = Get-LightProjectConfig -ProjectRoot ($ProjectRoot ? $ProjectRoot : (Get-LightProjectRoot))
if (-not $Preset) { $Preset = Resolve-LightTargetPreset -Config $config -Target $Target }
$tree = Resolve-LightTree -Config $config -Preset $Preset

if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot 'light-build.ps1') -Target $Target -Preset $Preset -ProjectRoot $config.Root
}

$uf2 = Join-Path $tree "module/$Target/$Target.uf2"
if (-not (Test-Path $uf2)) {
        throw "no UF2 at '$uf2' -- is '$Target' an RP2 target? (STM32 targets flash over SWD and are not supported here)"
}

function Get-BootselVolume {
        # a volume with no drive letter cannot be written to; treat it as absent rather than
        # composing a path like 'C:\...\:\' out of it
        return Get-Volume | Where-Object { $_.FileSystemLabel -like 'RP*' -and $_.DriveLetter }
}

function Wait-For {
        param([scriptblock]$Condition, [int]$Seconds, [string]$What)

        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
                $result = & $Condition
                if ($result) { return $result }
                Start-Sleep -Milliseconds 250
        }
        return $null
}

$volume = Get-BootselVolume
if ($volume) {
        Write-Host "board already in BOOTSEL at $($volume.DriveLetter):"
} else {
        # PID_0009 is pico-sdk's CDC stdio interface. PID_000C is a CMSIS-DAP probe, which shares
        # the VID and will happily accept a reset that then does nothing useful.
        $port = Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -like '*VID_2E8A&PID_0009*' }
        if (-not $port) {
                throw "no board found: no CDC port with VID_2E8A&PID_0009, and no BOOTSEL volume. Is it connected? If it has panicked or halted, its USB stack is gone -- hold BOOT and re-plug."
        }

        Write-Host "resetting $($port.DeviceID) into BOOTSEL"
        $sp = New-Object System.IO.Ports.SerialPort $port.DeviceID, 1200, None, 8, one
        try { $sp.Open(); $sp.Close() } catch {
                # expected: "A device which does not exist was specified". The reset still happened.
        }

        $volume = Wait-For -Seconds $TimeoutSeconds -Condition { Get-BootselVolume }
        if (-not $volume) {
                throw "board did not enter BOOTSEL within ${TimeoutSeconds}s. If it is halted (a panic, or a build that faulted early) the 1200-baud reset is served by firmware that is no longer running -- hold the BOOT button and re-plug it, then re-run with -NoBuild."
        }
}

$dest = "$($volume.DriveLetter):\"
Write-Host "copying $(Split-Path $uf2 -Leaf) -> $dest"
Copy-Item $uf2 -Destination $dest -Force -ErrorAction Stop

# the volume disappearing IS the reboot; if it never does, the image was not accepted
$gone = Wait-For -Seconds $TimeoutSeconds -Condition { if (-not (Get-BootselVolume)) { $true } }
if (-not $gone) {
        Write-Warning "BOOTSEL volume still present after ${TimeoutSeconds}s -- the board may not have rebooted"
} else {
        Write-Host "flashed $Target"
}
