# CMake file for P4Flay.
project(flay)

# Add cmake directory to our module path. Also add cmake directory from P4C
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(P4TOOLS_FLAY_WITH_GRPC "Build with gRPC support" OFF)

# Declare common P4Flay variables.
set(FLAY_DIR ${P4C_BINARY_DIR}/flay)
set(FLAY_DRIVER "${CMAKE_CURRENT_BINARY_DIR}/p4flay")
set(FLAY_REFERENCE_DRIVER "${flay_BINARY_DIR}/tools/flay_reference_checker")


# ############### Protobuf generation
set(FLAY_CONTROL_PLANE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core/control_plane)
set(FLAY_P4RUNTIME_DIR ${FLAY_CONTROL_PLANE_DIR}/p4runtime)
set(FLAY_P4RUNTIME_TESTS_PROTO ${FLAY_P4RUNTIME_DIR}/flaytests.proto)
set(FLAY_BFRUNTIME_DIR ${FLAY_CONTROL_PLANE_DIR}/bfruntime)
set(FLAY_BFRUNTIME_TESTS_PROTO ${FLAY_BFRUNTIME_DIR}/flaytests.proto)


# The core of the tool.
add_subdirectory(core)

if(P4TOOLS_FLAY_WITH_GRPC)
  include(GRpc)
  p4c_obtain_grpc()
  add_subdirectory(grpc_service)
endif()

set(FLAY_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/flay.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/options.cpp
)

# GTest source files for P4Flay.
set(FLAY_GTEST_SOURCES
  ${P4C_SOURCE_DIR}/test/gtest/helpers.cpp
  ${P4C_SOURCE_DIR}/test/gtest/gtestp4c.cpp
  ${CMAKE_CURRENT_LIST_DIR}/test/core/execution_state_test.cpp
  ${CMAKE_CURRENT_LIST_DIR}/test/core/simplify_expression_test.cpp
  ${CMAKE_CURRENT_LIST_DIR}/test/core/structural_compare_test.cpp
)

# Flay libraries.
set(FLAY_LIBS
    PRIVATE p4tools-control-plane p4tools-common flay-runtime p4tools-runtime-proto controlplane
    flay-lib flay-control-plane flay-interpreter flay-specialization ${LIBGC_LIBRARIES}
)

file(GLOB flay_targets RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/targets
     ${CMAKE_CURRENT_SOURCE_DIR}/targets/*
)
foreach(ext ${flay_targets})
  set(flay_targets_dir ${CMAKE_CURRENT_SOURCE_DIR}/targets/${ext}/)
  if(EXISTS ${flay_targets_dir}/CMakeLists.txt AND IS_DIRECTORY ${flay_targets_dir})
    # Generate an option that makes it possible to disable this extension.
    string(MAKE_C_IDENTIFIER ${ext} EXT_AS_IDENTIFIER)
    string(TOUPPER ${EXT_AS_IDENTIFIER} EXT_AS_OPTION_NAME)
    string(CONCAT ENABLE_EXT_OPTION "ENABLE_TOOLS_TARGET_" ${EXT_AS_OPTION_NAME})
    string(CONCAT EXT_HELP_TEXT "Build the " ${ext} " target")
    option(${ENABLE_EXT_OPTION} ${EXT_HELP_TEXT} ON)
    if(${ENABLE_EXT_OPTION})
      message("-- Enabling target ${ext}")
      add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/targets/${ext})
      set(include_statements_var
          "${include_statements_var}#include \"backends/p4tools/modules/flay/targets/${ext}/register.h\"\n"
      )
      set(flay_targets_var "${flay_targets_var}    ${ext}_registerFlayTarget();\n")
    endif()
  endif()
endforeach(ext)

# Propagate def files set by target extensions upwards.
set(IR_DEF_FILES ${IR_DEF_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/flay.def PARENT_SCOPE)

# Convert the list of files into #includes
foreach(include_file ${include_files})

endforeach()

# Fill the template
configure_file(register.h.in register.h)


add_library(flay STATIC ${FLAY_SOURCES})
if(P4TOOLS_FLAY_WITH_GRPC)
  target_link_libraries(flay PUBLIC flay-grpc flay-grpc-service)
endif()
target_link_libraries(flay ${FLAY_LIBS})

add_p4tools_executable(p4flay main.cpp)

target_link_libraries(
  p4flay PRIVATE flay ${FLAY_LIBS} ${P4C_LIBRARIES} ${P4C_LIB_DEPS}
)

# Add some convenience links for invoking flay.
add_custom_target(
  linkflay
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_BINARY_DIR}/p4flay
          ${P4C_BINARY_DIR}/p4flay
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${P4C_BINARY_DIR}/p4include
          ${CMAKE_CURRENT_BINARY_DIR}/p4include
  DEPENDS update_includes
)

add_dependencies(p4flay linkflay)

# Utilities for testing.
add_subdirectory(tools)

if(ENABLE_GTESTS)
  add_executable(flay-gtest ${FLAY_GTEST_SOURCES})
  target_link_libraries(
    flay-gtest PRIVATE flay PRIVATE gtest ${FLAY_LIBS} ${P4C_LIBRARIES} ${P4C_LIB_DEPS}
  )

  if(ENABLE_TESTING)
    add_test(NAME flay-gtest COMMAND flay-gtest)
    set_tests_properties(flay-gtest PROPERTIES LABELS "gtest-flay")
  endif()

endif()

# Create some symlinks to configurations of static analysis tools.
file(CREATE_LINK ${P4C_SOURCE_DIR}/pyproject.toml ${flay_SOURCE_DIR}/pyproject.toml SYMBOLIC)
file(CREATE_LINK ${P4C_SOURCE_DIR}/.clang-tidy ${flay_SOURCE_DIR}/.clang-tidy SYMBOLIC)
file(CREATE_LINK ${P4C_SOURCE_DIR}/.clang-format ${flay_SOURCE_DIR}/.clang-format SYMBOLIC)
# Copy the current CPPLint configuration to the flay directory and modify the root.
file(CREATE_LINK ${P4C_SOURCE_DIR}/CPPLINT.cfg ${flay_SOURCE_DIR}/CPPLINT.cfg)
message(STATUS "Running sed command:")
# Note we need to use bash to make this work.
execute_process(COMMAND bash "-c" "sed -i 's|^root=[^ ]*|root=${P4C_SOURCE_DIR}|' ${flay_SOURCE_DIR}/CPPLINT.cfg" RESULT_VARIABLE rv COMMAND_ECHO STDOUT)
if(NOT rv EQUAL 0)
  message(WARNING "Failed to modify CPPLINT.cfg")
endif()
