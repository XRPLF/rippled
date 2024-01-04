#[===================================================================[
   coverage report target
#]===================================================================]

if (coverage)
  set(GCOVR_ADDITIONAL_ARGS ${coverage_extra_args})
  if (NOT GCOVR_ADDITIONAL_ARGS STREQUAL "")
    separate_arguments(GCOVR_ADDITIONAL_ARGS)
  endif()

  list (APPEND GCOVR_ADDITIONAL_ARGS
    --exclude-throw-branches
    --exclude-noncode-lines
    --exclude-unreachable-branches -s )

  setup_target_for_coverage_gcovr(
      NAME coverage_report
      FORMAT ${coverage_format}
      EXECUTABLE rippled
      EXECUTABLE_ARGS --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}> --unittest-jobs ${coverage_test_parallelism} --quiet --unittest-log
      EXCLUDE "src/test" "${CMAKE_BINARY_DIR}/proto_gen" "${CMAKE_BINARY_DIR}/proto_gen_grpc"
      DEPENDENCIES rippled
  )
endif()
