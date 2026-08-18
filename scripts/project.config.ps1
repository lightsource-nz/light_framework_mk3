# Per-project defaults for the framework's own build. The shared scripts live in this same
# directory, so there are no wrapper scripts here -- run scripts/light-*.ps1 directly.
#
# The framework's tests only exist on HOST builds: CMakeLists.txt gates the whole test/ subtree
# on `if(LIGHT_PLATFORM STREQUAL HOST)`, so a target build produces no test binaries at all.
@{
        Name = 'light_framework_mk3'

        Trees = @{
                'conf-demo-cli-debug'          = 'build'
                'conf-demo-cli-trace'          = 'build'
                'conf-demo-cli-release'        = 'build'
                # hand-configured, no preset produces it: a PICO_SDK build running in host mode
                'conf-light-pico-host'         = 'build-pico-hostmode'
                'conf-demo-blackpill-debug'    = 'build-blackpill'
                'conf-demo-mini-stm32h7-debug' = 'build-mini-stm32h7'
                'conf-demo-bluepill-plus-debug' = 'build-bluepill-plus'
        }

        Expect = @{
                'conf-demo-cli-debug'          = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'HOST_OS' }
                'conf-light-pico-host'         = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'PICO_SDK' }
                'conf-demo-blackpill-debug'    = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_SYSTEM = 'CMSIS' }
                'conf-demo-mini-stm32h7-debug' = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_SYSTEM = 'CMSIS' }
                'conf-demo-bluepill-plus-debug' = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_SYSTEM = 'CMSIS' }
        }

        Targets = @{
                'demo_cli'          = @{ Preset = 'conf-demo-cli-debug' }
                'demo_blackpill'    = @{ Preset = 'conf-demo-blackpill-debug' }
                'demo_mini_stm32h7' = @{ Preset = 'conf-demo-mini-stm32h7-debug' }
                'demo_bluepill_plus' = @{ Preset = 'conf-demo-bluepill-plus-debug' }
        }

        DefaultTarget = 'demo_cli'

        #   which OpenOCD config belongs to each preset, for scripts/light-debug.ps1 -- see
        # crossfire's project.config.ps1 for the fuller version of this pattern. No Svd: none is
        # vendored for the F1 either, same as the mini_stm32h7 entry would have if it had one.
        Debug = @{
                'conf-demo-bluepill-plus-debug' = @{
                        Config = 'openocd-bluepill-plus.cfg'
                }
        }

        Test = @{
                Preset = 'conf-demo-cli-debug'
                Ctest  = $true
        }

        #   Objects names every instrumented binary the suite runs: llvm-cov reads the coverage
        # map out of the objects themselves, so one left out does not error -- it silently
        # disappears from the report and understates the result
        Coverage = @{
                Objects     = 'test/*/*'
                IgnoreRegex = '(/lib/|/usr/|sanitizers/|_deps/)'
        }
}
