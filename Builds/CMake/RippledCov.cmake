#[===================================================================[
   coverage report target
#]===================================================================]

ProcessorCount(PROCESSOR_COUNT)

if (coverage)
  if (DEFINED CODE_COVERAGE_TEST_PARALLELISM)
    set(TEST_PARALLELISM ${CODE_COVERAGE_TEST_PARALLELISM})
  else()
    set(TEST_PARALLELISM ${PROCESSOR_COUNT})
  endif()

  if (DEFINED CODE_COVERAGE_REPORT_FORMAT)
    set(CODE_COVERAGE_FORMAT ${CODE_COVERAGE_REPORT_FORMAT})
  else()
    set(CODE_COVERAGE_FORMAT html-details)
  endif()

  set (GCOVR_ADDITIONAL_ARGS --exclude-throw-branches --exclude-noncode-lines --exclude-unreachable-branches -s)

  setup_target_for_coverage_gcovr(
      NAME coverage_report
      FORMAT ${CODE_COVERAGE_FORMAT}
      EXECUTABLE rippled
      EXECUTABLE_ARGS -u --unittest-jobs ${TEST_PARALLELISM} --quiet --unittest-log
      EXCLUDE "src/test" "${CMAKE_BINARY_DIR}/proto_gen" "${CMAKE_BINARY_DIR}/proto_gen_grpc"
      DEPENDENCIES rippled
  )
endif()
