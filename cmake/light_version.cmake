# Derives the framework's version from git and makes it available to C as LIGHT_VERSION_STRING.
#
# WHY THIS EXISTS: until now every version string in this project was a hand-typed "0.1.0" in a
# header, and a binary in someone else's hands could not say which commit produced it. That is
# the first question worth asking when a field report arrives, and it had no answer.
#
# scripts/light-version.ps1 is the single source of truth; this file only calls it. Keeping the
# derivation in one place means the string a release is tagged with and the string compiled into
# the image cannot drift apart.

# --- configure-time value, used for messages and as the fallback -------------------------------

function(_light_query_version out_var)
        find_program(_light_pwsh NAMES pwsh powershell)
        if(NOT _light_pwsh)
                set(${out_var} "0.0.0-unknown" PARENT_SCOPE)
                return()
        endif()
        execute_process(
                COMMAND ${_light_pwsh} -NoProfile -File "${LIGHT_PATH}/scripts/light-version.ps1"
                        -Path "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.."
                OUTPUT_VARIABLE _version
                ERROR_VARIABLE _version_err
                RESULT_VARIABLE _version_rc
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        #   a build from a source archive has no .git, and must still build. Failing here would
        # make the version machinery a hard dependency of compiling at all, which is a much worse
        # trade than an honest "unknown"
        if(NOT _version_rc EQUAL 0 OR _version STREQUAL "")
                set(${out_var} "0.0.0-unknown" PARENT_SCOPE)
        else()
                set(${out_var} "${_version}" PARENT_SCOPE)
        endif()
endfunction()

_light_query_version(LIGHT_VERSION_STRING)
light_info("light framework version: ${LIGHT_VERSION_STRING}")

# --- build-time regeneration -------------------------------------------------------------------

set(_light_version_header "${CMAKE_BINARY_DIR}/light_generated/light_version.h")
set(_light_version_script "${LIGHT_PATH}/cmake/light_version_generate.cmake")

#   regenerated on every build, not only at configure time. A version baked at configure time
# goes stale the moment you commit, so the binary would claim a commit it was not built from --
# which defeats the entire purpose of having it. The generator writes via copy_if_different, so
# a relink only happens when the string actually changes.
add_custom_target(light_version_header ALL
        BYPRODUCTS "${_light_version_header}"
        COMMAND ${CMAKE_COMMAND}
                -DLIGHT_PATH=${LIGHT_PATH}
                -DLIGHT_VERSION_HEADER=${_light_version_header}
                -DLIGHT_VERSION_FALLBACK=${LIGHT_VERSION_STRING}
                -P "${_light_version_script}"
        COMMENT "checking light framework version"
        VERBATIM)

# generate it once now as well, so the first compile has a header to include even if the custom
# target has not run yet
execute_process(COMMAND ${CMAKE_COMMAND}
        -DLIGHT_PATH=${LIGHT_PATH}
        -DLIGHT_VERSION_HEADER=${_light_version_header}
        -DLIGHT_VERSION_FALLBACK=${LIGHT_VERSION_STRING}
        -P "${_light_version_script}")

set(LIGHT_VERSION_INCLUDE_DIR "${CMAKE_BINARY_DIR}/light_generated" CACHE INTERNAL "")
