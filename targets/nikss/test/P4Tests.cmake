# This file defines how to execute Flay on P4 programs. General test utilities.
include(${P4TOOLS_SOURCE_DIR}/cmake/TestUtils.cmake)
# This file defines how we write the tests we generate.
include(${CMAKE_CURRENT_LIST_DIR}/TestTemplate.cmake)

# Fetch the bpftool.
set(FETCHCONTENT_QUIET_PREV ${FETCHCONTENT_QUIET})
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
  bpftool
  URL https://github.com/libbpf/bpftool/releases/download/v7.4.0/bpftool-v7.4.0-amd64.tar.gz
  URL_HASH
    SHA256=68862a7038a269e1d4af5fed53d2b4d763f1cd82a96caa49b76596453bae0c3e
  USES_TERMINAL_DOWNLOAD TRUE
  GIT_PROGRESS TRUE
  PATCH_COMMAND chmod 775 ./bpftool
)
FetchContent_MakeAvailable(bpftool)

# FIXME: Remove once TLS verification is fixed.
set (CMAKE_TLS_VERIFY FALSE)
FetchContent_Declare(trex
  URL https://trex-tgn.cisco.com/trex/release/v3.05.tar.gz
  URL_HASH
    SHA256=15e9ac6da554b9bdbec575da5f1010e3d2b07c8806be889e074ae8a776636c0b
  USES_TERMINAL_DOWNLOAD TRUE
  PATCH_COMMAND
    patch -p0 -i ${CMAKE_CURRENT_SOURCE_DIR}/test/trex.patch || echo
    "Patch does not apply because the patch was already applied."
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(trex)
set (CMAKE_TLS_VERIFY TRUE)

# TODO: Remove this once the Nikss FetchContent initalization is fixed.
set (FETCHCONTENT_UPDATES_DISCONNECTED_NIKSS_CTL ON)
FetchContent_Declare(nikss_ctl
  GIT_REPOSITORY https://github.com/NIKSS-vSwitch/nikss
  GIT_PROGRESS TRUE
  BUILD_COMMAND ""
  PATCH_COMMAND ./build_libbpf.sh
)
FetchContent_MakeAvailable(nikss_ctl)

set(FETCHCONTENT_QUIET ${FETCHCONTENT_QUIET_PREV})



# ##################################################################################################
# TEST PROGRAMS
# ##################################################################################################
set(PSA_SEARCH_PATTERNS "include.*psa.p4" "main")

set(P4TESTS_FOR_FPGA
  # P4Studio tests
  "${P4C_SOURCE_DIR}/backends/ebpf/psa/examples/*.p4"
  # Custom tests
  "${P4C_SOURCE_DIR}/backends/ebpf/tests/p4testdata/*.p4"
)

p4c_find_tests("${P4TESTS_FOR_FPGA}" PSA_TESTS INCLUDE "${PSA_SEARCH_PATTERNS}" EXCLUDE "")

# Filter some programs  because they have issues that are not captured with Xfails.
list(REMOVE_ITEM PSA_TESTS
  # These tests time out and require fixing.
)

# TODO: These port defines are a bit of a hack, we really shouldn't need them.
set (EXTRA_OPTS "-I${CMAKE_CURRENT_LIST_DIR}/p4include -DPORT0=0 -DPORT1=1 -DPORT2=2 -DPORT3=3")
set (EXTRA_OPTS "${EXTRA_OPTS} -DPSA_RECIRC=50")
set (EXTRA_OPTS "${EXTRA_OPTS} --reference-folder ${CMAKE_CURRENT_LIST_DIR}/testdata")

p4tools_add_tests(
  TESTS
  "${PSA_TESTS}"
  TAG
  "flay-nikss-psa"
  DRIVER
  ${FLAY_REFERENCE_DRIVER}
  TARGET
  "nikss"
  ARCH
  "psa"
  TEST_ARGS
  "${EXTRA_OPTS}"
)

# Include the list of failing tests.
include(${CMAKE_CURRENT_LIST_DIR}/NikssPsaXfail.cmake)
