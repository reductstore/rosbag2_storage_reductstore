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

"""Top-level ReductStore ROS 2 command."""

from ros2cli.command import add_subparsers_on_demand
from ros2cli.command import CommandExtension


class ReductStoreCommand(CommandExtension):
    """Work with remote ReductStore bags."""

    def add_arguments(self, parser, cli_name):
        """Register ReductStore command verbs."""
        self._subparser = parser
        add_subparsers_on_demand(
            parser,
            cli_name,
            '_verb',
            'rosbag2_storage_reductstore_cli.verb',
            required=False,
        )

    def main(self, *, parser, args):
        """Dispatch the selected ReductStore command verb."""
        if not hasattr(args, '_verb'):
            self._subparser.print_help()
            return 0
        return args._verb.main(args=args)
