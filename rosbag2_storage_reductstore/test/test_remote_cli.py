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

"""Regression coverage for remote ReductStore recording and playback."""

import configparser
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import uuid

sys.path.insert(0, str(Path(__file__).parents[1] / 'python'))

from rosbag2_storage_reductstore_cli.options import build_storage_options  # noqa: E402


class StorageOptions:
    """Capture storage options passed to the factory."""

    def __init__(self, **kwargs):
        """Store keyword arguments as option attributes."""
        self.__dict__.update(kwargs)


class RemoteStorageCliTest(unittest.TestCase):
    """Exercise remote recording and playback CLI behavior."""

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
        try:
            urllib.request.urlopen(request).close()
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise

    def wait_for(self, condition, description):
        """Wait for an asynchronous condition before timing out."""
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if condition():
                return
            time.sleep(0.1)
        self.fail(f'Timed out waiting for {description}.')

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

    def test_registers_the_reductstore_command_verbs(self):
        """Expose the ReductStore command with record and play verbs."""
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
        self.assertIn(
            'record = rosbag2_storage_reductstore_cli.record:RecordVerb',
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
        'REDUCTSTORE_URL is required for remote recording integration coverage.',
    )
    def test_records_to_a_timestamped_bucket_when_omitted(self):
        """Record to a timestamped remote bucket when no bucket is supplied."""
        import rclpy
        from rclpy.qos import qos_profile_sensor_data
        from std_msgs.msg import String

        bucket = None
        with tempfile.TemporaryDirectory() as directory:
            config_path = self.write_config(directory)
            rclpy.init()
            node = rclpy.create_node('remote_default_record_test')
            publisher = node.create_publisher(String, '/chatter', qos_profile_sensor_data)
            spin_thread = threading.Thread(target=rclpy.spin, args=(node,), daemon=True)
            spin_thread.start()
            recorder = subprocess.Popen(
                [
                    'ros2',
                    'reductstore',
                    'record',
                    '--storage-config-file',
                    str(config_path),
                    '--topics',
                    '/chatter',
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                cwd=directory,
                text=True,
            )
            try:
                def recorder_subscribed():
                    if recorder.poll() is not None:
                        self.fail(recorder.communicate()[0])
                    return node.count_subscribers('/chatter') > 0

                self.wait_for(recorder_subscribed, 'the default remote recorder subscription')
                publisher.publish(String(data='hello-reductstore'))
                recorder.send_signal(signal.SIGINT)
                output, _ = recorder.communicate(timeout=30)
                self.assertEqual(0, recorder.returncode, output)
                bucket_pattern = (
                    r"bucket='(rosbag2_\d{4}_\d{2}_\d{2}-\d{2}_\d{2}_\d{2})'"
                )
                match = re.search(bucket_pattern, output)
                self.assertIsNotNone(match, output)
                bucket = match.group(1)
                self.assertFalse((Path(directory) / bucket).exists())
            finally:
                if recorder.poll() is None:
                    recorder.send_signal(signal.SIGINT)
                    recorder.communicate(timeout=30)
                node.destroy_node()
                rclpy.shutdown()
                if bucket:
                    self.delete_bucket(bucket)

    @unittest.skipUnless(
        os.environ.get('REDUCTSTORE_URL'),
        'REDUCTSTORE_URL is required for remote playback integration coverage.',
    )
    def test_records_and_plays_a_remote_bucket_without_a_local_directory(self):
        """Record and play messages through a remote bucket without local output."""
        import rclpy
        from rclpy.qos import qos_profile_sensor_data
        from std_msgs.msg import String

        bucket = f'remote-record-{uuid.uuid4().hex}'
        with tempfile.TemporaryDirectory() as directory:
            config_path = self.write_config(directory)
            rclpy.init()
            node = rclpy.create_node('remote_record_test')
            received = []
            publisher = node.create_publisher(String, '/chatter', qos_profile_sensor_data)
            node.create_subscription(
                String,
                '/chatter',
                lambda message: received.append(message.data),
                qos_profile_sensor_data,
            )
            spin_thread = threading.Thread(target=rclpy.spin, args=(node,), daemon=True)
            spin_thread.start()
            recorder = subprocess.Popen(
                [
                    'ros2',
                    'reductstore',
                    'record',
                    bucket,
                    '--storage-config-file',
                    str(config_path),
                    '--topics',
                    '/chatter',
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                cwd=directory,
                text=True,
            )
            try:
                def recorder_subscribed():
                    if recorder.poll() is not None:
                        self.fail(recorder.communicate()[0])
                    return node.count_subscribers('/chatter') > 0

                self.wait_for(recorder_subscribed, 'the remote recorder subscription')
                for _ in range(20):
                    publisher.publish(String(data='hello-reductstore'))
                    time.sleep(0.1)
                recorder.send_signal(signal.SIGINT)
                output, _ = recorder.communicate(timeout=30)
                self.assertEqual(0, recorder.returncode, output)
                self.assertFalse((Path(directory) / bucket).exists())
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
                if recorder.poll() is None:
                    recorder.send_signal(signal.SIGINT)
                    recorder.communicate(timeout=30)
                node.destroy_node()
                rclpy.shutdown()
                self.delete_bucket(bucket)

            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn('hello-reductstore', received)


if __name__ == '__main__':
    unittest.main()
