#include "rosbag2_storage_reductstore/config.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace rosbag2_storage_reductstore
{
namespace
{

std::string scalar_value(const YAML::Node & node, const char * name, bool required)
{
  if (!node) {
    if (required) {
      throw std::runtime_error(std::string("ReductStore configuration requires '") + name + "'");
    }
    return "";
  }
  if (!node.IsScalar()) {
    throw std::runtime_error(std::string("ReductStore configuration field '") + name + "' must be a scalar");
  }

  const auto value = node.as<std::string>();
  if (required && value.empty()) {
    throw std::runtime_error(std::string("ReductStore configuration field '") + name + "' must not be empty");
  }
  return value;
}

bool is_safe_bucket_name(const std::string & value)
{
  if (value.empty() || !std::isalnum(static_cast<unsigned char>(value.front()))) {
    return false;
  }
  for (const auto character : value) {
    if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_' &&
      character != '.')
    {
      return false;
    }
  }
  return true;
}

}  // namespace

ReductStoreConfig load_config(const std::string & path)
{
  if (path.empty()) {
    throw std::runtime_error("ReductStore storage configuration path is required");
  }

  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception & error) {
    throw std::runtime_error("Unable to load ReductStore configuration '" + path + "': " + error.what());
  }
  if (!root.IsMap()) {
    throw std::runtime_error("ReductStore configuration must be a YAML map");
  }

  ReductStoreConfig config;
  try {
    config.url = scalar_value(root["url"], "url", true);
    const auto direct_token_node = root["api_token"];
    const auto token_environment_node = root["api_token_env"];
    const auto direct_token = scalar_value(direct_token_node, "api_token", false);
    const auto token_environment = scalar_value(token_environment_node, "api_token_env", false);
    if (direct_token_node && token_environment_node) {
      throw std::runtime_error("ReductStore configuration cannot set both api_token and api_token_env");
    }
    if (token_environment_node) {
      if (token_environment.empty()) {
        throw std::runtime_error("ReductStore configuration field 'api_token_env' must not be empty");
      }
      const char * token = std::getenv(token_environment.c_str());
      if (token == nullptr || token[0] == '\0') {
        throw std::runtime_error("ReductStore API token environment variable is missing or empty");
      }
      config.api_token = token;
    } else {
      config.api_token = direct_token;
    }
  } catch (const YAML::Exception & error) {
    throw std::runtime_error("Invalid ReductStore configuration: " + std::string(error.what()));
  }
  return config;
}

std::string bucket_name_from_uri(const std::string & uri)
{
  if (uri.empty()) {
    throw std::runtime_error("ReductStore storage URI must not be empty");
  }

  const std::filesystem::path path(uri);
  const auto filename = path.filename().string();
  std::string bucket = filename;
  const auto parent_name = path.parent_path().filename().string();
  if (!parent_name.empty() && filename == parent_name + "_0") {
    bucket = parent_name;
  }
  if (!is_safe_bucket_name(bucket)) {
    throw std::runtime_error("ReductStore bucket name derived from storage URI is unsafe");
  }
  return bucket;
}

}  // namespace rosbag2_storage_reductstore
