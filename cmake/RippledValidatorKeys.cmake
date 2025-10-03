option (validator_keys "Enables building of validator-keys tool as a separate target (imported via FetchContent)" OFF)

if (validator_keys)
  git_branch (current_branch)
  # default to tracking VK master branch unless we are on release
  if (NOT (current_branch STREQUAL "release"))
    set (current_branch "master")
  endif ()
  message (STATUS "Tracking ValidatorKeys branch: ${current_branch}")

  FetchContent_Declare (
    validator_keys
    GIT_REPOSITORY https://github.com/ripple/validator-keys-tool.git
    GIT_TAG        "${current_branch}"
  )
  FetchContent_MakeAvailable(validator_keys)
  install(TARGETS validator-keys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

endif ()
