# Runs `lake build XRPLModel:static` in CMake's own lake workspace. Invoked via
# `cmake -P` from the custom command in XrplLean4.cmake; expects LEAN4_LAKE,
# LEAN4_SRC (formal_verification/) and LEAN4_WORK (the workspace) on the
# command line.
#
# The workspace is a copy of LEAN4_SRC without its .lake, so this build never
# touches a developer's own formal_verification/.lake (built with the dev
# shell's lean) and never writes into the source tree. Everything but the
# workspace's .lake is replaced on each run so deleted sources disappear too;
# file(COPY) keeps timestamps, so unchanged modules stay up to date.
#
# LEAN_CC: leanc invokes it when set, so a system compiler would build the
# model's objects with a different toolchain than lean4-deps' were built with.
# It has to be *removed* rather than emptied: lake fails outright on an empty
# value ("external command '' exited with code 255"), which rules out
# `cmake -E env LEAN_CC=`. `cmake -E env --unset` needs CMake 3.24, above the
# project's 3.16 floor, so the unset happens here in script mode.
#
# LEAN_GITHASH: the dev shell sets it for nixpkgs' lean. It overrides the
# githash lake keys its traces on, so a value from another Lean build would make
# the prebuilt mathlib look stale.

unset(ENV{LEAN_CC})
unset(ENV{LEAN_GITHASH})

get_filename_component(LEAN4_SRC ${LEAN4_SRC} REALPATH)
file(MAKE_DIRECTORY ${LEAN4_WORK})
file(GLOB lean4_work_entries LIST_DIRECTORIES true "${LEAN4_WORK}/*")
list(FILTER lean4_work_entries EXCLUDE REGEX "/\\.lake$")
if(lean4_work_entries)
    file(REMOVE_RECURSE ${lean4_work_entries})
endif()
file(COPY ${LEAN4_SRC}/ DESTINATION ${LEAN4_WORK} PATTERN .lake EXCLUDE)

execute_process(
    COMMAND ${LEAN4_LAKE} build XRPLModel:static
    WORKING_DIRECTORY ${LEAN4_WORK}
    RESULT_VARIABLE lean4_model_result
)
if(NOT lean4_model_result EQUAL 0)
    message(
        FATAL_ERROR
        "formal_verification: Lean4 model build failed (${lean4_model_result})"
    )
endif()
