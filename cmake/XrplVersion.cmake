find_package(Git)

set(GIT_BUILD_BRANCH "")
set(GIT_COMMIT_HASH "")
set(git_exact_tag "")

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

    # A detached checkout of a tag names the tag, as a CI tag build does
    # through GITHUB_BRANCH_NAME, so a rebuild embeds the same branch.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --exact-match --tags HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
        OUTPUT_VARIABLE git_exact_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_describe_result
    )
    if(NOT git_describe_result EQUAL 0)
        set(git_exact_tag "")
    elseif(GIT_BUILD_BRANCH STREQUAL "HEAD")
        set(GIT_BUILD_BRANCH "${git_exact_tag}")
    endif()
endif()

message(STATUS "Git branch: ${GIT_BUILD_BRANCH}")
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")

# The rules beast::SemanticVersion::parse applies, so that an invalid version
# fails here rather than when xrpld starts. It reads each number as an int.
set(SEMVER_NUMBER "(0|[1-9][0-9]*)")
set(SEMVER_PRE_RELEASE "[A-Za-z1-9-][A-Za-z0-9-]*")
set(SEMVER_METADATA "[A-Za-z0-9-]+")
set(SEMVER_NUMBER_MAX 2147483647)
set(SEMVER_REGEX
    "^${SEMVER_NUMBER}\\.${SEMVER_NUMBER}\\.${SEMVER_NUMBER}(-${SEMVER_PRE_RELEASE}(\\.${SEMVER_PRE_RELEASE})*)?(\\+${SEMVER_METADATA}(\\.${SEMVER_METADATA})*)?$"
)

if(
    DEFINED ENV{FORCE_XRPLD_VERSION}
    AND NOT "$ENV{FORCE_XRPLD_VERSION}" STREQUAL ""
)
    message(
        STATUS
        "Using explicitly provided '$ENV{FORCE_XRPLD_VERSION}' as xrpld version"
    )
    set(forced_version "$ENV{FORCE_XRPLD_VERSION}")
elseif(git_exact_tag MATCHES "${SEMVER_REGEX}")
    # CI sets FORCE_XRPLD_VERSION to the tag on a tag build; a plain checkout of
    # the same tag gets the same version.
    message(STATUS "Using exact tag '${git_exact_tag}' as xrpld version")
    set(forced_version "${git_exact_tag}")
endif()

if(DEFINED forced_version)
    set(XRPLD_VERSION "${forced_version}")

    if(
        NOT XRPLD_VERSION MATCHES "${SEMVER_REGEX}"
        OR CMAKE_MATCH_1 GREATER SEMVER_NUMBER_MAX
        OR CMAKE_MATCH_2 GREATER SEMVER_NUMBER_MAX
        OR CMAKE_MATCH_3 GREATER SEMVER_NUMBER_MAX
    )
        message(
            FATAL_ERROR
            "Version '${XRPLD_VERSION}' is not a semantic version xrpld accepts, see https://semver.org"
        )
    endif()
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
