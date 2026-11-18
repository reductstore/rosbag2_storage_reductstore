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

"""Remote ReductStore recording command."""

from argparse import ArgumentTypeError
from argparse import FileType
import datetime
from pathlib import Path
import re
import signal
import tempfile
import threading
import time

from rclpy.qos import InvalidQoSProfileException
from ros2bag.api import convert_service_to_service_event_topic
from ros2bag.api import convert_yaml_to_qos_profile
from ros2bag.api import print_error
from ros2bag.api import print_warn
from ros2bag.verb import VerbExtension
from ros2cli.node import NODE_NAME_PREFIX
from rosbag2_py import Recorder
from rosbag2_py import RecordOptions
from rosbag2_py import StorageOptions
import yaml

termination_requested = threading.Event()


def validate_bucket_name(bucket):
    """Reject names unsafe for a ReductStore bucket or temporary directory."""
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', bucket):
        raise ArgumentTypeError(f'Invalid ReductStore bucket name: {bucket}')
    return bucket


def validate_recording_arguments(args):
    """Validate the supported remote recorder filters."""
    if not (
        args.all or args.all_topics or args.all_services or args.services or args.topics or
        args.topic_types or args.regex
    ):
        return print_error(
            'Need to specify at least one option out of --all, --all-topics, --all-services, '
            '--services, --topics, --topic-types or --regex'
        )
    if args.exclude_regex and not (
        args.all or args.all_topics or args.topic_types or args.all_services or args.regex
    ):
        return print_error(
            '--exclude-regex argument requires either --all, --all-topics, --topic-types, '
            '--all-services or --regex'
        )
    if args.exclude_topics and not (args.all or args.all_topics or args.topic_types or args.regex):
        return print_error(
            '--exclude-topics argument requires either --all, --all-topics, --topic-types or '
            '--regex'
        )
    if args.exclude_topic_types and not (
        args.all or args.all_topics or args.topic_types or args.regex
    ):
        return print_error(
            '--exclude-topic-types argument requires either --all, --all-topics or --regex'
        )
    if args.exclude_services and not (args.all or args.all_services or args.regex):
        return print_error(
            '--exclude-services argument requires either --all, --all-services or --regex'
        )
    if (args.all or args.all_services) and args.services:
        print(print_warn('--all or --all-services will override --services'), flush=True)
    if (args.all or args.all_topics) and args.topics:
        print(print_warn('--all or --all-topics will override --topics'), flush=True)
    if (args.all or args.all_topics or args.all_services) and args.regex:
        print(
            print_warn('--all, --all-topics or --all-services will override --regex'),
            flush=True,
        )
    if args.use_sim_time and args.no_discovery:
        return print_error(
            '--use-sim-time and --no-discovery both set, but are incompatible settings. '
            'The /clock topic needs to be discovered to record with sim time.'
        )
    return None


def signal_handler(signum, _):
    """Request recorder shutdown after a termination signal."""
    termination_requested.set()


class RecordVerb(VerbExtension):
    """Record ROS data directly into a remote ReductStore bucket."""

    def add_arguments(self, parser, cli_name):
        """Register remote recording arguments."""
        parser.add_argument(
            'bucket',
            nargs='?',
            type=validate_bucket_name,
            help='Remote ReductStore bucket to record, defaults to a timestamped bucket.',
        )
        parser.add_argument(
            '--topics',
            type=str,
            default=[],
            metavar='Topic',
            nargs='+',
            help='Space-delimited list of topics to record.',
        )
        parser.add_argument(
            '--services',
            type=str,
            default=[],
            metavar='ServiceName',
            nargs='+',
            help='Space-delimited list of services to record.',
        )
        parser.add_argument(
            '--topic-types',
            type=str,
            default=[],
            metavar='TopicType',
            nargs='+',
            help='Space-delimited list of topic types to record.',
        )
        parser.add_argument(
            '-a',
            '--all',
            action='store_true',
            help='Record all topics and services, excluding hidden topics.',
        )
        parser.add_argument(
            '--all-topics',
            action='store_true',
            help='Record all topics, excluding hidden topics.',
        )
        parser.add_argument(
            '--all-services',
            action='store_true',
            help='Record all services via service event topics.',
        )
        parser.add_argument(
            '-e',
            '--regex',
            default='',
            help='Record only topics and services matching a regular expression.',
        )
        parser.add_argument(
            '--exclude-regex',
            default='',
            help='Exclude topics and services matching a regular expression.',
        )
        parser.add_argument(
            '--exclude-topic-types',
            type=str,
            default=[],
            metavar='ExcludeTopicTypes',
            nargs='+',
            help='Space-delimited list of topic types not to record.',
        )
        parser.add_argument(
            '--exclude-topics',
            type=str,
            default=[],
            metavar='Topic',
            nargs='+',
            help='Space-delimited list of topics not to record.',
        )
        parser.add_argument(
            '--exclude-services',
            type=str,
            default=[],
            metavar='ServiceName',
            nargs='+',
            help='Space-delimited list of services not to record.',
        )
        parser.add_argument(
            '--include-unpublished-topics',
            action='store_true',
            help='Discover and record topics that have no publisher.',
        )
        parser.add_argument(
            '--include-hidden-topics',
            action='store_true',
            help='Discover and record hidden topics.',
        )
        parser.add_argument(
            '--no-discovery',
            action='store_true',
            help='Disable automatic topic discovery during recording.',
        )
        parser.add_argument(
            '-p',
            '--polling-interval',
            type=int,
            default=100,
            help='Milliseconds between topic-discovery queries.',
        )
        parser.add_argument(
            '--ignore-leaf-topics',
            action='store_true',
            help='Ignore topics without a subscription.',
        )
        parser.add_argument(
            '--qos-profile-overrides-path',
            type=FileType('r'),
            help='Path to a YAML file defining topic QoS overrides.',
        )
        parser.add_argument(
            '--max-cache-size',
            type=int,
            default=100 * 1024 * 1024,
            help='Maximum bytes of messages held in each recorder cache buffer.',
        )
        parser.add_argument(
            '--disable-keyboard-controls',
            action='store_true',
            help='Disable recorder keyboard controls.',
        )
        parser.add_argument(
            '--start-paused',
            action='store_true',
            help='Start recording paused.',
        )
        parser.add_argument(
            '--use-sim-time',
            action='store_true',
            help='Use simulation time for message timestamps.',
        )
        parser.add_argument(
            '--log-level',
            choices=['debug', 'info', 'warn', 'error', 'fatal'],
            default='info',
            help='Logging level.',
        )
        parser.add_argument(
            '--storage-config-file',
            required=True,
            type=FileType('r'),
            help='Path to the ReductStore connection configuration YAML file.',
        )

    def main(self, *, args):
        """Record through a temporary upstream writer workspace."""
        error = validate_recording_arguments(args)
        if error:
            return error

        qos_profile_overrides = {}
        if args.qos_profile_overrides_path:
            try:
                qos_profile_overrides = convert_yaml_to_qos_profile(
                    yaml.safe_load(args.qos_profile_overrides_path)
                )
            except (InvalidQoSProfileException, ValueError) as error:
                return print_error(str(error))

        bucket = args.bucket or datetime.datetime.now().strftime('rosbag2_%Y_%m_%d-%H_%M_%S')
        with tempfile.TemporaryDirectory(prefix='rosbag2-reductstore-') as directory:
            storage_options = StorageOptions(
                uri=str(Path(directory) / bucket),
                storage_id='reductstore',
                max_bagfile_size=0,
                max_bagfile_duration=0,
                max_cache_size=args.max_cache_size,
                storage_preset_profile='',
                storage_config_uri=args.storage_config_file.name,
                snapshot_mode=False,
                custom_data={},
            )
            record_options = RecordOptions()
            record_options.all_topics = args.all_topics or args.all
            record_options.all_services = args.all_services or args.all
            record_options.is_discovery_disabled = args.no_discovery
            record_options.topics = args.topics
            record_options.topic_types = args.topic_types
            record_options.services = convert_service_to_service_event_topic(args.services)
            record_options.exclude_topic_types = args.exclude_topic_types
            record_options.rmw_serialization_format = ''
            record_options.topic_polling_interval = datetime.timedelta(
                milliseconds=args.polling_interval
            )
            record_options.regex = args.regex
            record_options.exclude_regex = args.exclude_regex
            record_options.exclude_topics = args.exclude_topics
            record_options.exclude_service_events = convert_service_to_service_event_topic(
                args.exclude_services
            )
            record_options.node_prefix = NODE_NAME_PREFIX
            record_options.compression_mode = 'NONE'
            record_options.compression_format = ''
            record_options.compression_queue_size = 0
            record_options.compression_threads = 0
            record_options.compression_threads_priority = 0
            record_options.topic_qos_profile_overrides = qos_profile_overrides
            record_options.include_hidden_topics = args.include_hidden_topics
            record_options.include_unpublished_topics = args.include_unpublished_topics
            record_options.start_paused = args.start_paused
            record_options.ignore_leaf_topics = args.ignore_leaf_topics
            record_options.use_sim_time = args.use_sim_time
            record_options.disable_keyboard_controls = args.disable_keyboard_controls

            recorder = Recorder(
                storage_options,
                record_options,
                args.log_level,
                'rosbag2_reductstore_recorder',
            )
            signal.signal(signal.SIGTERM, signal_handler)
            try:
                recorder.start_spin()
                recorder.record()
                while not termination_requested.is_set():
                    time.sleep(0.1)
            except KeyboardInterrupt:
                pass
            finally:
                recorder.stop()
                recorder.stop_spin()
                signal.signal(signal.SIGTERM, signal.SIG_DFL)
                termination_requested.clear()
