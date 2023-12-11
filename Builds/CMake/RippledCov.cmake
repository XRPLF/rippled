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

  set (GCOVR_ADDITIONAL_ARGS --exclude-noncode-lines --exclude-unreachable-branches -j ${PROCESSOR_COUNT} -s)

  setup_target_for_coverage_gcovr_html(
      NAME coverage_report
      EXECUTABLE rippled
      EXECUTABLE_ARGS -u --unittest-jobs ${TEST_PARALLELISM} --quiet --unittest-log
      EXCLUDE "src/test" "${CMAKE_BINARY_DIR}/proto_gen" "${CMAKE_BINARY_DIR}/proto_gen_grpc"
      DEPENDENCIES rippled
  )
endif()
