# rosbag2_storage_reductstore

ROS 2 Jazzy `rosbag2` storage plugin scaffold for ReductStore.

> **Status:** This package does not implement recording, playback, remote bucket I/O, metadata persistence, or schema attachments. Every `open()` and data operation fails explicitly so it cannot silently lose messages or appear to create a valid remote bag.

## Requirements

- Ubuntu 24.04
- ROS 2 Jazzy
- `ros-jazzy-ament-cmake-vendor-package` and `python3-vcstool`
- ReductCPP build dependencies: `libconcurrentqueue-dev`, `libcpp-httplib-dev`, `libfmt-dev`, `libssl-dev`, and `nlohmann-json3-dev`. The bundled `reductcpp_vendor` package fetches and builds reductcpp v1.21.2. Attachment APIs needed by the planned backend require reductcpp 1.19 or later.

## Build

```bash
source /opt/ros/jazzy/setup.bash
colcon build --packages-up-to rosbag2_storage_reductstore \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
source install/setup.bash
```

On a minimal ROS installation, install the vendor build tools first:

```bash
sudo apt-get install ros-jazzy-ament-cmake-vendor-package python3-vcstool \
  libconcurrentqueue-dev libcpp-httplib-dev libfmt-dev libssl-dev nlohmann-json3-dev
```

## Plugin And Configuration

The plugin identifier is `reductstore`. Configuration is a flat YAML file passed through rosbag's storage configuration option:

```yaml
url: http://localhost:8383
api_token_env: REDUCTSTORE_API_TOKEN
```

- `url` is required.
- `api_token` is optional and supplies a token directly. Do not commit plaintext credentials.
- `api_token_env` is optional and names an environment variable holding the token.
- Omitting both token fields uses anonymous access.
- Supplying both token fields, an empty URL, or a missing/empty environment variable is an error. Credential values are never included in errors.

The example configuration is installed as `config/reductstore.yaml.example`.

One rosbag will map to one future ReductStore bucket. For Jazzy's usual storage URI `<bag-dir>/<bag-name>_0`, the bucket name is `<bag-name>`; other URIs use their basename. The original URI remains in local `metadata.yaml` as rosbag's relative storage path. The local rosbag directory and metadata file are retained.

## Planned Data Model

ReductStore will not receive rosbag files. The future backend will write each serialized ROS message as a raw ReductStore record, organized into entries by ROS topic. The message type definition and serialization metadata will be stored as an entry-level `$schema` attachment. This preserves message payloads as raw data while keeping the schema adjacent to the topic entry that owns it.

## Future Commands

These are the intended commands after backend I/O is implemented. They currently fail with `ReductStore storage backend is not implemented`.

```bash
ros2 bag record --storage reductstore --storage-config-file reductstore.yaml /topic
ros2 bag play my_bag --storage reductstore --storage-config-file reductstore.yaml
```

Bagfile size and duration splitting, snapshot mode, and append mode are intentionally unsupported and fail during `open()`.

## Tests

```bash
source /opt/ros/jazzy/setup.bash
colcon test --packages-select rosbag2_storage_reductstore
colcon test-result --verbose
```
