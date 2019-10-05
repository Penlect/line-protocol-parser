Welcome to InfluxDB line-protocol-parser
========================================

Parse InfluxDB `line protocol`_ strings into Python dictionaries.

Example:
^^^^^^^^

.. code-block:: python

    >>> from line_protocol_parser import parse_line
    >>> data = parse_line('myMeas,someTag=ABC field1=3.14,field2="Hello, World!" 123')
    >>> print(data)
    {'measurement': 'myMeas',
    'fields': {'field1': 3.14, 'field2': 'Hello, World!'},
    'tags': {'someTag': 'ABC'},
    'time': 123}


**The InfluxDB line protocol is a text based format for writing points to InfluxDB.
This project can read this format and convert line strings to Python dicitonaries.**

The line protocol has the following format:

    <measurement>[,<tag_key>=<tag_value>[,<tag_key>=<tag_value>]] <field_key>=<field_value>[,<field_key>=<field_value>] [<timestamp>]

and is documented here: `InfluxDB line protocol`_.

The ``line_protocol_parser`` module only contains the ``parse_line`` function and the ``LineFormatError`` exception which is raised on failure.

Installation
^^^^^^^^^^^^
From PyPI:

.. code-block:: bash

    $ python3 -m pip install line-protocol-parser

or from source (make sure you have ``python3 -m pip install wheel setuptools`` first):

.. code-block:: bash

    $ git clone https://github.com/Penlect/line-protocol-parser.git
    $ cd line-protocol-parser
    $ python3 setup.py bdist_wheel
    $ python3 -m pip install ./dist/line-protocol-parser-*.whl

or from generated Debian package:

.. code-block:: bash

    # Install build dependencies
    $ sudo apt install python3-all python3-all-dev python3-setuptools dh-python
    $ git clone https://github.com/Penlect/line-protocol-parser.git
    $ cd line-protocol-parser
    $ make deb
    $ sudo apt install ./python3-line-protocol-parser_*.deb

Use Case 1: Read points from a file
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Suppose you have a text file with influxDB measurement points, "my_influxDB_points.txt"::

    myMeasurement,someTag=A temperature=37.0 1570977942581909918
    myMeasurement,someTag=A temperature=37.3 1570977942581910000
    myMeasurement,someTag=A temperature=36.9 1570977942581912345
    myMeasurement,someTag=A temperature=37.1 1570977942581923399
    ...

Then you can load each line into a dicitonary to be printed like this:

.. code-block:: python3

    >>> from line_protocol_parser import parse_line
    >>> with open('my_influxDB_points.txt', 'r') as f_obj:
    ...     for line in f_obj:
    ...         print(parse_line(line))


Use Case 2: InfluxDB subscriptions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
InfluxDB subscriptions are documented here: `InfluxDB Subscriptions`_.

InfluxDB subscriptions are local or remote endpoints to which all data written to InfluxDB is copied. Endpoint able to accept UDP, HTTP, or HTTPS connections can subscribe to InfluxDB and receive a copy of all data as it is written.

In this example we will do the following:

1) Setup and run a InfluxDB container.
2) Create a subscription.
3) Create a Python server and register it as an endpoint.
4) Use ``line_protocol_parser`` to read and print incoming data.

**Step 1**. Run the following commands to run a `InfluxDB container`_ and attach to the influx client.

.. code-block:: bash

   $ docker run -d --network="host" --name inf influxdb
   $ docker exec -it inf influx


**Step 2**. Create subscription. Run these commands in the influx client prompt.


.. code-block:: bash

   > CREATE DATABASE mydb
   > USE mydb
   > CREATE SUBSCRIPTION "mysub" ON "mydb"."autogen" DESTINATIONS ALL 'http://localhost:9090'

Since we used `--network="host"` we can use localhost from inside the container.

**Step 3 & 4**. Python server to receive InfluxDB data.

Create a python file *server.py* with the following content:

.. code-block:: python

    from pprint import pprint
    from http.server import HTTPServer, BaseHTTPRequestHandler
    from line_protocol_parser import parse_line

    class PostHandler(BaseHTTPRequestHandler):

        def do_POST(self):
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            pprint(parse_line(post_data))
            self.send_response(200)
            self.end_headers()

    if __name__ == '__main__':
        server = HTTPServer(('localhost', 9090), PostHandler)
        print('Starting server, use <Ctrl-C> to stop')
        server.serve_forever()


Start the server:

.. code-block:: bash

   $ python3 server.py
   Starting server, use <Ctrl-C> to stop


Next, go back to your influx client and insert a data point:

.. code-block:: bash

   > INSERT oven,room=kitchen temperature=225.0 1234567890

Head back to your Python server and watch the output:

.. code-block:: bash

   $ python3 server.py
   Starting server, use <Ctrl-C> to stop
   {'fields': {'temperature': 225.0},
    'measurement': 'oven',
    'tags': {'room': 'kitchen'},
    'time': 1234567890}
   172.17.0.2 - - [14/Oct/2019 21:02:57] "POST /write?consistency=&db=mydb&precision=ns&rp=autogen HTTP/1.1" 200 -


Pure C usage
^^^^^^^^^^^^
If you are not interested in the Python wrapper you may find the pure-c files useful:

* ``include/line_protocol_parser.h``
* ``src/line_protocol_parser.c``

Example:

.. code-block:: c

    int main()
    {
        const char *line = "measurement,tag=value field=\"Hello, world!\" 1570283407262541159";
        struct LP_Point *point;
        int status = 0;
        point = LP_parse_line(line, &status);
        if (point == NULL) {
            LP_DEBUG_PRINT("ERROR STATUS: %d\n", status);
        }
        // < Do something useful with point here >
        LP_free_point(point);
        return status;
    }

Please see the comments in the source and header file for more information.

Examples from the Test Cases
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
The test cases are a good source of examples. Please see: `tests/test_parse_line.py <tests/test_parse_line.py>`_.

Changelog
^^^^^^^^^
The changelog is maintained in the debian directory, please check there: `changelog <debian/changelog>`_.

.. _line protocol: https://docs.influxdata.com/influxdb/latest/write_protocols/line_protocol_reference/
.. _InfluxDB line protocol: https://docs.influxdata.com/influxdb/latest/write_protocols/line_protocol_reference/
.. _InfluxDB Subscriptions: https://docs.influxdata.com/influxdb/latest/administration/subscription-management/
.. _InfluxDB container: https://hub.docker.com/_/influxdb
