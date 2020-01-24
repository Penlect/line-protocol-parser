"""Test parse_line"""

# Built-in imports
import unittest

# Project
from line_protocol_parser import parse_line, LineFormatError


class TestPoint(unittest.TestCase):
    """Test Point parsing and behavior"""

    def test_from_line_bytes(self):
        _ = parse_line(b'foobar,t0=0,t1=1 f0=0,f1=1 0')

    def test_from_line(self):
        for _ in range(100):
            p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0')
            params = dict(
                measurement='foobar',
                tags=dict(t0='0', t1='1'),
                fields=dict(f0=0.0, f1=1.0),
                time=0)
            self.assertDictEqual(p, params)

    def test_from_line_measurement(self):
        p = parse_line('foobar,tag1=1 f1=0 1234')
        self.assertEqual(p['measurement'], 'foobar')

    def test_from_line_measurement_escape(self):
        p = parse_line('f\\ \\,\\=\\"\\oobar,tag1=1 f1=0 1234')
        self.assertEqual(p['measurement'], 'f ,="\\oobar')

    def test_from_line_measurement_without_tags(self):
        p = parse_line('foobar f1=0 1234')
        self.assertEqual(p['measurement'], 'foobar')
        self.assertDictEqual(p['tags'], dict())

    # TEST TAG KEYS
    def test_from_line_tag_keys(self):
        p = parse_line('foobar,ta\\ \\,\\=\\"\\g1=1,tag2=2 f1=0 1234')
        self.assertTrue('ta ,=\\"\\g1' in p['tags'])

    # TEST TAG VALUES
    def test_from_line_tag_values(self):
        p = parse_line('foobar,tag1=A\\ \\,\\=\\"\\B,tag2="\\ " f1=0 0')
        self.assertEqual(p['tags']['tag1'], 'A ,=\\"\\B')
        self.assertEqual(p['tags']['tag2'], '" "')

    # TEST FIELD KEYS
    def test_from_line_field_keys(self):
        p = parse_line('foobar field\\ \\,\\=\\"\\1=1,field2=2 1234')
        self.assertTrue('field ,="\\1' in p['fields'])

    # TEST FIELD VALUES
    def test_from_line_field_values_float(self):
        p = parse_line('foobar,tag1=1 f1=3.14 0')
        self.assertAlmostEqual(p['fields']['f1'], 3.14)

    def test_from_line_field_values_integer(self):
        p = parse_line('foobar,tag1=1 f1=123i 0')
        self.assertAlmostEqual(p['fields']['f1'], 123)

    def test_from_line_field_values_big_integer(self):
        p = parse_line('foobar,tag1=1 f1=15758827520i 0')
        self.assertAlmostEqual(p['fields']['f1'], 15_758_827_520)

    def test_from_line_field_values_string(self):
        p = parse_line('foobar,tag1=1 f1="MelodiesOfLife" 0')
        self.assertAlmostEqual(p['fields']['f1'], "MelodiesOfLife")

    def test_from_line_field_values_boolean_true(self):
        p = parse_line('foobar,tag1=1 f1=t 0')
        self.assertAlmostEqual(p['fields']['f1'], True)
        p = parse_line('foobar,tag1=1 f1=true 0')
        self.assertAlmostEqual(p['fields']['f1'], True)
        p = parse_line('foobar,tag1=1 f1=True 0')
        self.assertAlmostEqual(p['fields']['f1'], True)
        p = parse_line('foobar,tag1=1 f1=TRUE 0')
        self.assertAlmostEqual(p['fields']['f1'], True)

    def test_from_line_field_values_boolean_false(self):
        p = parse_line('foobar,tag1=1 f1=f 0')
        self.assertAlmostEqual(p['fields']['f1'], False)
        p = parse_line('foobar,tag1=1 f1=false 0')
        self.assertAlmostEqual(p['fields']['f1'], False)
        p = parse_line('foobar,tag1=1 f1=False 0')
        self.assertAlmostEqual(p['fields']['f1'], False)
        p = parse_line('foobar,tag1=1 f1=FALSE 0')
        self.assertAlmostEqual(p['fields']['f1'], False)

    def test_from_line_field_values_whitespace(self):
        p = parse_line('foobar,tag1=1 f1="with space" 0')
        self.assertEqual(p['fields']['f1'], "with space")

    def test_from_line_field_values_escape(self):
        p = parse_line('foobar,tag1=1 f1="\\ \\"\\,\\=" 0')
        self.assertEqual(p['fields']['f1'], '\\ \\"\\,\\=')

    # TEST TIME
    def test_from_line_time(self):
        p = parse_line('foobar,tag1=1 f1=0 1134871200000000007')
        self.assertAlmostEqual(p['time'], 1134871200000000007)

    # TEST MULTILINE
    def test_newline(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\nABC')
        params = dict(
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=0)
        self.assertDictEqual(p, params)

    # TEST MULTILINE
    def test_carriage_return(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\r\nABC')
        params = dict(
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=0)
        self.assertDictEqual(p, params)

    # TEST ERRORS
    def test_empty_line_error(self):
        with self.assertRaisesRegex(LineFormatError, 'empty string'):
            parse_line('')

    def test_measurement_error(self):
        with self.assertRaisesRegex(LineFormatError, 'measurement'):
            parse_line('measurement')

    def test_tag_key_error(self):
        with self.assertRaisesRegex(LineFormatError, 'key of tag'):
            parse_line('measurement,tag')

    def test_tag_value_error(self):
        with self.assertRaisesRegex(LineFormatError, 'value of tag'):
            parse_line('measurement,tag=value')

    def test_field_key_error(self):
        with self.assertRaisesRegex(LineFormatError, 'key of field'):
            parse_line('measurement,tag=value field')

    def test_field_value_error(self):
        with self.assertRaisesRegex(LineFormatError, 'value of field'):
            parse_line('measurement,tag=value field=1.23')

    def test_field_value_type_error(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('measurement,tag=value field=hej 123')

    def test_time_error(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('measurement,tag=value field=1.23 time')

    def test_type_error(self):
        with self.assertRaises(TypeError):
            parse_line(123)

    def test_no_argument_error(self):
        with self.assertRaises(TypeError):
            parse_line()


if __name__ == '__main__':
    unittest.main()
