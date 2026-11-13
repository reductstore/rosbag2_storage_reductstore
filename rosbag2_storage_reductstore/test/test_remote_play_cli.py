# Copyright (c) 2026 ReductStore
# MIT License
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""Regression coverage for remote ReductStore playback."""

import configparser
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
import uuid

sys.path.insert(0, str(Path(__file__).parents[1] / 'python'))

from rosbag2_storage_reductstore_cli.options import build_storage_options  # noqa: E402


class StorageOptions:
    """Capture storage options passed to the factory."""

    def __init__(self, **kwargs):
        """Store keyword arguments as option attributes."""
        self.__dict__.update(kwargs)


class RemotePlayStorageOptionsTest(unittest.TestCase):
    """Exercise remote playback CLI behavior."""

    @staticmethod
    def write_config(directory):
        """Write a ReductStore connection config into a directory."""
        config_path = Path(directory) / 'reductstore.yaml'
        config = f"url: {os.environ['REDUCTSTORE_URL']}\n"
        if api_token := os.environ.get('REDUCTSTORE_API_TOKEN'):
            config += f'api_token: {api_token}\n'
        config_path.write_text(config, encoding='utf-8')
        return config_path

    @staticmethod
    def delete_bucket(bucket):
        """Delete a test bucket on the ReductStore server."""
        request = urllib.request.Request(
            f"{os.environ['REDUCTSTORE_URL']}/api/v1/b/{bucket}",
            method='DELETE',
        )
        if api_token := os.environ.get('REDUCTSTORE_API_TOKEN'):
            request.add_header('Authorization', f'Bearer {api_token}')
        urllib.request.urlopen(request).close()

    def test_passes_remote_bucket_directly_to_reductstore_storage(self):
        """Pass the bucket directly to ReductStore storage."""
        options = build_storage_options(
            'remote-only-bucket',
            '/tmp/reductstore.yaml',
            StorageOptions,
        )

        self.assertEqual('remote-only-bucket', options.uri)
        self.assertEqual('reductstore', options.storage_id)
        self.assertEqual('/tmp/reductstore.yaml', options.storage_config_uri)

    def test_registers_the_reductstore_command_and_play_verb(self):
        """Expose the ReductStore command and its play verb."""
        config = configparser.ConfigParser()
        config.read(Path(__file__).parents[1] / 'setup.cfg')
        entry_points = config['options.entry_points']

        self.assertIn(
            'reductstore = rosbag2_storage_reductstore_cli.command:ReductStoreCommand',
            entry_points['ros2cli.command'],
        )
        self.assertIn(
            'play = rosbag2_storage_reductstore_cli.play:PlayVerb',
            entry_points['rosbag2_storage_reductstore_cli.verb'],
        )

    @unittest.skipUnless(
        os.environ.get('REDUCTSTORE_URL'),
        'REDUCTSTORE_URL is required for remote playback integration coverage.',
    )
    def test_plays_a_remote_bucket_without_a_local_directory(self):
        """Play a remote bucket without creating a same-named directory."""
        with tempfile.TemporaryDirectory() as directory:
            config_path = self.write_config(directory)
            bucket = f'remote-only-{uuid.uuid4().hex}'
            result = subprocess.run(
                [
                    'ros2',
                    'reductstore',
                    'play',
                    bucket,
                    '--storage-config-file',
                    str(config_path),
                ],
                capture_output=True,
                cwd=directory,
                text=True,
            )

            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertFalse((Path(directory) / bucket).exists())

    @unittest.skipUnless(
        os.environ.get('REDUCTSTORE_URL'),
        'REDUCTSTORE_URL is required for remote playback integration coverage.',
    )
    def test_plays_recorded_messages_from_a_remote_bucket(self):
        """Play messages written through the reductstore storage plugin."""
        import rclpy
        from rclpy.qos import qos_profile_sensor_data
        from rclpy.serialization import serialize_message
        from rosbag2_py import ConverterOptions
        from rosbag2_py import SequentialWriter
        from rosbag2_py import StorageOptions as RosbagStorageOptions
        from rosbag2_py import TopicMetadata
        from std_msgs.msg import String

        bucket = f'remote-play-{uuid.uuid4().hex}'
        with tempfile.TemporaryDirectory() as directory:
            config_path = self.write_config(directory)
            previous_directory = os.getcwd()
            try:
                os.chdir(directory)
                writer = SequentialWriter()
                writer.open(
                    RosbagStorageOptions(
                        uri=bucket,
                        storage_id='reductstore',
                        storage_config_uri=str(config_path),
                    ),
                    ConverterOptions(),
                )
                writer.create_topic(
                    TopicMetadata(
                        id=0,
                        name='/chatter',
                        type='std_msgs/msg/String',
                        serialization_format='cdr',
                    )
                )
                now_ns = time.time_ns()
                for offset in range(20):
                    writer.write(
                        '/chatter',
                        serialize_message(String(data='hello-reductstore')),
                        now_ns + offset * 100_000_000,
                    )
                writer.close()
            finally:
                os.chdir(previous_directory)

            rclpy.init()
            node = rclpy.create_node('remote_play_test_subscriber')
            received = []
            node.create_subscription(
                String,
                '/chatter',
                lambda message: received.append(message.data),
                qos_profile_sensor_data,
            )
            spin_thread = threading.Thread(target=rclpy.spin, args=(node,), daemon=True)
            spin_thread.start()
            try:
                result = subprocess.run(
                    [
                        'ros2',
                        'reductstore',
                        'play',
                        bucket,
                        '--storage-config-file',
                        str(config_path),
                    ],
                    capture_output=True,
                    cwd=directory,
                    text=True,
                    timeout=30,
                )
            finally:
                node.destroy_node()
                rclpy.shutdown()
                self.delete_bucket(bucket)

            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn('hello-reductstore', received)


if __name__ == '__main__':
    unittest.main()
