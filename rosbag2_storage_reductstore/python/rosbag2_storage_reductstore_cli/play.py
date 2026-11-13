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
from ros2bag.api import check_not_negative_float
from ros2bag.api import check_not_negative_int
from ros2bag.api import check_positive_float
from ros2bag.api import convert_yaml_to_qos_profile
from ros2bag.api import print_error
from ros2bag.verb import VerbExtension
from ros2cli.node import NODE_NAME_PREFIX
from rosbag2_py import Player
from rosbag2_py import PlayOptions
from rosbag2_py import StorageOptions
from rosbag2_storage_reductstore_cli.options import build_storage_options
import yaml

termination_requested = threading.Event()


def signal_handler(signum, _):
    """Request playback shutdown after a termination signal."""
    termination_requested.set()


class PlayVerb(VerbExtension):
    """Play messages from a remote ReductStore bucket."""

    def add_arguments(self, parser, cli_name):
        """Register remote playback arguments."""
        parser.add_argument('bucket', help='Remote ReductStore bucket to play.')
        parser.add_argument(
            '--storage-config-file',
            required=True,
            type=FileType('r'),
            help='Path to the ReductStore connection configuration YAML file.',
        )
        parser.add_argument(
            '--read-ahead-queue-size',
            default=1000,
            type=int,
            help='Number of messages held in memory to support deterministic playback.',
        )
        parser.add_argument(
            '-r',
            '--rate',
            default=1.0,
            type=check_positive_float,
            help='Playback rate. Valid range is greater than zero.',
        )
        parser.add_argument(
            '--topics',
            default=[],
            metavar='topic',
            nargs='+',
            help='Space-delimited list of topics to play.',
        )
        parser.add_argument(
            '--qos-profile-overrides-path',
            type=FileType('r'),
            help='Path to a YAML file defining topic QoS overrides.',
        )
        parser.add_argument(
            '-l',
            '--loop',
            action='store_true',
            help='Restart playback at the beginning after reaching the end.',
        )
        parser.add_argument(
            '--remap',
            '-m',
            default=[],
            nargs='+',
            help='Topic remapping rules in the form old_topic:=new_topic.',
        )
        parser.add_argument(
            '-p',
            '--start-paused',
            action='store_true',
            help='Start playback paused.',
        )
        parser.add_argument(
            '--start-offset',
            default=0.0,
            type=check_not_negative_float,
            help='Offset in seconds from the first message.',
        )
        parser.add_argument(
            '--wait-for-all-acked',
            default=-1,
            metavar='TIMEOUT',
            type=check_not_negative_int,
            help='Wait for acknowledgements before playback terminates.',
        )
        parser.add_argument(
            '--log-level',
            choices=['debug', 'info', 'warn', 'error', 'fatal'],
            default='info',
            help='Logging level.',
        )

    def main(self, *, args):
        """Configure and run remote playback."""
        qos_profile_overrides = {}
        if args.qos_profile_overrides_path:
            try:
                qos_profile_overrides = convert_yaml_to_qos_profile(
                    yaml.safe_load(args.qos_profile_overrides_path)
                )
            except (InvalidQoSProfileException, ValueError) as error:
                return print_error(str(error))

        play_options = PlayOptions()
        play_options.read_ahead_queue_size = args.read_ahead_queue_size
        play_options.node_prefix = NODE_NAME_PREFIX
        play_options.rate = args.rate
        play_options.topics_to_filter = args.topics
        play_options.topic_qos_profile_overrides = qos_profile_overrides
        play_options.loop = args.loop
        play_options.topic_remapping_options = [
            '--ros-args',
            *[
                option
                for rule in args.remap
                for option in ('--remap', rule)
            ],
        ]
        play_options.start_paused = args.start_paused
        play_options.start_offset = args.start_offset
        play_options.wait_acked_timeout = args.wait_for_all_acked

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
