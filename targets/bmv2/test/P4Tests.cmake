# This file defines how to execute Flay on P4 programs. General test utilities.
include(${P4TOOLS_SOURCE_DIR}/cmake/TestUtils.cmake)
# This file defines how we write the tests we generate.
include(${CMAKE_CURRENT_LIST_DIR}/TestTemplate.cmake)

# ##################################################################################################
# TEST PROGRAMS
# ##################################################################################################
set(V1_SEARCH_PATTERNS "include.*v1model.p4" "main|common_v1_test")
# General BMv2 tests supplied by the compiler.
set(FLAY_P4TESTS_FOR_BMV2
    "${P4C_SOURCE_DIR}/testdata/p4_16_samples/*.p4"
    "${P4C_SOURCE_DIR}/testdata/p4_16_samples/dash/*.p4"
    "${P4C_SOURCE_DIR}/testdata/p4_16_samples/fabric_*/fabric.p4"
    "${P4C_SOURCE_DIR}/testdata/p4_16_samples/omec/*.p4"
    "${P4C_SOURCE_DIR}/testdata/p4_16_samples/pins/*.p4"
    # Custom tests
    "${CMAKE_CURRENT_LIST_DIR}/programs/*.p4"
)

p4c_find_tests("${FLAY_P4TESTS_FOR_BMV2}" FLAY_P4_16_BMV2_V1_TESTS INCLUDE "${V1_SEARCH_PATTERNS}" EXCLUDE "")

# Filter some programs  because they have issues that are not captured with Xfails.
list(
  REMOVE_ITEM
  FLAY_P4_16_BMV2_V1_TESTS
  # These tests time out and require fixing.
)

set (EXTRA_OPTS "--reference-folder ${CMAKE_CURRENT_LIST_DIR}/testdata")

p4tools_add_tests(
  TESTS
  "${FLAY_P4_16_BMV2_V1_TESTS}"
  TAG
  "flay-bmv2-v1model"
  DRIVER
  ${FLAY_REFERENCE_DRIVER}
  TARGET
  "bmv2"
  ARCH
  "v1model"
  TEST_ARGS
  "${EXTRA_OPTS}"
)

# TODO: Re-enable after full-parser analysis fits within the test timeout.
# The parser-skipping run also exposes a BMv2 checksum-conversion crash.
if(TEST "flay-bmv2-v1model/switch_p4_16.p4")
  set_tests_properties("flay-bmv2-v1model/switch_p4_16.p4" PROPERTIES DISABLED TRUE)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/ConfigTests.cmake)

# Include the list of failing tests.
include(${CMAKE_CURRENT_LIST_DIR}/BMv2V1ModelXfail.cmake)
