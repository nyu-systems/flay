if(ENABLE_TESTING)
  # Include the test subdirectory.
  message("-- Adding Flay v1model test suite.")
  include(test/P4Tests.cmake)
endif()

# Source files for the main flay.
set(FLAY_SOURCES
    ${FLAY_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/constants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/control_plane_objects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/expression_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/program_info.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stepper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/symbolic_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/target.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/table_executor.cpp
    PARENT_SCOPE
)

set(FLAY_GTEST_SOURCES
    ${FLAY_GTEST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/target_test.cpp
    PARENT_SCOPE
)
