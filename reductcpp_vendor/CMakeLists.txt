cmake_minimum_required(VERSION 3.16)
project(reductcpp_vendor)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_vendor_package REQUIRED)

ament_vendor(reductcpp_vendor
  VCS_URL https://github.com/reductstore/reduct-cpp.git
  VCS_VERSION v1.21.2
  CMAKE_ARGS
    -DREDUCT_CPP_USE_FETCHCONTENT=OFF
    -DREDUCT_CPP_ENABLE_TESTS=OFF
)

ament_package()
