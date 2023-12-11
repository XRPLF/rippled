#[===================================================================[
   coverage report target
#]===================================================================]

ProcessorCount(PROCESSOR_COUNT)

if (coverage)
  set(COVERAGE_EXCLUDES "/usr/include" "src/ed25519-donna" "src/secp256k1" "src/test")

  setup_target_for_coverage_gcovr_html(
      NAME coverage_report
      EXECUTABLE rippled -u --unittest-jobs ${PROCESSOR_COUNT} --quiet --unittest-log
      DEPENDENCIES rippled
  )
endif()
