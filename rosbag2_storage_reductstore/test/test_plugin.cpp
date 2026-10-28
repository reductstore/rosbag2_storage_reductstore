#include <algorithm>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>

#include "rosbag2_storage/storage_interfaces/read_write_interface.hpp"

TEST(Plugin, IsDeclaredAndConstructible)
{
  pluginlib::ClassLoader<rosbag2_storage::storage_interfaces::ReadWriteInterface> loader(
    "rosbag2_storage", "rosbag2_storage::storage_interfaces::ReadWriteInterface");
  const auto classes = loader.getDeclaredClasses();
  EXPECT_NE(std::find(classes.begin(), classes.end(), "reductstore"), classes.end());
  auto storage = loader.createUniqueInstance("reductstore");
  EXPECT_EQ(storage->get_storage_identifier(), "reductstore");
}
