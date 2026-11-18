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

"""Remote ReductStore playback command."""

from argparse import FileType
import signal
import threading

from rclpy.qos import InvalidQoSProfileException
from ros2bag.api import convert_service_to_service_event_topic
from ros2bag.api import convert_yaml_to_qos_profile
from ros2bag.api import print_error
from ros2bag.verb.play import PlayVerb as RosbagPlayVerb
from ros2cli.node import NODE_NAME_PREFIX
from rosbag2_py import Player
from rosbag2_py import PlayOptions
from rosbag2_py import ServiceRequestsSource
from rosbag2_py import StorageOptions
from rosbag2_storage_reductstore_cli.options import build_storage_options
import yaml

termination_requested = threading.Event()


def signal_handler(signum, _):
    """Request playback shutdown after a termination signal."""
    termination_requested.set()


class PlayVerb(RosbagPlayVerb):
    """Play messages from a remote ReductStore bucket."""

    @staticmethod
    def remove_argument(parser, destination):
        """Remove an upstream parser argument invalid for remote playback."""
        action = next(action for action in parser._actions if action.dest == destination)
        for option in action.option_strings:
            del parser._option_string_actions[option]
        parser._remove_action(action)
        for group in parser._action_groups:
            if action in group._group_actions:
                group._group_actions.remove(action)

    def add_arguments(self, parser, cli_name):
        """Register remote playback arguments."""
        super().add_arguments(parser, cli_name)
        for destination in ('bag_path', 'input', 'storage', 'storage_config_file'):
            self.remove_argument(parser, destination)
        parser.add_argument('bucket', help='Remote ReductStore bucket to play.')
        parser.add_argument(
            '--storage-config-file',
            required=True,
            type=FileType('r'),
            help='Path to the ReductStore connection configuration YAML file.',
        )

    def main(self, *, args):
        """Configure and run remote playback."""
        qos_profile_overrides = {}
        if args.qos_profile_overrides_path:
            qos_profile_dict = yaml.safe_load(args.qos_profile_overrides_path)
            try:
                qos_profile_overrides = convert_yaml_to_qos_profile(qos_profile_dict)
            except (InvalidQoSProfileException, ValueError) as error:
                return print_error(str(error))

        topic_remapping = ['--ros-args']
        for remap_rule in args.remap:
            topic_remapping.extend(('--remap', remap_rule))

        play_options = PlayOptions()
        play_options.read_ahead_queue_size = args.read_ahead_queue_size
        play_options.node_prefix = NODE_NAME_PREFIX
        play_options.rate = args.rate
        play_options.topics_to_filter = args.topics
        play_options.services_to_filter = convert_service_to_service_event_topic(args.services)
        play_options.regex_to_filter = args.regex
        play_options.exclude_regex_to_filter = args.exclude_regex
        play_options.exclude_service_events_to_filter = convert_service_to_service_event_topic(
            args.exclude_services
        )
        play_options.topic_qos_profile_overrides = qos_profile_overrides
        play_options.loop = args.loop
        play_options.topic_remapping_options = topic_remapping
        play_options.clock_publish_frequency = args.clock
        exclude_topics = args.exclude_topics if args.exclude_topics else []
        if play_options.clock_publish_frequency > 0:
            exclude_topics.append('/clock')
        play_options.exclude_topics_to_filter = exclude_topics
        if args.clock_topics_all or args.clock_topics:
            play_options.clock_publish_on_topic_publish = True
        play_options.clock_topics = args.clock_topics
        play_options.delay = args.delay
        play_options.playback_duration = args.playback_duration
        play_options.playback_until_timestamp = self.get_playback_until_from_arg_group(
            args.playback_until_sec,
            args.playback_until_nsec,
        )
        play_options.disable_keyboard_controls = args.disable_keyboard_controls
        play_options.start_paused = args.start_paused
        play_options.start_offset = args.start_offset
        play_options.wait_acked_timeout = args.wait_for_all_acked
        play_options.disable_loan_message = args.disable_loan_message
        play_options.publish_service_requests = args.publish_service_requests
        if args.service_requests_source == 'client_introspection':
            play_options.service_requests_source = ServiceRequestsSource.CLIENT_INTROSPECTION
        else:
            play_options.service_requests_source = ServiceRequestsSource.SERVICE_INTROSPECTION

        player = Player(
            [
                build_storage_options(
                    args.bucket,
                    args.storage_config_file.name,
                    StorageOptions,
                )
            ],
            play_options,
            args.log_level,
        )
        signal.signal(signal.SIGTERM, signal_handler)
        try:
            player.start_spin()
            player.play()
            while not termination_requested.is_set():
                if player.wait_for_playback_to_finish_exclusively(0.1):
                    break
        except KeyboardInterrupt:
            pass
        finally:
            player.stop()
            player.stop_spin()
            signal.signal(signal.SIGTERM, signal.SIG_DFL)
            termination_requested.clear()
