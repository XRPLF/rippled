find_package(Git)

set(GIT_BUILD_BRANCH "")
set(GIT_COMMIT_HASH "")

if(DEFINED ENV{GITHUB_BRANCH_NAME})
    set(GIT_BUILD_BRANCH $ENV{GITHUB_BRANCH_NAME})
    set(GIT_COMMIT_HASH $ENV{GITHUB_HEAD_SHA})
elseif(Git_FOUND AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
        OUTPUT_VARIABLE GIT_BUILD_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

message(STATUS "Git branch: ${GIT_BUILD_BRANCH}")
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")

if(
    DEFINED ENV{FORCE_XRPLD_VERSION}
    AND NOT "$ENV{FORCE_XRPLD_VERSION}" STREQUAL ""
)
    message(
        STATUS
        "Using explicitly provided '$ENV{FORCE_XRPLD_VERSION}' as xrpld version"
    )

    set(XRPLD_VERSION "$ENV{FORCE_XRPLD_VERSION}")
else()
    message(STATUS "Using '0.0.0-dev+<git short rev>' as xrpld version")

    if(GIT_COMMIT_HASH STREQUAL "")
        message(
            FATAL_ERROR
            "Unable to determine xrpld version without git, set FORCE_XRPLD_VERSION"
        )
    endif()

    string(SUBSTRING ${GIT_COMMIT_HASH} 0 7 GIT_COMMIT_HASH_SHORT)

    set(XRPLD_VERSION "0.0.0-dev+${GIT_COMMIT_HASH_SHORT}")
endif()

message(STATUS "Build version: ${XRPLD_VERSION}")
