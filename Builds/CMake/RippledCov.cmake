#[===================================================================[
   coverage report target
#]===================================================================]

if(NOT coverage)
  message(FATAL_ERROR "Code coverage not enabled! Aborting ...")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  message(WARNING "Code coverage on Windows is not supported, ignoring 'coverage' flag")
  return()
endif()

include(CodeCoverage)

# The instructions for these commands come from the `CodeCoverage` module,
# which was copied from https://github.com/bilke/cmake-modules, commit fb7d2a3,
# then locally changed (see CHANGES: section in `CodeCoverage.cmake`)

set(GCOVR_ADDITIONAL_ARGS ${coverage_extra_args})
if(NOT GCOVR_ADDITIONAL_ARGS STREQUAL "")
  separate_arguments(GCOVR_ADDITIONAL_ARGS)
endif()

list(APPEND GCOVR_ADDITIONAL_ARGS
  --exclude-throw-branches
  --exclude-noncode-lines
  --exclude-unreachable-branches -s
  -j ${coverage_test_parallelism})

setup_target_for_coverage_gcovr(
  NAME coverage
  FORMAT ${coverage_format}
  EXECUTABLE rippled
  EXECUTABLE_ARGS --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}> --unittest-jobs ${coverage_test_parallelism} --quiet --unittest-log
  EXCLUDE "src/test" "${CMAKE_BINARY_DIR}/proto_gen" "${CMAKE_BINARY_DIR}/proto_gen_grpc"
  DEPENDENCIES rippled
)
