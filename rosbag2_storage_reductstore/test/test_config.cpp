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

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "rosbag2_storage_reductstore/config.hpp"

namespace
{

std::filesystem::path write_config(const std::string & contents)
{
  const auto path = std::filesystem::temp_directory_path() / "reductstore_storage_test.yaml";
  std::ofstream stream(path);
  stream << contents;
  stream.close();
  return path;
}

TEST(Config, LoadsAnonymousConnection)
{
  const auto config =
    rosbag2_storage_reductstore::load_config(write_config("url: http://localhost:8383\n").string());
  EXPECT_EQ(config.url, "http://localhost:8383");
  EXPECT_TRUE(config.api_token.empty());
}

TEST(Config, LoadsDirectToken)
{
  const auto config = rosbag2_storage_reductstore::load_config(
    write_config("url: http://localhost:8383\napi_token: a-token\n").string());
  EXPECT_EQ(config.url, "http://localhost:8383");
  EXPECT_FALSE(config.api_token.empty());
}

TEST(Config, LoadsEnvironmentToken)
{
  ASSERT_EQ(setenv("REDUCTSTORE_TEST_TOKEN", "a-token", 1), 0);
  const auto config = rosbag2_storage_reductstore::load_config(
    write_config("url: http://localhost:8383\napi_token_env: REDUCTSTORE_TEST_TOKEN\n").string());
  EXPECT_FALSE(config.api_token.empty());
  ASSERT_EQ(unsetenv("REDUCTSTORE_TEST_TOKEN"), 0);
}

TEST(Config, RejectsMissingUrlFileEnvironmentAndAmbiguousCredentials)
{
  const auto ambiguous_config_path = write_config(
    "url: http://localhost\n"
    "api_token: a-token\n"
    "api_token_env: REDUCTSTORE_TEST_TOKEN\n");
  EXPECT_THROW(rosbag2_storage_reductstore::load_config("/does/not/exist.yaml"),
      std::runtime_error);
  EXPECT_THROW(
    rosbag2_storage_reductstore::load_config(write_config("api_token: a-token\n").string()),
      std::runtime_error);
  EXPECT_THROW(
    rosbag2_storage_reductstore::load_config(
      write_config(
      "url: http://localhost\napi_token_env: MISSING_REDUCTSTORE_TEST_TOKEN\n").string()),
    std::runtime_error);
  EXPECT_THROW(
    rosbag2_storage_reductstore::load_config(ambiguous_config_path.string()),
    std::runtime_error);
}

TEST(Config, RejectsMalformedYaml)
{
  EXPECT_THROW(
    rosbag2_storage_reductstore::load_config(write_config("url: [unterminated\n").string()),
      std::runtime_error);
}

TEST(BucketName, HandlesJazzyAndFallbackUris)
{
  EXPECT_EQ(rosbag2_storage_reductstore::bucket_name_from_uri("/tmp/my_bag/my_bag_0"), "my_bag");
  EXPECT_EQ(rosbag2_storage_reductstore::bucket_name_from_uri("my_bag/my_bag_0"), "my_bag");
  EXPECT_EQ(rosbag2_storage_reductstore::bucket_name_from_uri("/tmp/remote-storage"),
      "remote-storage");
}

TEST(BucketName, RejectsInvalidNames)
{
  EXPECT_THROW(rosbag2_storage_reductstore::bucket_name_from_uri(""), std::runtime_error);
  EXPECT_THROW(rosbag2_storage_reductstore::bucket_name_from_uri("/tmp/$invalid"),
      std::runtime_error);
}

}  // namespace
