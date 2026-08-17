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
        }

        Expect = @{
                'conf-demo-cli-debug'          = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'HOST_OS' }
                'conf-light-pico-host'         = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'PICO_SDK' }
                'conf-demo-blackpill-debug'    = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_SYSTEM = 'CMSIS' }
                'conf-demo-mini-stm32h7-debug' = @{ LIGHT_PLATFORM = 'TARGET'; LIGHT_SYSTEM = 'CMSIS' }
        }

        Targets = @{
                'demo_cli'          = @{ Preset = 'conf-demo-cli-debug' }
                'demo_blackpill'    = @{ Preset = 'conf-demo-blackpill-debug' }
                'demo_mini_stm32h7' = @{ Preset = 'conf-demo-mini-stm32h7-debug' }
        }

        DefaultTarget = 'demo_cli'

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
