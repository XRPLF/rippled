#[===================================================================[
   coverage report target
#]===================================================================]

ProcessorCount(PROCESSOR_COUNT)

if (coverage)
  if (NOT GENHTML_PATH)
    message(FATAL_ERROR "Cannot find genhtml, aborting ...")
  endif()

  math(EXPR TEST_PARALLELISM "(${PROCESSOR_COUNT} + 1)/ 2")
  set(COVERAGE_EXCLUDES "/usr/include" "src/ed25519-donna" "src/secp256k1" "src/test")

  if (NOT FASTCOV_PATH)
    if (NOT GCOVR_PATH)
      if (NOT LCOV_PATH)
        message(FATAL_ERROR "Cannot find either of fastco, gcovr or lcov, aborting ...")
      else()
        setup_target_for_coverage_lcov(
          NAME coverage_report
          EXECUTABLE rippled -u --unittest-jobs ${TEST_PARALLELISM} --quiet --unittest-log
          DEPENDENCIES rippled
          )
      endif()
    else()
      setup_target_for_coverage_gcovr_html(
          NAME coverage_report
          EXECUTABLE rippled -u --unittest-jobs ${TEST_PARALLELISM} --quiet --unittest-log
          DEPENDENCIES rippled
      )
    endif()
  else()
    setup_target_for_coverage_fastcov(
          NAME coverage_report
          EXECUTABLE rippled -u --unittest-jobs ${TEST_PARALLELISM} --quiet --unittest-log
          DEPENDENCIES rippled
    )
  endif()
endif()
