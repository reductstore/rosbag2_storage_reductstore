#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <gtest/gtest.h>

#include "rosbag2_storage_reductstore/reductstore_storage.hpp"

namespace
{

rosbag2_storage::StorageOptions valid_options()
{
  const auto config_path = std::filesystem::temp_directory_path() / "reductstore_storage_open_test.yaml";
  std::ofstream stream(config_path);
  stream << "url: http://localhost:8383\n";
  stream.close();

  rosbag2_storage::StorageOptions options;
  options.uri = "/tmp/my_bag/my_bag_0";
  options.storage_config_uri = config_path.string();
  return options;
}

TEST(Storage, ReportsScaffoldIdentity)
{
  rosbag2_storage_reductstore::ReductStoreStorage storage;
  EXPECT_EQ(storage.get_storage_identifier(), "reductstore");
  EXPECT_EQ(storage.get_relative_file_path(), "");
  EXPECT_EQ(storage.get_bagfile_size(), 0U);
  EXPECT_EQ(storage.get_minimum_split_file_size(), 0U);
}

TEST(Storage, RejectsUnsupportedOpenModesBeforeBackendUse)
{
  rosbag2_storage_reductstore::ReductStoreStorage storage;
  auto options = valid_options();

  options.max_bagfile_size = 1;
  EXPECT_THROW(storage.open(options), std::runtime_error);
  options.max_bagfile_size = 0;
  options.max_bagfile_duration = 1;
  EXPECT_THROW(storage.open(options), std::runtime_error);
  options.max_bagfile_duration = 0;
  options.snapshot_mode = true;
  EXPECT_THROW(storage.open(options), std::runtime_error);
  options.snapshot_mode = false;
  EXPECT_THROW(storage.open(options, rosbag2_storage::storage_interfaces::IOFlag::APPEND), std::runtime_error);
}

TEST(Storage, FailsExplicitlyAfterValidOpenValidation)
{
  rosbag2_storage_reductstore::ReductStoreStorage storage;
  EXPECT_THROW(storage.open(valid_options()), std::runtime_error);
}

}  // namespace
