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
