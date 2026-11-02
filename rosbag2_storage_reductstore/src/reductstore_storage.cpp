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

#include "rosbag2_storage_reductstore/reductstore_storage.hpp"

#include <reduct/client.h>

#include <pluginlib/class_list_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>

#include "rcutils/logging_macros.h"

constexpr const char* kLogger = "rosbag2_storage_reductstore";

namespace rosbag2_storage_reductstore {
namespace {

[[noreturn]] void not_implemented(
  const std::source_location& location = std::source_location::current())
{
  throw std::runtime_error(
          std::string("ReductStore storage backend is not implemented at ") +
          location.file_name() + ":" + std::to_string(location.line()) +
          " in " + location.function_name());
}

std::string json_quote(const std::string& value)
{
  std::string result = "\"";
  for (const unsigned char character : value) {
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          result += "\\u00";
          result += hex[character >> 4];
          result += hex[character & 0x0f];
        } else {
          result += static_cast<char>(character);
        }
    }
  }
  result += '"';
  return result;
}

std::string entry_name_from_topic(const std::string& topic_name)
{
  const auto first_character = topic_name.starts_with('/') ? 1 : 0;
  const auto entry_name = topic_name.substr(first_character);
  if (entry_name.empty() || entry_name.front() == '/' || entry_name.back() == '/' ||
      entry_name.find("//") != std::string::npos)
  {
    throw std::runtime_error("ROS topic cannot be mapped to a valid ReductStore entry name: '" +
                             topic_name + "'");
  }
  return entry_name;
}

}  // namespace

ReductStoreStorage::~ReductStoreStorage() = default;

void ReductStoreStorage::open(const rosbag2_storage::StorageOptions& storage_options,
                              rosbag2_storage::storage_interfaces::IOFlag io_flag) {
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

  reduct::HttpOptions client_options{};
  client_options.api_token = config_.api_token;
  client_options.ssl_verification = true;
  client_ = reduct::IClient::Build(config_.url, std::move(client_options));
  if (!client_) {
    throw std::runtime_error("Failed to initialize ReductStore client");
  }
  auto bucket_result = client_->GetOrCreateBucket(bucket_name_);
  if (bucket_result.error) {
    throw std::runtime_error("Failed to get or create ReductStore bucket '" + bucket_name_ +
                             "': " + bucket_result.error.ToString());
  }
  bucket_ = std::move(bucket_result.result);
  if (!bucket_) {
    throw std::runtime_error("ReductStore returned an empty bucket handle for '" + bucket_name_ + "'");
  }

  RCUTILS_LOG_INFO_NAMED(
    "rosbag2_storage_reductstore",
    "Starting with configuration: url='%s', bucket='%s', authentication=%s",
    config_.url.c_str(), bucket_name_.c_str(), config_.api_token.empty() ? "disabled" : "enabled");
}

void ReductStoreStorage::write(std::shared_ptr<const rosbag2_storage::SerializedBagMessage> msg) {
  write_messages({std::move(msg)});
}

void ReductStoreStorage::write(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>>& messages)
{
  write_messages(messages);
}

void ReductStoreStorage::write_messages(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>>& messages)
{
  if (!bucket_) {
    throw std::runtime_error("ReductStore bucket is not initialized; call open() first");
  }

  RCUTILS_LOG_DEBUG_NAMED(kLogger, "Writing %zu message(s)", messages.size());
  for (size_t begin = 0; begin < messages.size();) {
    const auto end = begin + std::min(config_.max_records_per_batch, messages.size() - begin);
    write_batch(messages, begin, end);
    begin = end;
  }
}

void ReductStoreStorage::write_batch(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>>& messages,
  size_t begin,
  size_t end)
{
  const auto write_result = bucket_->WriteBatch([this, &messages, begin, end](reduct::IBucket::Batch* batch) {
    for (auto index = begin; index < end; ++index) {
      const auto& message = messages[index];
      if (!message) {
        throw std::invalid_argument("Cannot write a null serialized bag message");
      }
      add_message_to_batch(*batch, *message);
    }
  });
  if (write_result.error) {
    throw std::runtime_error("Failed to write ReductStore message batch: " +
                             write_result.error.ToString());
  }
  if (!write_result.result.empty()) {
    const auto& [entry_name, errors] = *write_result.result.begin();
    const auto& [timestamp, error] = *errors.begin();
    static_cast<void>(timestamp);
    throw std::runtime_error("Failed to write ReductStore message to entry '" + entry_name + "': " +
                             error.ToString());
  }
}

void ReductStoreStorage::add_message_to_batch(
  reduct::IBucket::Batch& batch,
  const rosbag2_storage::SerializedBagMessage& message) const
{
  const auto entry_name = entry_name_from_topic(message.topic_name);
  const auto* serialized_data = message.serialized_data.get();
  if (!serialized_data || (serialized_data->buffer_length != 0 && !serialized_data->buffer)) {
    throw std::invalid_argument("Serialized bag message for topic '" + message.topic_name +
                                "' has no payload buffer");
  }

  const auto timestamp = reduct::IClient::Time(std::chrono::nanoseconds(message.recv_timestamp));
  const auto* data = reinterpret_cast<const char*>(serialized_data->buffer);
  batch.AddRecord(entry_name, timestamp,
                  std::string(data == nullptr ? "" : data, serialized_data->buffer_length));
}

void ReductStoreStorage::update_metadata(const rosbag2_storage::BagMetadata& bag_metadata) {
  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "update_metadata called: storage_id='%s', message_count=%llu, topic_count=%zu, "
    "bag_size=%llu, duration_ns=%lld, storage_path_count=%zu",
    bag_metadata.storage_identifier.c_str(),
    static_cast<unsigned long long>(bag_metadata.message_count),
    bag_metadata.topics_with_message_count.size(),
    static_cast<unsigned long long>(bag_metadata.bag_size),
    static_cast<long long>(bag_metadata.duration.count()),
    bag_metadata.relative_file_paths.size());

 metadata_ = bag_metadata;
}

void ReductStoreStorage::create_topic(const rosbag2_storage::TopicMetadata& topic,
                                      const rosbag2_storage::MessageDefinition& message_definition) {
  if (!bucket_) {
    throw std::runtime_error("ReductStore bucket is not initialized; call open() first");
  }

  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "create_topic called: name='%s', type='%s', serialization_format='%s', "
    "definition_encoding='%s', definition_size=%zu, type_hash='%s'",
    topic.name.c_str(), topic.type.c_str(), topic.serialization_format.c_str(),
    message_definition.encoding.c_str(), message_definition.encoded_message_definition.size(),
    message_definition.type_hash.c_str());

  topics_.insert_or_assign(topic.name, topic);
  message_definitions_.insert_or_assign(topic.name, message_definition);

  const std::string schema_json =
    "{\"encoding\":" + json_quote(topic.serialization_format) +
    ",\"topic\":" + json_quote(topic.name) +
    ",\"schema_name\":" + json_quote(message_definition.topic_type) +
    ",\"schema\":" + json_quote(message_definition.encoded_message_definition) + "}";
  const reduct::IBucket::AttachmentMap attachments{{"$schema", schema_json}};
  const auto entry_name = entry_name_from_topic(topic.name);
  const auto error = bucket_->WriteAttachments(entry_name, attachments);
  if (error) {
    throw std::runtime_error("Failed to write $schema attachment for topic '" + topic.name +
                             "': " + error.ToString());
  }
}

void ReductStoreStorage::remove_topic(const rosbag2_storage::TopicMetadata& topic) {
  RCUTILS_LOG_DEBUG_NAMED(kLogger, "remove_topic called: name='%s'", topic.name.c_str());
  topics_.erase(topic.name);
  message_definitions_.erase(topic.name);
}

bool ReductStoreStorage::set_read_order(const rosbag2_storage::ReadOrder& read_order) {
  static_cast<void>(read_order);
  not_implemented();
}

bool ReductStoreStorage::has_next() { not_implemented(); }

std::shared_ptr<rosbag2_storage::SerializedBagMessage> ReductStoreStorage::read_next() { not_implemented(); }

std::vector<rosbag2_storage::TopicMetadata> ReductStoreStorage::get_all_topics_and_types() { not_implemented(); }

void ReductStoreStorage::get_all_message_definitions(std::vector<rosbag2_storage::MessageDefinition>& definitions) {
  static_cast<void>(definitions);
  not_implemented();
}

rosbag2_storage::BagMetadata ReductStoreStorage::get_metadata() { not_implemented(); }

std::string ReductStoreStorage::get_relative_file_path() const { return uri_; }

uint64_t ReductStoreStorage::get_bagfile_size() const { return 0; }

std::string ReductStoreStorage::get_storage_identifier() const { return "reductstore"; }

uint64_t ReductStoreStorage::get_minimum_split_file_size() const { return 0; }

void ReductStoreStorage::set_filter(const rosbag2_storage::StorageFilter& storage_filter) {
  static_cast<void>(storage_filter);
  not_implemented();
}

void ReductStoreStorage::reset_filter() { not_implemented(); }

void ReductStoreStorage::seek(const rcutils_time_point_value_t& timestamp) {
  static_cast<void>(timestamp);
  not_implemented();
}

}  // namespace rosbag2_storage_reductstore

PLUGINLIB_EXPORT_CLASS(rosbag2_storage_reductstore::ReductStoreStorage,
                       rosbag2_storage::storage_interfaces::ReadWriteInterface)
