# rosbag2_storage_reductstore

ROS 2 Jazzy `rosbag2` storage plugin for ReductStore.

## Requirements

- Ubuntu 24.04
- ROS 2 Jazzy
- `ros-jazzy-ament-cmake-vendor-package` and `python3-vcstool`
- ReductCPP build dependencies: `libconcurrentqueue-dev`, `libcpp-httplib-dev`, `libfmt-dev`, `libssl-dev`, and `nlohmann-json3-dev`. The bundled `reductcpp_vendor` package fetches and builds reductcpp v1.21.2. The backend requires ReductCPP 1.19 or later for attachment APIs.

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

The plugin identifier is `reductstore`. Configuration is a flat YAML connection file:

```yaml
url: http://localhost:8383
api_token_env: REDUCTSTORE_API_TOKEN
max_records_per_batch: 80
```

- `url` is required.
- `api_token` is optional and supplies a token directly. Do not commit plaintext credentials.
- `api_token_env` is optional and names an environment variable holding the token.
- Omitting both token fields uses anonymous access.
- Supplying both token fields, an empty URL, or a missing/empty environment variable is an error. Credential values are never included in errors.
- `max_records_per_batch` is optional, defaults to `80`, and controls the maximum number of messages sent in each write request.

The example configuration is installed as `config/reductstore.yaml.example`.

Each ReductStore bucket is one remote bag. Standard `ros2 bag record` receives a local URI, so it derives the bucket name from that URI and retains the local output directory and `metadata.yaml`.

## Data Model

ReductStore stores each serialized ROS message as a raw record in an entry named for its ROS topic. The message type definition and serialization metadata are stored as an entry-level `$schema` attachment, keeping the schema adjacent to the topic entry that owns it.

## Remote Playback

`ros2 reductstore play` takes a ReductStore bucket directly. The bucket is not a local path, and the connection configuration remains a local YAML file:

```bash
ros2 reductstore play <bucket> \
  --storage-config-file reductstore.yaml \
  --topics /chatter
```

The command supports playback rate, topic filtering, QoS override files, looping, remapping, start-paused mode, start offsets, acknowledgement waiting, and log-level selection. No local directory or `metadata.yaml` named after `<bucket>` is required.

Remote-only recording is constrained by rosbag2 Jazzy's recorder, which always creates a local output directory and `metadata.yaml`. Use the standard command until rosbag2 provides a remote-writer path:

```bash
ros2 bag record --storage reductstore --storage-config-file reductstore.yaml \
  --output ./local-bag --topics /chatter
```

Bagfile size and duration splitting, snapshot mode, and append mode are intentionally unsupported and fail during `open()`.

## Tests

```bash
source /opt/ros/jazzy/setup.bash
REDUCTSTORE_URL=http://127.0.0.1:8383 \
  colcon test --packages-select rosbag2_storage_reductstore
colcon test-result --verbose
```
