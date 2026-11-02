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

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <limits>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <pluginlib/class_list_macros.hpp>

#include "rcutils/logging_macros.h"
#include "rcutils/allocator.h"
#include "rcutils/types/uint8_array.h"

constexpr const char * kLogger = "rosbag2_storage_reductstore";

namespace rosbag2_storage_reductstore
{
namespace
{

std::string json_quote(const std::string & value)
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

std::string entry_name_from_topic(const std::string & topic_name)
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

std::string schema_member(const std::string_view schema, const std::string_view key)
{
  const auto member = '"' + std::string(key) + '"';
  const auto key_position = schema.find(member);
  if (key_position == std::string_view::npos) {
    throw std::runtime_error("ReductStore topic schema is missing '" + std::string(key) + "'");
  }
  const auto colon_position = schema.find(':', key_position + member.size());
  const auto value_position = schema.find('"', colon_position + 1);
  if (colon_position == std::string_view::npos || value_position == std::string_view::npos) {
    throw std::runtime_error("ReductStore topic schema has an invalid '" + std::string(key) + "'");
  }

  std::string value;
  for (size_t position = value_position + 1; position < schema.size(); ++position) {
    const auto character = schema[position];
    if (character == '"') {
      return value;
    }
    if (character != '\\' || ++position == schema.size()) {
      value += character;
      continue;
    }
    switch (schema[position]) {
      case '"': value += '"'; break;
      case '\\': value += '\\'; break;
      case '/': value += '/'; break;
      case 'b': value += '\b'; break;
      case 'f': value += '\f'; break;
      case 'n': value += '\n'; break;
      case 'r': value += '\r'; break;
      case 't': value += '\t'; break;
      default:
        throw std::runtime_error(
            "ReductStore topic schema contains an unsupported escape sequence");
    }
  }
  throw std::runtime_error("ReductStore topic schema has an unterminated '" + std::string(key) +
        "'");
}

}  // namespace

ReductStoreStorage::~ReductStoreStorage()
{
  stop_query();
}

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

  reduct::HttpOptions client_options{};
  client_options.api_token = config_.api_token;
  client_options.ssl_verification = true;
  client_options.connection_timeout = std::chrono::seconds(5);
  client_options.request_timeout = std::chrono::seconds(10);
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
    throw std::runtime_error("ReductStore returned an empty bucket handle for '" + bucket_name_ +
        "'");
  }
  auto query_bucket_result = client_->GetBucket(bucket_name_);
  if (query_bucket_result.error) {
    throw std::runtime_error("Failed to get ReductStore query bucket '" + bucket_name_ + "': " +
                             query_bucket_result.error.ToString());
  }
  query_bucket_ = std::move(query_bucket_result.result);
  if (!query_bucket_) {
    throw std::runtime_error("ReductStore returned an empty query bucket handle for '" +
        bucket_name_ + "'");
  }

  RCUTILS_LOG_INFO_NAMED(
    "rosbag2_storage_reductstore",
    "Starting with configuration: url='%s', bucket='%s', authentication=%s",
    config_.url.c_str(), bucket_name_.c_str(), config_.api_token.empty() ? "disabled" : "enabled");
}

void ReductStoreStorage::write(std::shared_ptr<const rosbag2_storage::SerializedBagMessage> msg)
{
  write_messages({std::move(msg)});
}

void ReductStoreStorage::write(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>> & messages)
{
  write_messages(messages);
}

void ReductStoreStorage::write_messages(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>> & messages)
{
  if (!bucket_) {
    throw std::runtime_error("ReductStore bucket is not initialized; call open() first");
  }

  RCUTILS_LOG_DEBUG_NAMED(kLogger, "Writing %zu message(s)", messages.size());
  for (size_t begin = 0; begin < messages.size(); ) {
    const auto end = begin + std::min(config_.max_records_per_batch, messages.size() - begin);
    write_batch(messages, begin, end);
    begin = end;
  }
}

void ReductStoreStorage::write_batch(
  const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>> & messages,
  size_t begin,
  size_t end)
{
  const auto write_result = bucket_->WriteBatch([this, &messages, begin,
      end](reduct::IBucket::Batch * batch) {
        for (auto index = begin; index < end; ++index) {
          const auto & message = messages[index];
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
    const auto & [entry_name, errors] = *write_result.result.begin();
    const auto & [timestamp, error] = *errors.begin();
    static_cast<void>(timestamp);
    throw std::runtime_error("Failed to write ReductStore message to entry '" + entry_name + "': " +
                             error.ToString());
  }
}

void ReductStoreStorage::add_message_to_batch(
  reduct::IBucket::Batch & batch,
  const rosbag2_storage::SerializedBagMessage & message) const
{
  const auto entry_name = entry_name_from_topic(message.topic_name);
  const auto * serialized_data = message.serialized_data.get();
  if (!serialized_data || (serialized_data->buffer_length != 0 && !serialized_data->buffer)) {
    throw std::invalid_argument("Serialized bag message for topic '" + message.topic_name +
                                "' has no payload buffer");
  }

  const auto timestamp = reduct::IClient::Time(std::chrono::nanoseconds(message.recv_timestamp));
  const auto * data = reinterpret_cast<const char *>(serialized_data->buffer);
  batch.AddRecord(entry_name, timestamp,
                  std::string(data == nullptr ? "" : data, serialized_data->buffer_length));
}

void ReductStoreStorage::update_metadata(const rosbag2_storage::BagMetadata & bag_metadata)
{
  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "update_metadata called: storage_id='%s', message_count=%" PRIu64 ", topic_count=%zu, "
    "bag_size=%" PRIu64 ", duration_ns=%" PRId64 ", storage_path_count=%zu",
    bag_metadata.storage_identifier.c_str(),
    static_cast<uint64_t>(bag_metadata.message_count),
    bag_metadata.topics_with_message_count.size(),
    static_cast<uint64_t>(bag_metadata.bag_size),
    static_cast<int64_t>(bag_metadata.duration.count()),
    bag_metadata.relative_file_paths.size());

  metadata_ = bag_metadata;
}

void ReductStoreStorage::create_topic(
  const rosbag2_storage::TopicMetadata & topic,
  const rosbag2_storage::MessageDefinition & message_definition)
{
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

void ReductStoreStorage::remove_topic(const rosbag2_storage::TopicMetadata & topic)
{
  RCUTILS_LOG_DEBUG_NAMED(kLogger, "remove_topic called: name='%s'", topic.name.c_str());
  topics_.erase(topic.name);
  message_definitions_.erase(topic.name);
}

bool ReductStoreStorage::set_read_order(const rosbag2_storage::ReadOrder & read_order)
{
  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "set_read_order called: sort_by=%d, reverse=%s",
    static_cast<int>(read_order.sort_by), read_order.reverse ? "true" : "false");
  if (read_order.reverse) {
    throw std::runtime_error("ReductStore storage does not support reverse read order");
  }
  if (read_order.sort_by != rosbag2_storage::ReadOrder::ReceivedTimestamp) {
    throw std::runtime_error("ReductStore storage only supports received-timestamp read order");
  }
  return true;
}

std::vector<std::string> ReductStoreStorage::filtered_entry_names() const
{
  const auto entries_result = bucket_->GetEntryList();
  if (entries_result.error) {
    throw std::runtime_error("Failed to list ReductStore entries: " +
                             entries_result.error.ToString());
  }

  std::optional<std::regex> include_regex;
  std::optional<std::regex> exclude_regex;
  try {
    if (!filter_.regex.empty()) {
      include_regex.emplace(filter_.regex);
    }
    if (!filter_.regex_to_exclude.empty()) {
      exclude_regex.emplace(filter_.regex_to_exclude);
    }
  } catch (const std::regex_error & error) {
    throw std::runtime_error("Invalid ReductStore topic filter regex: " +
        std::string(error.what()));
  }

  const auto contains_topic = [](const std::vector<std::string> & topics,
    const std::string & topic) {
      return std::find(topics.begin(), topics.end(), topic) != topics.end();
    };
  const auto has_name_filter = !filter_.topics.empty() || !filter_.services_events.empty();
  std::vector<std::string> names;
  names.reserve(entries_result.result.size());
  for (const auto & entry : entries_result.result) {
    const auto topic = "/" + entry.name;
    const auto included_by_name = contains_topic(filter_.topics, topic) ||
      contains_topic(filter_.services_events, topic);
    const auto excluded_by_name = contains_topic(filter_.exclude_topics, topic) ||
      contains_topic(filter_.exclude_service_events, topic);
    if ((has_name_filter && !included_by_name) || excluded_by_name ||
      (include_regex && !std::regex_search(topic, *include_regex)) ||
      (exclude_regex && std::regex_search(topic, *exclude_regex)))
    {
      continue;
    }
    names.push_back(entry.name);
  }
  return names;
}

void ReductStoreStorage::stop_query()
{
  if (query_thread_.joinable()) {
    query_thread_.request_stop();
    query_condition_.notify_all();
    query_thread_.join();
  }
}

void ReductStoreStorage::start_query()
{
  if (!query_bucket_) {
    throw std::runtime_error("ReductStore bucket is not initialized; call open() first");
  }

  stop_query();
  const auto entries = filtered_entry_names();
  {
    std::lock_guard lock(query_mutex_);
    records_.clear();
    query_error_ = nullptr;
    query_finished_ = entries.empty();
  }
  if (entries.empty()) {
    query_condition_.notify_all();
    return;
  }

  const auto start = seek_timestamp_;
  query_thread_ = std::jthread([this, entries, start](const std::stop_token stop_token) {
        reduct::IBucket::QueryOptions options;
        const auto error = query_bucket_->Query(
      entries, start, std::nullopt, std::move(options),
          [this, stop_token](const reduct::IBucket::ReadableRecord & record) {
            if (stop_token.stop_requested()) {
              return false;
            }
            auto data_result = record.ReadAll();
            if (data_result.error) {
              std::lock_guard lock(query_mutex_);
              query_error_ = std::make_exception_ptr(std::runtime_error(
            "Failed to read ReductStore record: " + data_result.error.ToString()));
              query_condition_.notify_all();
              return false;
            }

            std::unique_lock lock(query_mutex_);
            query_condition_.wait(lock, [this, &stop_token] {
              return stop_token.stop_requested() || records_.size() < config_.max_records_per_batch;
        });
            if (stop_token.stop_requested()) {
              return false;
            }
            records_.push_back({record.entry, record.timestamp, std::move(data_result.result)});
            lock.unlock();
            query_condition_.notify_all();
            return true;
      });

        std::lock_guard lock(query_mutex_);
        if (error && !stop_token.stop_requested() && !query_error_) {
          query_error_ = std::make_exception_ptr(std::runtime_error(
        "Failed to query ReductStore records: " + error.ToString()));
        }
        query_finished_ = true;
        query_condition_.notify_all();
  });
}

bool ReductStoreStorage::has_next()
{
  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "has_next called: seek_timestamp_set=%s, topic_filter_count=%zu",
    seek_timestamp_.has_value() ? "true" : "false", filter_.topics.size());
  std::unique_lock lock(query_mutex_);
  query_condition_.wait(lock, [this] {
      return !records_.empty() || query_finished_ || query_error_;
  });
  if (query_error_) {
    std::rethrow_exception(query_error_);
  }
  return !records_.empty();
}

std::shared_ptr<rosbag2_storage::SerializedBagMessage> ReductStoreStorage::read_next()
{
  if (!has_next()) {
    throw std::runtime_error("No ReductStore records are available to read");
  }

  CachedRecord record;
  {
    std::lock_guard lock(query_mutex_);
    record = std::move(records_.front());
    records_.pop_front();
  }
  query_condition_.notify_one();

  auto serialized_data = std::unique_ptr<rcutils_uint8_array_t>(new rcutils_uint8_array_t{});
  const auto allocator = rcutils_get_default_allocator();
  if (rcutils_uint8_array_init(serialized_data.get(), record.data.size(),
      &allocator) != RCUTILS_RET_OK)
  {
    throw std::runtime_error("Failed to allocate serialized ReductStore message data");
  }
  if (!record.data.empty()) {
    std::memcpy(serialized_data->buffer, record.data.data(), record.data.size());
  }
  serialized_data->buffer_length = record.data.size();

  const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
    record.timestamp.time_since_epoch()).count();
  auto message = std::make_shared<rosbag2_storage::SerializedBagMessage>();
  message->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
    serialized_data.release(), [](rcutils_uint8_array_t * data) {
      const auto result = rcutils_uint8_array_fini(data);
      static_cast<void>(result);
      delete data;
    });
  message->recv_timestamp = timestamp;
  message->send_timestamp = timestamp;
  message->topic_name = "/" + record.entry;
  return message;
}

std::vector<rosbag2_storage::TopicMetadata> ReductStoreStorage::get_all_topics_and_types()
{
  RCUTILS_LOG_DEBUG_NAMED(kLogger, "get_all_topics_and_types called");
  if (!bucket_) {
    throw std::runtime_error("ReductStore bucket is not initialized; call open() first");
  }

  const auto entries_result = bucket_->GetEntryList();
  if (entries_result.error) {
    throw std::runtime_error("Failed to list ReductStore entries: " +
                             entries_result.error.ToString());
  }

  std::vector<rosbag2_storage::TopicMetadata> topics;
  topics.reserve(entries_result.result.size());
  for (const auto & entry : entries_result.result) {
    RCUTILS_LOG_DEBUG_NAMED(
      kLogger, "Reading $schema attachment for ReductStore entry '%s'", entry.name.c_str());
    std::optional<std::string> schema;
    std::optional<reduct::Error> read_error;
    const auto error = bucket_->Read(entry.name + "/$meta", std::nullopt,
        [&schema, &read_error](const reduct::IBucket::ReadableRecord & record) {
          const auto key = record.labels.find("key");
          if (key == record.labels.end() || key->second != "$schema") {
            return true;
          }
          const auto payload = record.ReadAll();
          if (payload.error) {
            read_error = payload.error;
            return false;
          }
          schema = payload.result;
          return true;
      });
    if (error || read_error) {
      const auto & read_failure = error ? error : *read_error;
      RCUTILS_LOG_DEBUG_NAMED(
        kLogger, "Failed to read $schema attachment for ReductStore entry '%s': %s",
          entry.name.c_str(),
        read_failure.ToString().c_str());
      throw std::runtime_error("Failed to read schema for ReductStore entry '" + entry.name +
          "': " +
                               read_failure.ToString());
    }
    if (!schema) {
      RCUTILS_LOG_WARN_NAMED(
        kLogger, "Skipping ReductStore entry '%s' because it has no $schema attachment",
          entry.name.c_str());
      continue;
    }
    RCUTILS_LOG_DEBUG_NAMED(
      kLogger, "ReductStore entry '%s' attachment '$schema': %s", entry.name.c_str(),
        schema->c_str());

    const auto topic_name = schema_member(*schema, "topic");
    const auto topic_type = schema_member(*schema, "schema_name");
    const auto serialization_format = schema_member(*schema, "encoding");
    const auto definition = schema_member(*schema, "schema");
    rosbag2_storage::TopicMetadata topic;
    topic.name = topic_name;
    topic.type = topic_type;
    topic.serialization_format = serialization_format;
    message_definitions_.insert_or_assign(
      topic.name,
        rosbag2_storage::MessageDefinition{topic_type, serialization_format, definition, ""});
    topics_.insert_or_assign(topic.name, topic);
    RCUTILS_LOG_DEBUG_NAMED(
      kLogger, "Discovered ReductStore topic '%s' with type '%s'", topic.name.c_str(),
        topic.type.c_str());
    topics.push_back(std::move(topic));
  }
  return topics;
}

void ReductStoreStorage::get_all_message_definitions(
  std::vector<rosbag2_storage::MessageDefinition> & definitions)
{
  RCUTILS_LOG_DEBUG_NAMED(kLogger, "get_all_message_definitions called");
  if (message_definitions_.empty() && bucket_) {
    get_all_topics_and_types();
  }

  std::vector<std::string> topic_names;
  topic_names.reserve(message_definitions_.size());
  for (const auto & [topic_name, definition] : message_definitions_) {
    static_cast<void>(definition);
    topic_names.push_back(topic_name);
  }
  std::sort(topic_names.begin(), topic_names.end());

  definitions.clear();
  definitions.reserve(topic_names.size());
  for (const auto & topic_name : topic_names) {
    definitions.push_back(message_definitions_.at(topic_name));
  }
}

rosbag2_storage::BagMetadata ReductStoreStorage::get_metadata()
{
  RCUTILS_LOG_DEBUG_NAMED(kLogger, "get_metadata called");
  if (!bucket_) {
    throw std::runtime_error("ReductStore bucket is not initialized; call open() first");
  }

  const auto bucket_info = bucket_->GetInfo();
  if (bucket_info.error) {
    throw std::runtime_error("Failed to get ReductStore bucket metadata: " +
                             bucket_info.error.ToString());
  }
  const auto entries_result = bucket_->GetEntryList();
  if (entries_result.error) {
    throw std::runtime_error("Failed to list ReductStore entries: " +
                             entries_result.error.ToString());
  }
  get_all_topics_and_types();

  auto metadata = metadata_;
  metadata.storage_identifier = get_storage_identifier();
  metadata.bag_size = bucket_info.result.size;
  metadata.relative_file_paths = {get_relative_file_path()};
  metadata.message_count = 0;
  metadata.topics_with_message_count.clear();

  for (const auto & entry : entries_result.result) {
    metadata.message_count += entry.record_count;
    const auto topic = topics_.find("/" + entry.name);
    if (topic != topics_.end()) {
      metadata.topics_with_message_count.push_back({topic->second, entry.record_count});
    }
  }

  if (metadata.message_count == 0) {
    metadata.starting_time = {};
    metadata.duration = {};
    metadata.files.clear();
    return metadata;
  }

  const auto starting_duration =
    std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
    bucket_info.result.oldest_record.time_since_epoch());
  metadata.starting_time =
    std::chrono::time_point<std::chrono::high_resolution_clock>(starting_duration);
  metadata.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
    bucket_info.result.latest_record - bucket_info.result.oldest_record);
  metadata.files = {{get_relative_file_path(), metadata.starting_time, metadata.duration,
    static_cast<size_t>(metadata.message_count)}};
  return metadata;
}

std::string ReductStoreStorage::get_relative_file_path() const {return uri_;}

uint64_t ReductStoreStorage::get_bagfile_size() const
{
  if (!bucket_) {
    return 0;
  }
  const auto bucket_info = bucket_->GetInfo();
  if (bucket_info.error) {
    throw std::runtime_error("Failed to get ReductStore bucket size: " +
        bucket_info.error.ToString());
  }
  return bucket_info.result.size;
}

std::string ReductStoreStorage::get_storage_identifier() const {return "reductstore";}

uint64_t ReductStoreStorage::get_minimum_split_file_size() const {return 0;}

void ReductStoreStorage::set_filter(const rosbag2_storage::StorageFilter & storage_filter)
{
  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "set_filter called: topic_count=%zu, first_topic='%s', service_event_count=%zu, "
    "regex='%s', excluded_topic_count=%zu, first_excluded_topic='%s', "
    "excluded_service_event_count=%zu, excluded_regex='%s'",
    storage_filter.topics.size(),
    storage_filter.topics.empty() ? "" : storage_filter.topics.front().c_str(),
    storage_filter.services_events.size(), storage_filter.regex.c_str(),
    storage_filter.exclude_topics.size(),
    storage_filter.exclude_topics.empty() ? "" : storage_filter.exclude_topics.front().c_str(),
    storage_filter.exclude_service_events.size(), storage_filter.regex_to_exclude.c_str());
  filter_ = storage_filter;
}

void ReductStoreStorage::reset_filter()
{
  RCUTILS_LOG_DEBUG_NAMED(kLogger, "reset_filter called");
  filter_ = {};
}

void ReductStoreStorage::seek(const rcutils_time_point_value_t & timestamp)
{
  if (timestamp == std::numeric_limits<rcutils_time_point_value_t>::max()) {
    seek_timestamp_.reset();
    RCUTILS_LOG_DEBUG_NAMED(kLogger, "seek called with unbounded playback timestamp");
    start_query();
    return;
  }

  const auto rounded_timestamp = std::chrono::ceil<std::chrono::microseconds>(
    std::chrono::nanoseconds(timestamp));
  seek_timestamp_ = reduct::IClient::Time(rounded_timestamp);
  RCUTILS_LOG_DEBUG_NAMED(
    kLogger,
    "seek called: requested_timestamp_ns=%" PRId64 ", rounded_timestamp_us=%" PRId64,
    static_cast<int64_t>(timestamp),
    static_cast<int64_t>(rounded_timestamp.count()));
  start_query();
}

}  // namespace rosbag2_storage_reductstore

PLUGINLIB_EXPORT_CLASS(rosbag2_storage_reductstore::ReductStoreStorage,
                       rosbag2_storage::storage_interfaces::ReadWriteInterface)
