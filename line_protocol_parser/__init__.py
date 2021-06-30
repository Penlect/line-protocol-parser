"""Module for parsing InfluxDB line protocol strings"""
from ._line_protocol_parser import parse_line, LineFormatError

# Module metadata
__author__ = 'Daniel Andersson'
__maintainer__ = __author__
__email__ = 'daniel.4ndersson@gmail.com'
__contact__ = __email__
__copyright__ = 'Copyright (c) 2019 Daniel Andersson'
__license__ = 'MIT'
__url__ = "https://github.com/Penlect/line-protocol-parser"
__version__ = '1.1.1'
