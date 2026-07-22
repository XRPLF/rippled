include_guard()

set(GIT_BUILD_BRANCH "")
set(GIT_COMMIT_HASH "")

find_package(Git)
if(NOT Git_FOUND)
    message(WARNING "Git not found. Git branch and commit hash will be empty.")
    return()
endif()

set(GIT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/.git)

execute_process(
    COMMAND
        ${GIT_EXECUTABLE} --git-dir=${GIT_DIRECTORY} rev-parse --abbrev-ref HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_BUILD_BRANCH
)

execute_process(
    COMMAND ${GIT_EXECUTABLE} --git-dir=${GIT_DIRECTORY} rev-parse HEAD
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE GIT_COMMIT_HASH
)

message(STATUS "Git branch: ${GIT_BUILD_BRANCH}")
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
