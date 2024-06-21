option (validator_keys "Enables building of validator-keys-tool as a separate target (imported via FetchContent)" OFF)

if (validator_keys)
  git_branch (current_branch)
  # default to tracking VK master branch unless we are on release
  if (NOT (current_branch STREQUAL "release"))
    set (current_branch "master")
  endif ()
  message (STATUS "tracking ValidatorKeys branch: ${current_branch}")

  FetchContent_Declare (
    validator_keys_src
    GIT_REPOSITORY https://github.com/ripple/validator-keys-tool.git
    GIT_TAG        "${current_branch}"
  )
  FetchContent_GetProperties (validator_keys_src)
  if (NOT validator_keys_src_POPULATED)
    message (STATUS "Pausing to download ValidatorKeys...")
    FetchContent_Populate (validator_keys_src)
  endif ()
  add_subdirectory (${validator_keys_src_SOURCE_DIR} ${CMAKE_BINARY_DIR}/validator-keys)
endif ()
