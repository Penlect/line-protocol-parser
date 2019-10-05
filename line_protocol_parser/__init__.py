"""Module for parsing InfluxDB line protocol strings"""
from ._line_protocol_parser import parse_line, LineFormatError

from ._info import (
    __version__,
    __author__,
    __maintainer__,
    __email__,
    __status__,
    __url__,
    __license__
)
