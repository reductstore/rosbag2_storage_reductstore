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

import datetime
from pathlib import Path
import tempfile

from ros2bag.verb.record import RecordVerb as RosbagRecordVerb


class RecordVerb(RosbagRecordVerb):
    """Record ROS data directly into a remote ReductStore bucket."""

    @staticmethod
    def remove_argument(parser, destination):
        """Remove an upstream parser argument invalid for remote recording."""
        action = next(action for action in parser._actions if action.dest == destination)
        for option in action.option_strings:
            del parser._option_string_actions[option]
        parser._remove_action(action)
        parser._optionals._group_actions.remove(action)

    def add_arguments(self, parser, cli_name):
        """Register remote recording arguments."""
        parser.add_argument(
            'bucket',
            nargs='?',
            help='Remote ReductStore bucket to record, defaults to a timestamped bucket.',
        )
        super().add_arguments(parser, cli_name)
        self.remove_argument(parser, 'output')
        self.remove_argument(parser, 'storage')
        parser.set_defaults(storage='reductstore')

    def main(self, *, args):
        """Record through a temporary upstream writer workspace."""
        bucket = args.bucket or datetime.datetime.now().strftime('rosbag2_%Y_%m_%d-%H_%M_%S')
        with tempfile.TemporaryDirectory(prefix='rosbag2-reductstore-') as directory:
            args.output = str(Path(directory) / bucket)
            return super().main(args=args)
