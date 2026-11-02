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

#ifndef ROSBAG2_STORAGE_REDUCTSTORE__REDUCTSTORE_STORAGE_HPP_
#define ROSBAG2_STORAGE_REDUCTSTORE__REDUCTSTORE_STORAGE_HPP_

#include <reduct/client.h>

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "rosbag2_storage/storage_interfaces/read_write_interface.hpp"
#include "rosbag2_storage_reductstore/config.hpp"
#include "rosbag2_storage_reductstore/visibility_control.hpp"

namespace rosbag2_storage_reductstore {

class ROSBAG2_STORAGE_REDUCTSTORE_PUBLIC ReductStoreStorage
    : public rosbag2_storage::storage_interfaces::ReadWriteInterface {
 public:
  ReductStoreStorage() = default;
  ~ReductStoreStorage() override;

  void open(const rosbag2_storage::StorageOptions& storage_options,
            rosbag2_storage::storage_interfaces::IOFlag io_flag =
                rosbag2_storage::storage_interfaces::IOFlag::READ_WRITE) override;
  void write(std::shared_ptr<const rosbag2_storage::SerializedBagMessage> msg) override;
  void write(const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>>& msg) override;
  void update_metadata(const rosbag2_storage::BagMetadata& bag_metadata) override;
  void create_topic(const rosbag2_storage::TopicMetadata& topic,
                    const rosbag2_storage::MessageDefinition& message_definition) override;
  void remove_topic(const rosbag2_storage::TopicMetadata& topic) override;
  bool set_read_order(const rosbag2_storage::ReadOrder& read_order) override;
  bool has_next() override;
  std::shared_ptr<rosbag2_storage::SerializedBagMessage> read_next() override;
  std::vector<rosbag2_storage::TopicMetadata> get_all_topics_and_types() override;
  void get_all_message_definitions(std::vector<rosbag2_storage::MessageDefinition>& definitions) override;
  rosbag2_storage::BagMetadata get_metadata() override;
  std::string get_relative_file_path() const override;
  uint64_t get_bagfile_size() const override;
  std::string get_storage_identifier() const override;
  uint64_t get_minimum_split_file_size() const override;
  void set_filter(const rosbag2_storage::StorageFilter& storage_filter) override;
  void reset_filter() override;
  void seek(const rcutils_time_point_value_t& timestamp) override;

 private:
  struct CachedRecord {
    std::string entry;
    reduct::IClient::Time timestamp;
    std::string data;
  };

  void write_messages(
    const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>>& messages);
  void write_batch(
    const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>>& messages,
    size_t begin,
    size_t end);
  void add_message_to_batch(
    reduct::IBucket::Batch& batch,
    const rosbag2_storage::SerializedBagMessage& message) const;
  std::vector<std::string> filtered_entry_names() const;
  void start_query();
  void stop_query();

  ReductStoreConfig config_;
  std::string bucket_name_;
  std::string uri_;
  std::unique_ptr<reduct::IClient> client_;
  std::unique_ptr<reduct::IBucket> bucket_;
  std::unique_ptr<reduct::IBucket> query_bucket_;

  rosbag2_storage::BagMetadata metadata_;
  std::unordered_map<std::string, rosbag2_storage::TopicMetadata> topics_;
  std::unordered_map<std::string, rosbag2_storage::MessageDefinition> message_definitions_;
  rosbag2_storage::StorageFilter filter_;
  std::optional<reduct::IClient::Time> seek_timestamp_;
  std::jthread query_thread_;
  std::mutex query_mutex_;
  std::condition_variable query_condition_;
  std::deque<CachedRecord> records_;
  std::exception_ptr query_error_;
  bool query_finished_ = true;
};

}  // namespace rosbag2_storage_reductstore

#endif  // ROSBAG2_STORAGE_REDUCTSTORE__REDUCTSTORE_STORAGE_HPP_
