#include "rosbag2_storage_reductstore/reductstore_storage.hpp"

#include <stdexcept>
#include <utility>

#include <pluginlib/class_list_macros.hpp>
#include <reduct/client.h>

namespace rosbag2_storage_reductstore
{
namespace
{

[[noreturn]] void not_implemented()
{
  throw std::runtime_error("ReductStore storage backend is not implemented");
}

}  // namespace

ReductStoreStorage::~ReductStoreStorage() = default;

void ReductStoreStorage::open(
  const rosbag2_storage::StorageOptions & storage_options,
  rosbag2_storage::storage_interfaces::IOFlag io_flag)
{
  if (storage_options.max_bagfile_size != 0) {
    throw std::runtime_error("ReductStore storage does not support bagfile size splitting");
  }
  if (storage_options.max_bagfile_duration != 0) {
    throw std::runtime_error("ReductStore storage does not support bagfile duration splitting");
  }
  if (storage_options.snapshot_mode) {
    throw std::runtime_error("ReductStore storage does not support snapshot mode");
  }
  if (io_flag == rosbag2_storage::storage_interfaces::IOFlag::APPEND) {
    throw std::runtime_error("ReductStore storage does not support append mode");
  }

  config_ = load_config(storage_options.storage_config_uri);
  bucket_name_ = bucket_name_from_uri(storage_options.uri);
  uri_ = storage_options.uri;
  io_flag_ = io_flag;
  not_implemented();
}

void ReductStoreStorage::write(std::shared_ptr<const rosbag2_storage::SerializedBagMessage> msg)
{
  static_cast<void>(msg);
  not_implemented();
}

void ReductStoreStorage::write(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>> & msg)
{
  static_cast<void>(msg);
  not_implemented();
}

void ReductStoreStorage::update_metadata(const rosbag2_storage::BagMetadata & bag_metadata)
{
  static_cast<void>(bag_metadata);
  not_implemented();
}

void ReductStoreStorage::create_topic(
  const rosbag2_storage::TopicMetadata & topic,
  const rosbag2_storage::MessageDefinition & message_definition)
{
  static_cast<void>(topic);
  static_cast<void>(message_definition);
  not_implemented();
}

void ReductStoreStorage::remove_topic(const rosbag2_storage::TopicMetadata & topic)
{
  static_cast<void>(topic);
  not_implemented();
}

bool ReductStoreStorage::set_read_order(const rosbag2_storage::ReadOrder & read_order)
{
  static_cast<void>(read_order);
  not_implemented();
}

bool ReductStoreStorage::has_next()
{
  not_implemented();
}

std::shared_ptr<rosbag2_storage::SerializedBagMessage> ReductStoreStorage::read_next()
{
  not_implemented();
}

std::vector<rosbag2_storage::TopicMetadata> ReductStoreStorage::get_all_topics_and_types()
{
  not_implemented();
}

void ReductStoreStorage::get_all_message_definitions(
  std::vector<rosbag2_storage::MessageDefinition> & definitions)
{
  static_cast<void>(definitions);
  not_implemented();
}

rosbag2_storage::BagMetadata ReductStoreStorage::get_metadata()
{
  not_implemented();
}

std::string ReductStoreStorage::get_relative_file_path() const
{
  return uri_;
}

uint64_t ReductStoreStorage::get_bagfile_size() const
{
  return 0;
}

std::string ReductStoreStorage::get_storage_identifier() const
{
  return "reductstore";
}

uint64_t ReductStoreStorage::get_minimum_split_file_size() const
{
  return 0;
}

void ReductStoreStorage::set_filter(const rosbag2_storage::StorageFilter & storage_filter)
{
  static_cast<void>(storage_filter);
  not_implemented();
}

void ReductStoreStorage::reset_filter()
{
  not_implemented();
}

void ReductStoreStorage::seek(const rcutils_time_point_value_t & timestamp)
{
  static_cast<void>(timestamp);
  not_implemented();
}

}  // namespace rosbag2_storage_reductstore

PLUGINLIB_EXPORT_CLASS(
  rosbag2_storage_reductstore::ReductStoreStorage,
  rosbag2_storage::storage_interfaces::ReadWriteInterface)
