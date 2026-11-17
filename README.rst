line-protocol-parser
====================

Parse one line of InfluxDB line protocol into a Python dictionary. The
parser targets the archived `InfluxDB v1.2 specification`_. You can read
points from a file or a byte stream without connecting to InfluxDB.

Installation
------------

Install from PyPI in a Python 3.10 or newer environment:

.. code-block:: bash

    python3 -m pip install line-protocol-parser

If a wheel is available for your Python version and platform, pip uses it.
Building from source requires a C compiler and Python development headers.
See the `files on PyPI`_ for available wheels. To install from a source
checkout, run ``python3 -m pip install .`` in the repository root.

Parse a point
-------------

Pass a single point as a ``str`` or UTF-8 ``bytes``. Each point has four
keys:

.. code-block:: pycon

    >>> from line_protocol_parser import parse_line
    >>> point = parse_line("weather,site=roof temp=18.5,samples=3i,online=true 123")
    >>> point["measurement"]
    'weather'
    >>> point["tags"]
    {'site': 'roof'}
    >>> point["fields"]["temp"], point["fields"]["samples"], point["fields"]["online"]
    (18.5, 3, True)
    >>> point["time"]
    123

Tag values are always strings. Field values follow their line-protocol
types. For example, ``3`` gives the float ``3.0``, ``3i`` and ``3u`` give
the integer ``3``, and ``"3"`` gives the string ``'3'``.

A point without tags has an empty tags dictionary. An omitted timestamp
gives ``None`` in the dictionary, and an explicit timestamp of zero stays
zero. A comment line makes the whole call return ``None``:

.. code-block:: pycon

    >>> point = parse_line(b"weather temp=18")
    >>> point["tags"], point["time"]
    ({}, None)
    >>> parse_line("weather temp=18 0")["time"]
    0
    >>> parse_line("# exported by the roof sensor") is None
    True

Escaping and quoted strings
---------------------------

Use a backslash to escape a space in a measurement name, a comma in a tag
value, or a double quote inside a quoted string field. The Python raw
string passes those backslashes through to the parser.

.. code-block:: pycon

    >>> point = parse_line(
    ...     r'weather\ station,site=north\,roof note="fan is \"on\""'
    ... )
    >>> point["measurement"], point["tags"]["site"]
    ('weather station', 'north,roof')
    >>> point["fields"]["note"]
    'fan is "on"'

Read points from a file
-----------------------

For example, a file named ``weather.lp`` could contain::

    # roof sensor
    weather,site=roof temp=18.5

    weather,site=roof temp=19.0

Iterate over the file one line at a time. Blank lines raise
``line_protocol_parser.LineFormatError``, so this example skips them before
parsing. It also skips comments and reports malformed points with their
line numbers.

.. code-block:: python

    from line_protocol_parser import LineFormatError, parse_line

    with open("weather.lp", encoding="utf-8") as source:
        for line_number, line in enumerate(source, start=1):
            if not line.strip():
                continue
            try:
                point = parse_line(line)
            except LineFormatError as error:
                raise ValueError(f"weather.lp line {line_number}: {error}") from error
            if point is not None:
                print(point["fields"]["temp"])

This prints ``18.5`` and ``19.0`` on separate lines.

Protocol compatibility
----------------------

The parser also accepts unsigned integer fields such as ``3u``. The combined
measurement and tag key is limited to 65,535 bytes. The Python API accepts
comment lines with leading spaces or tabs and rejects embedded NUL bytes
with ``LineFormatError``.

Starting in version 2.0.2, if the same field key appears more than once in a
point, the last value wins.

.. code-block:: pycon

    >>> parse_line("weather samples=2i,samples=3i")["fields"]["samples"]
    3

C API and development
---------------------

For standalone C use, compile the `parser source`_ with the `public
header`_. Pass a NUL-terminated string to ``LP_parse_line()`` and release
each returned point with ``LP_free_point()``. On failure, the function
returns ``NULL`` and sets the status code. Filter comment lines before
calling the C API.

See the `parser tests`_ for more input and output examples and the
`GitHub releases`_ for version history.

.. _files on PyPI: https://pypi.org/project/line-protocol-parser/#files
.. _InfluxDB v1.2 specification: https://archive.docs.influxdata.com/influxdb/v1.2/write_protocols/line_protocol_reference/
.. _parser source: https://github.com/Penlect/line-protocol-parser/blob/master/src/line_protocol_parser.c
.. _public header: https://github.com/Penlect/line-protocol-parser/blob/master/include/line_protocol_parser.h
.. _parser tests: https://github.com/Penlect/line-protocol-parser/blob/master/tests/test_parse_line.py
.. _GitHub releases: https://github.com/Penlect/line-protocol-parser/releases
