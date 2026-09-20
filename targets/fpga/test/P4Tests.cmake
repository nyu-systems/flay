# This file defines how to execute Flay on P4 programs. General test utilities.
include(${P4TOOLS_SOURCE_DIR}/cmake/TestUtils.cmake)
# This file defines how we write the tests we generate.
include(${CMAKE_CURRENT_LIST_DIR}/TestTemplate.cmake)

# ##################################################################################################
# TEST PROGRAMS
# ##################################################################################################
set(XSA_SEARCH_PATTERNS "include.*xsa.p4" "main")

set(P4TESTS_FOR_FPGA
  # P4Studio tests
  "${CMAKE_CURRENT_LIST_DIR}/programs/esnet/**/*.p4"
  # Custom tests
  "${CMAKE_CURRENT_LIST_DIR}/programs/*.p4"
)

p4c_find_tests("${P4TESTS_FOR_FPGA}" XSA_TESTS INCLUDE "${XSA_SEARCH_PATTERNS}" EXCLUDE "")

# Filter some programs  because they have issues that are not captured with Xfails.
list(REMOVE_ITEM XSA_TESTS
  # These tests time out and require fixing.
)

set (EXTRA_OPTS "-I${CMAKE_CURRENT_LIST_DIR}/p4include ")
set (EXTRA_OPTS "${EXTRA_OPTS} --reference-folder ${CMAKE_CURRENT_LIST_DIR}/testdata")

p4tools_add_tests(
  TESTS
  "${XSA_TESTS}"
  TAG
  "flay-fpga-xsa"
  DRIVER
  ${FLAY_REFERENCE_DRIVER}
  TARGET
  "fpga"
  ARCH
  "xsa"
  TEST_ARGS
  "${EXTRA_OPTS}"
)

# TODO: Re-enable after full-parser analysis fits within the test timeout.
if(TEST "flay-fpga-xsa/consolidated_switch_xsa.p4")
  set_tests_properties("flay-fpga-xsa/consolidated_switch_xsa.p4" PROPERTIES DISABLED TRUE)
endif()

# Include the list of failing tests.
include(${CMAKE_CURRENT_LIST_DIR}/XsaXfail.cmake)

# Research projects using the XSA architecture.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/programs)
