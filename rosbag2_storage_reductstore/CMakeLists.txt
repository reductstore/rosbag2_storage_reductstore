cmake_minimum_required(VERSION 3.16)
project(rosbag2_storage_reductstore)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Allow direct IDE configuration after the vendor package has been built with colcon.
set(_reductcpp_vendor_prefix "${CMAKE_CURRENT_SOURCE_DIR}/../install/reductcpp_vendor")
if(EXISTS "${_reductcpp_vendor_prefix}/share/reductcpp_vendor/cmake/reductcpp_vendorConfig.cmake")
  list(PREPEND CMAKE_PREFIX_PATH "${_reductcpp_vendor_prefix}")
endif()
string(REPLACE ";" ":" _ament_prefix_path "${CMAKE_PREFIX_PATH}")
if("$ENV{AMENT_PREFIX_PATH}" STREQUAL "")
  set(ENV{AMENT_PREFIX_PATH} "${_ament_prefix_path}")
else()
  set(ENV{AMENT_PREFIX_PATH} "${_ament_prefix_path}:$ENV{AMENT_PREFIX_PATH}")
endif()

message(STATUS "PYTHONPATH=$ENV{PYTHONPATH}")
message(STATUS "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
message(STATUS "Python3_EXECUTABLE=${Python3_EXECUTABLE}")

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(pluginlib REQUIRED)
find_package(reductcpp_vendor REQUIRED)
find_package(reductcpp REQUIRED)
find_package(rosbag2_storage REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(yaml-cpp REQUIRED)
get_filename_component(_reductcpp_vendor_prefix "${reductcpp_vendor_DIR}/../../.." ABSOLUTE)

add_library(${PROJECT_NAME} SHARED
  src/config.cpp
  src/reductstore_storage.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  "${_reductcpp_vendor_prefix}/opt/reductcpp_vendor/include"
)
ament_target_dependencies(${PROJECT_NAME}
  pluginlib
  rosbag2_storage
  yaml_cpp_vendor
)
target_link_libraries(${PROJECT_NAME} reductcpp::reductcpp yaml-cpp)
pluginlib_export_plugin_description_file(rosbag2_storage plugin_description.xml)
ament_python_install_package(rosbag2_storage_reductstore_cli PACKAGE_DIR python/rosbag2_storage_reductstore_cli)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  ament_add_gtest(test_config test/test_config.cpp)
  target_link_libraries(test_config ${PROJECT_NAME})

  ament_add_gtest(test_storage test/test_storage.cpp)
  target_link_libraries(test_storage ${PROJECT_NAME})

  ament_add_gtest(test_plugin test/test_plugin.cpp)
  ament_target_dependencies(test_plugin pluginlib rosbag2_storage)
  target_link_libraries(test_plugin ${PROJECT_NAME})
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_export_dependencies(pluginlib reductcpp_vendor rosbag2_storage yaml_cpp_vendor)
ament_package()
