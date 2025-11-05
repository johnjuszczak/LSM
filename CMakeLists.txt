cmake_minimum_required(VERSION 3.24)
project(lsm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LSM_BUILD_EXAMPLES "Build examples" OFF)
option(LSM_BUILD_TESTS "Build tests" OFF)

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_sources(${PROJECT_NAME}
  INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/core.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/cosm.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/concepts.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/effect.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/handlers.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/helpers.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/machine_impl.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/policy.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lsm/detail/types.hpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_EXTENSIONS OFF)

if (PROJECT_IS_TOP_LEVEL AND LSM_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if (PROJECT_IS_TOP_LEVEL AND LSM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
  EXPORT lsmTargets
  FILE_SET HEADERS
)
install(EXPORT lsmTargets
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lsm
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/lsmConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lsmConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/lsmConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lsm
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/lsmConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/lsmConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lsm
)
