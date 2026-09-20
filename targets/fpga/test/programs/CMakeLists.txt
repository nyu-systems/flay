# ##################################################################################################
# GITHUB REPOSITORIES
# ##################################################################################################

set (FETCHCONTENT_BASE_DIR_PREV ${FETCHCONTENT_BASE_DIR})
set (FETCHCONTENT_BASE_DIR ${P4C_BINARY_DIR}/fpga_external_examples)

# Effectively emulates fetchcontent_makeavailable but does not add the module to install.
macro(fetchcontent_download_without_build content)
  FetchContent_GetProperties(${content})
  if(NOT ${content}_POPULATED)
    FetchContent_Populate(${content})
  endif()
endmacro(fetchcontent_download_without_build)

FetchContent_Declare(
  esnet
  GIT_REPOSITORY https://github.com/esnet/esnet-smartnic-hw
  # Keep the inputs stable alongside their checked-in reference outputs.
  GIT_TAG 0b38574673e2592c354a75b9a9f37dc9c25e3359
)

fetchcontent_download_without_build(esnet)

set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_PREV})

# ##################################################################################################
# TEST PROGRAMS
# ##################################################################################################

set(XSA_EXTERNAL_TESTS
  ${esnet_SOURCE_DIR}/examples/p4_only/p4/p4_only.p4
  ${esnet_SOURCE_DIR}/examples/p4_with_extern/p4/p4_with_extern.p4
  ${esnet_SOURCE_DIR}/examples/p4_multi_proc/p4/p4_multi_proc_igr.p4
  ${esnet_SOURCE_DIR}/examples/p4_multi_proc/p4/p4_multi_proc_egr.p4
  ${esnet_SOURCE_DIR}/examples/p4_and_verilog/p4/p4_and_verilog.p4
)

set (EXTRA_OPTS "-I${CMAKE_CURRENT_LIST_DIR}/../p4include")
set (EXTRA_OPTS "${EXTRA_OPTS} --reference-folder ${CMAKE_CURRENT_LIST_DIR}/../testdata")

p4tools_add_tests(
  TESTS
  "${XSA_EXTERNAL_TESTS}"
  TAG
  "flay-fpga-xsa-external"
  DRIVER
  ${FLAY_REFERENCE_DRIVER}
  TARGET
  "fpga"
  ARCH
  "xsa"
  TEST_ARGS
  "${EXTRA_OPTS}"
)

# Wildcard failing tests for now.
# TODO:  Classify further.
p4tools_add_xfail_reason(
  "flay-fpga-xsa-external"
  ".*"
)
