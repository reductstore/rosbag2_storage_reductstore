// Copyright (c) 2026 ReductStore
// MIT License
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef ROSBAG2_STORAGE_REDUCTSTORE__VISIBILITY_CONTROL_HPP_
#define ROSBAG2_STORAGE_REDUCTSTORE__VISIBILITY_CONTROL_HPP_

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define ROSBAG2_STORAGE_REDUCTSTORE_EXPORT __attribute__((dllexport))
    #define ROSBAG2_STORAGE_REDUCTSTORE_IMPORT __attribute__((dllimport))
  #else
    #define ROSBAG2_STORAGE_REDUCTSTORE_EXPORT __declspec(dllexport)
    #define ROSBAG2_STORAGE_REDUCTSTORE_IMPORT __declspec(dllimport)
  #endif
  #ifdef ROSBAG2_STORAGE_REDUCTSTORE_BUILDING_DLL
    #define ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC ROSBAG2_STORAGE_REDUCTSTORE_EXPORT
  #else
    #define ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC ROSBAG2_STORAGE_REDUCTSTORE_IMPORT
  #endif
  #define ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC_TYPE ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC
#else
  #define ROSBAG2_STORAGE_REDUCTSTORE_EXPORT __attribute__((visibility("default")))
  #define ROSBAG2_STORAGE_REDUCTSTORE_IMPORT
  #if __GNUC__ >= 4
    #define ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC __attribute__((visibility("default")))
    #define ROSBAG2_STORAGE_REDUCTSTORE_LOCAL __attribute__((visibility("hidden")))
  #else
    #define ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC
    #define ROSBAG2_STORAGE_REDUCTSTORE_LOCAL
  #endif
  #define ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC_TYPE
#endif

#endif  // ROSBAG2_STORAGE_REDUCTSTORE__VISIBILITY_CONTROL_HPP_
