# Per-project defaults for the framework's own build. The shared scripts live in this same
# directory, so there are no wrapper scripts here -- run scripts/light-*.ps1 directly.
#
# The framework's tests only exist on HOST builds: CMakeLists.txt gates the whole test/ subtree
# on `if(LIGHT_PLATFORM STREQUAL HOST)`, so a target build produces no test binaries at all.
@{
        Name = 'light_framework_mk3'

        Trees = @{
                'conf-demo-cli-debug'   = 'build'
                'conf-demo-cli-trace'   = 'build'
                'conf-demo-cli-release' = 'build'
                # hand-configured, no preset produces it: a PICO_SDK build running in host mode
                'conf-light-pico-host'  = 'build-pico-hostmode'
        }

        Expect = @{
                'conf-demo-cli-debug'  = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'HOST_OS' }
                'conf-light-pico-host' = @{ LIGHT_PLATFORM = 'HOST'; LIGHT_SYSTEM = 'PICO_SDK' }
        }

        Targets = @{
                'demo_cli' = @{ Preset = 'conf-demo-cli-debug' }
        }

        DefaultTarget = 'demo_cli'

        Test = @{
                Preset = 'conf-demo-cli-debug'
                Ctest  = $true
        }
}
