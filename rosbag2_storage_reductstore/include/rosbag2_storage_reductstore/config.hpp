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

#ifndef ROSBAG2_STORAGE_REDUCTSTORE__CONFIG_HPP_
#define ROSBAG2_STORAGE_REDUCTSTORE__CONFIG_HPP_

#include <string>

#include "rosbag2_storage_reductstore/visibility_control.hpp"

namespace rosbag2_storage_reductstore
{

struct ReductStoreConfig
{
  std::string url;
  std::string api_token;
};

ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC ReductStoreConfig load_config(const std::string & path);
ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC std::string bucket_name_from_uri(const std::string & uri);

}  // namespace rosbag2_storage_reductstore

#endif  // ROSBAG2_STORAGE_REDUCTSTORE__CONFIG_HPP_
