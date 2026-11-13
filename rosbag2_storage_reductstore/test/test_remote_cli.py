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

import argparse
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
from rosbag2_storage_reductstore_cli.record import RecordVerb  # noqa: E402


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

    @staticmethod
    def bucket_exists(bucket):
        """Return whether a test bucket exists on the ReductStore server."""
        request = urllib.request.Request(
            f"{os.environ['REDUCTSTORE_URL']}/api/v1/b/{bucket}",
            method='HEAD',
        )
        if api_token := os.environ.get('REDUCTSTORE_API_TOKEN'):
            request.add_header('Authorization', f'Bearer {api_token}')
        try:
            urllib.request.urlopen(request).close()
            return True
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return False
            raise

    @staticmethod
    def record_parser():
        """Build the remote recorder parser."""
        parser = argparse.ArgumentParser()
        RecordVerb().add_arguments(parser, 'ros2 reductstore record')
        return parser

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

    def test_record_help_excludes_unsupported_options(self):
        """Do not expose local output, storage, or compression options."""
        result = subprocess.run(
            ['ros2', 'reductstore', 'record', '--help'],
            capture_output=True,
            text=True,
        )

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertNotIn('--output', result.stdout)
        self.assertNotIn('--storage ', result.stdout)
        self.assertNotIn('--compression-', result.stdout)

    def test_record_parser_rejects_unsafe_bucket_name(self):
        """Reject a bucket name that can escape the temporary workspace."""
        with self.assertRaises(SystemExit) as error:
            self.record_parser().parse_args(['../outside', '--topics', '/chatter'])

        self.assertEqual(2, error.exception.code)

    def test_record_parser_rejects_compression_options(self):
        """Reject compression until remote playback supports it."""
        with self.assertRaises(SystemExit) as error:
            self.record_parser().parse_args(
                ['bucket', '--topics', '/chatter', '--compression-mode', 'message']
            )

        self.assertEqual(2, error.exception.code)

    @unittest.skipUnless(
        os.environ.get('REDUCTSTORE_URL'),
        'REDUCTSTORE_URL is required for remote playback integration coverage.',
    )
    def test_rejects_a_missing_remote_bucket_without_creating_it(self):
        """Fail playback when a remote bucket does not exist."""
        with tempfile.TemporaryDirectory() as directory:
            config_path = self.write_config(directory)
            bucket = f'remote-only-{uuid.uuid4().hex}'
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
                )

                self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertFalse(self.bucket_exists(bucket))
                self.assertFalse((Path(directory) / bucket).exists())
            finally:
                self.delete_bucket(bucket)

    @unittest.skipUnless(
        os.environ.get('REDUCTSTORE_URL'),
        'REDUCTSTORE_URL is required for remote recording integration coverage.',
    )
    def test_rejects_recording_to_an_existing_remote_bucket(self):
        """Fail a second recording attempt before it can mix remote data."""
        bucket = f'remote-existing-{uuid.uuid4().hex}'
        with tempfile.TemporaryDirectory() as directory:
            config_path = self.write_config(directory)
            recorder = None
            duplicate = None
            try:
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
                self.wait_for(
                    lambda: self.bucket_exists(bucket),
                    'the first remote recorder to create its bucket',
                )
                recorder.send_signal(signal.SIGINT)
                output, _ = recorder.communicate(timeout=30)
                self.assertEqual(0, recorder.returncode, output)

                duplicate = subprocess.Popen(
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
                self.wait_for(
                    lambda: duplicate.poll() is not None,
                    'the duplicate remote recorder to reject the existing bucket',
                )
                output, _ = duplicate.communicate(timeout=30)
                self.assertNotEqual(0, duplicate.returncode, output)
            finally:
                if recorder and recorder.poll() is None:
                    recorder.send_signal(signal.SIGINT)
                    recorder.communicate(timeout=30)
                if duplicate and duplicate.poll() is None:
                    duplicate.send_signal(signal.SIGINT)
                    duplicate.communicate(timeout=30)
                self.delete_bucket(bucket)

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
    def test_records_and_plays_to_a_remapped_topic_without_a_local_directory(self):
        """Record and replay messages through a remote bucket without local output."""
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
            player = None
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
                node.create_subscription(
                    String,
                    '/replayed_chatter',
                    lambda message: received.append(message.data),
                    qos_profile_sensor_data,
                )
                player = subprocess.Popen(
                    [
                        'ros2',
                        'reductstore',
                        'play',
                        bucket,
                        '--storage-config-file',
                        str(config_path),
                        '--remap',
                        '/chatter:=/replayed_chatter',
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    cwd=directory,
                    text=True,
                )

                def replayed_message():
                    if 'hello-reductstore' in received:
                        return True
                    if player.poll() is None:
                        return False
                    self.fail(player.communicate()[0])

                self.wait_for(replayed_message, 'a replayed remapped message')
                output, _ = player.communicate(timeout=30)
                self.assertEqual(0, player.returncode, output)
            finally:
                if recorder.poll() is None:
                    recorder.send_signal(signal.SIGINT)
                    recorder.communicate(timeout=30)
                if player and player.poll() is None:
                    player.send_signal(signal.SIGINT)
                    player.communicate(timeout=30)
                node.destroy_node()
                rclpy.shutdown()
                self.delete_bucket(bucket)


if __name__ == '__main__':
    unittest.main()
