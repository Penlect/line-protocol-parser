"""Tests for the archived InfluxDB v1.2 line protocol behavior.

This suite is being expanded in checkpoints so the parser can be tightened
without losing track of public API expectations. Timestamp semantics are
covered explicitly because omitted timestamps must remain distinguishable from
an explicit ``0`` timestamp.
"""

# Built-in imports
from sys import float_info
import unittest

# Project
from line_protocol_parser import parse_line, LineFormatError


class TestPoint(unittest.TestCase):
    """Test point parsing and behavior."""

    def assert_point(self, parsed, **expected):
        """Keep dictionary comparisons compact in the tests below."""

        self.assertDictEqual(parsed, expected)

    def test_from_line_bytes(self):
        _ = parse_line(b'foobar,t0=0,t1=1 f0=0,f1=1 0')

    def test_from_comment_line_bytes(self):
        self.assertIsNone(parse_line(b'# comment'))

    def test_from_line(self):
        for _ in range(100):
            p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0')
            self.assert_point(
                p,
                measurement='foobar',
                tags=dict(t0='0', t1='1'),
                fields=dict(f0=0.0, f1=1.0),
                time=0,
            )

    def test_from_comment_line(self):
        self.assertIsNone(parse_line('# comment'))

    def test_from_comment_line_with_leading_spaces(self):
        self.assertIsNone(parse_line('   # comment'))

    def test_from_comment_line_with_leading_tab(self):
        self.assertIsNone(parse_line('\t# comment'))

    def test_from_comment_line_newline(self):
        self.assertIsNone(parse_line('# comment\n'))

    def test_from_comment_line_carriage_return_newline(self):
        self.assertIsNone(parse_line('# comment\r\n'))

    def test_from_comment_line_carriage_return(self):
        self.assertIsNone(parse_line('# comment\r'))

    def test_comment_line_with_trailing_junk_is_not_silently_ignored(self):
        with self.assertRaises(LineFormatError):
            parse_line('# comment\nABC')

    def test_embedded_nul_in_str_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'NUL'):
            parse_line('cpu value=1i 0\0ABC')

    def test_embedded_nul_in_bytes_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'NUL'):
            parse_line(b'cpu value=1i 0\x00ABC')

    def test_hash_inside_measurement_is_not_comment(self):
        p = parse_line('foo#bar field=1i 0')
        self.assert_point(
            p,
            measurement='foo#bar',
            tags={},
            fields={'field': 1},
            time=0,
        )

    def test_from_line_measurement(self):
        p = parse_line('foobar,tag1=1 f1=0 1234')
        self.assertEqual(p['measurement'], 'foobar')

    def test_from_line_measurement_escape(self):
        p = parse_line('f\\ \\,\\=\\"\\oobar,tag1=1 f1=0 1234')
        self.assertEqual(p['measurement'], 'f ,\\=\\"\\oobar')

    def test_from_line_measurement_equals_sign_is_literal(self):
        p = parse_line('cpu=load,tag1=1 f1=0 1234')
        self.assertEqual(p['measurement'], 'cpu=load')

    def test_from_line_measurement_escaped_equals_keeps_backslash(self):
        p = parse_line(r'cpu\=load,tag1=1 f1=0 1234')
        self.assertEqual(p['measurement'], r'cpu\=load')

    def test_from_line_measurement_without_tags(self):
        p = parse_line('foobar f1=0 1234')
        self.assertEqual(p['measurement'], 'foobar')
        self.assertDictEqual(p['tags'], dict())

    def test_from_line_measurement_with_literal_tab(self):
        p = parse_line('foo\tbar field=1i 0')
        self.assertEqual(p['measurement'], 'foo\tbar')

    def test_from_line_measurement_even_backslashes_before_space_is_invalid(self):
        with self.assertRaises(LineFormatError):
            parse_line(r'm\\ field=1 0')

    def test_from_line_with_leading_spaces(self):
        p = parse_line('   foobar f1=0 1234')
        self.assertEqual(p['measurement'], 'foobar')

    def test_from_line_with_multiple_spaces_before_fields(self):
        p = parse_line('foobar  f1=0 1234')
        self.assertEqual(p['fields']['f1'], 0.0)

    def test_from_line_with_tab_after_measurement_space(self):
        p = parse_line('foobar \tf1=0 1234')
        self.assertEqual(p['fields']['f1'], 0.0)

    # TEST TAG KEYS
    def test_from_line_tag_keys(self):
        p = parse_line('foobar,ta\\ \\,\\=\\"\\g1=1,tag2=2 f1=0 1234')
        self.assertTrue('ta ,=\\"\\g1' in p['tags'])

    # TEST TAG VALUES
    def test_from_line_tag_values(self):
        p = parse_line('foobar,tag1=A\\ \\,\\=\\"\\B,tag2="\\ " f1=0 0')
        self.assertEqual(p['tags']['tag1'], 'A ,=\\"\\B')
        self.assertEqual(p['tags']['tag2'], '" "')

    def test_from_line_tag_key_single_space(self):
        p = parse_line(r'cpu,\ =east value=1.0')
        self.assertEqual(p['tags'][' '], 'east')

    def test_from_line_tag_value_with_literal_tab(self):
        p = parse_line('foobar,tag1=A\tB f1=0 0')
        self.assertEqual(p['tags']['tag1'], 'A\tB')

    def test_from_line_tag_value_odd_backslashes_escape_comma(self):
        p = parse_line(r'foobar,tag1=A\\\,B,tag2=C f1=0 0')
        self.assertEqual(p['tags']['tag1'], r'A\\,B')
        self.assertEqual(p['tags']['tag2'], 'C')

    def test_from_line_tag_value_backslash_before_comma_is_still_escaped(self):
        with self.assertRaisesRegex(LineFormatError, 'value of tag'):
            parse_line(r'foobar,tag1=A\\,tag2=C f1=0 0')

    def test_from_line_tag_key_backslash_before_escaped_equals(self):
        p = parse_line(r'cpu,reg\\=ion=east value=1.0')
        self.assertEqual(p['tags'][r'reg\=ion'], 'east')

    def test_from_line_tag_value_backslash_then_escaped_space_prefix(self):
        p = parse_line(r'cpu,regions=\\ east value=1.0')
        self.assertEqual(p['tags']['regions'], r'\ east')

    def test_from_line_tag_value_backslash_then_escaped_space_middle(self):
        p = parse_line(r'cpu,regions=eas\\ t value=1.0')
        self.assertEqual(p['tags']['regions'], r'eas\ t')

    def test_from_line_tag_value_backslash_then_escaped_space_suffix(self):
        p = parse_line(r'cpu,regions=east\\  value=1.0')
        self.assertEqual(p['tags']['regions'], 'east\\ ')

    def test_from_line_tag_value_backslash_then_mixed_escaped_delimiters(self):
        p = parse_line(r'cpu,regions=\\,\,\=east value=1.0')
        self.assertEqual(p['tags']['regions'], r'\,,=east')

    def test_from_line_tag_value_even_backslashes_before_space_is_invalid(self):
        with self.assertRaises(LineFormatError):
            parse_line(r'm,tag=v\\ field=1 0')

    def test_from_line_large_number_of_tags(self):
        line = 'cpu' + ''.join(
            f',tag{i}=value{i}' for i in range(255)
        ) + ' value=1 0'
        p = parse_line(line)
        self.assertEqual(len(p['tags']), 255)
        self.assertEqual(p['tags']['tag0'], 'value0')
        self.assertEqual(p['tags']['tag254'], 'value254')

    def test_max_key_length_exact_limit_is_allowed(self):
        measurement = 'm' * 65535
        p = parse_line(f'{measurement} value=1i 0')
        self.assertEqual(len(p['measurement']), 65535)
        self.assertEqual(p['fields']['value'], 1)
        self.assertEqual(p['time'], 0)

    def test_max_key_length_exceeded_is_rejected(self):
        measurement = 'm' * 65536
        with self.assertRaisesRegex(LineFormatError, 'max key length'):
            parse_line(f'{measurement} value=1i 0')

    def test_max_key_length_exact_limit_is_allowed_with_tags(self):
        tag_value = 'v' * (65535 - len('m,tag='))
        p = parse_line(f'm,tag={tag_value} value=1i 0')
        self.assertEqual(p['tags']['tag'], tag_value)
        self.assertEqual(p['fields']['value'], 1)

    def test_max_key_length_counts_bytes_not_codepoints(self):
        measurement = 'é' * 32767  # 65534 bytes in UTF-8.
        p = parse_line(f'{measurement} value=1i 0')
        self.assertEqual(p['fields']['value'], 1)

        measurement = 'é' * 32768  # 65536 bytes in UTF-8.
        with self.assertRaisesRegex(LineFormatError, 'max key length'):
            parse_line(f'{measurement} value=1i 0')

    # TEST FIELD KEYS
    def test_from_line_field_keys(self):
        p = parse_line('foobar field\\ \\,\\=\\"\\1=1,field2=2 1234')
        self.assertTrue('field ,=\\"\\1' in p['fields'])

    def test_from_line_field_key_with_escaped_space(self):
        p = parse_line(r'cpu a\ =123i 0')
        self.assertIn('a ', p['fields'])
        self.assertEqual(p['fields']['a '], 123)

    def test_from_line_field_key_unknown_escape_keeps_backslash(self):
        p = parse_line(r'cpu \a=1i 0')
        self.assertEqual(p['fields'][r'\a'], 1)

    def test_from_line_field_key_even_backslashes_before_space_is_invalid(self):
        with self.assertRaises(LineFormatError):
            parse_line(r'm field\\ =1i 0')

    def test_from_line_field_key_even_backslashes_before_comma_is_invalid(self):
        with self.assertRaises(LineFormatError):
            parse_line(r'm field\\,other=1i 0')

    # TEST FIELD VALUES
    def test_from_line_field_values_float(self):
        p = parse_line('foobar,tag1=1 f1=3.14 0')
        self.assertAlmostEqual(p['fields']['f1'], 3.14)

    def test_from_line_field_values_float_no_leading_digit(self):
        p = parse_line('cpu value=.1 0')
        self.assertEqual(p['fields']['value'], 0.1)

    def test_from_line_field_values_float_trailing_decimal(self):
        p = parse_line('cpu value=1. 0')
        self.assertEqual(p['fields']['value'], 1.0)

    def test_from_line_field_values_float_scientific(self):
        p = parse_line('cpu value=1e4 0')
        self.assertEqual(p['fields']['value'], 1e4)
        p = parse_line('cpu value=1.0e4 0')
        self.assertEqual(p['fields']['value'], 1e4)

    def test_from_line_field_values_float_scientific_upper(self):
        p = parse_line('cpu value=1E4 0')
        self.assertEqual(p['fields']['value'], 1e4)
        p = parse_line('cpu value=1.0E4 0')
        self.assertEqual(p['fields']['value'], 1e4)

    def test_from_line_field_values_float_negative_scientific(self):
        p = parse_line('cpu value=-1.0e-4 0')
        self.assertEqual(p['fields']['value'], -1.0e-4)

    def test_from_line_field_values_float_maximum(self):
        p = parse_line(f'cpu value={float_info.max} 0')
        self.assertEqual(p['fields']['value'], float_info.max)

    def test_from_line_field_values_float_maximum_with_leading_zeros(self):
        p = parse_line(f'cpu value=0000{float_info.max} 0')
        self.assertEqual(p['fields']['value'], float_info.max)

    def test_from_line_field_values_float_minimum(self):
        p = parse_line(f'cpu value={-float_info.max} 0')
        self.assertEqual(p['fields']['value'], -float_info.max)

    def test_from_line_field_values_float_minimum_with_leading_zeros(self):
        p = parse_line(f'cpu value=-0000000{float_info.max} 0')
        self.assertEqual(p['fields']['value'], -float_info.max)

    def test_from_line_field_values_float_without_timestamp(self):
        p = parse_line('foobar,tag1=1 f1=3.14')
        self.assertAlmostEqual(p['fields']['f1'], 3.14)
        self.assertIsNone(p['time'])

    def test_from_line_field_values_integer(self):
        p = parse_line('foobar,tag1=1 f1=123i 0')
        self.assertAlmostEqual(p['fields']['f1'], 123)

    def test_from_line_field_values_uinteger(self):
        p = parse_line('foobar,tag1=1 f1=123u 0')
        self.assertAlmostEqual(p['fields']['f1'], 123)

    def test_from_line_field_values_large_uinteger(self):
        value = 2**64 - 2
        p = parse_line(f'foobar,tag1=1 f1={value}u 0')
        self.assertEqual(p['fields']['f1'], value)

    def test_from_line_field_values_integer_without_timestamp(self):
        p = parse_line('foobar,tag1=1 f1=123i')
        self.assertAlmostEqual(p['fields']['f1'], 123)
        self.assertIsNone(p['time'])

    def test_from_line_field_values_big_integer(self):
        p = parse_line('foobar,tag1=1 f1=15758827520i 0')
        self.assertAlmostEqual(p['fields']['f1'], 15758827520)

    def test_from_line_field_values_integer_minimum(self):
        p = parse_line('foobar,tag1=1 f1=-9223372036854775808i 0')
        self.assertEqual(p['fields']['f1'], -9223372036854775808)

    def test_from_line_field_values_integer_maximum(self):
        p = parse_line('foobar,tag1=1 f1=9223372036854775807i 0')
        self.assertEqual(p['fields']['f1'], 9223372036854775807)

    def test_from_line_field_values_integer_maximum_with_leading_zeros(self):
        p = parse_line('cpu value=0009223372036854775807i 0')
        self.assertEqual(p['fields']['value'], 9223372036854775807)

    def test_from_line_field_values_integer_minimum_with_leading_zeros(self):
        p = parse_line('cpu value=-0009223372036854775808i 0')
        self.assertEqual(p['fields']['value'], -9223372036854775808)

    def test_from_line_field_values_string(self):
        p = parse_line('foobar,tag1=1 f1="MelodiesOfLife" 0')
        self.assertAlmostEqual(p['fields']['f1'], "MelodiesOfLife")

    def test_from_line_field_values_string_with_commas(self):
        p = parse_line('cpu value="foo,bar" 0')
        self.assertEqual(p['fields']['value'], 'foo,bar')

    def test_from_line_field_values_string_with_equals(self):
        p = parse_line('cpu str="foo=bar",value=1.0 0')
        self.assertEqual(p['fields']['str'], 'foo=bar')
        self.assertEqual(p['fields']['value'], 1.0)

    def test_from_line_field_values_string_with_trailing_backslash(self):
        p = parse_line(r'cpu value="test\\" 0')
        self.assertEqual(p['fields']['value'], 'test\\')

    def test_from_line_field_values_double_backslash_unescapes_like_go_prefix(self):
        p = parse_line(r'cpu value="\\a" 0')
        self.assertEqual(p['fields']['value'], r'\a')

    def test_from_line_field_values_double_backslash_unescapes_like_go_middle(self):
        p = parse_line(r'cpu value="a\\\\b" 0')
        self.assertEqual(p['fields']['value'], r'a\\b')

    def test_from_line_field_values_string_unicode(self):
        p = parse_line('cpu value="wè" 0')
        self.assertEqual(p['fields']['value'], 'wè')

    def test_from_line_field_values_string_with_newline(self):
        p = parse_line(
            'cpu,host=serverA,region=us-east value=1.0,str="foo\nbar" '
            '1000000000'
        )
        self.assertEqual(p['fields']['value'], 1.0)
        self.assertEqual(p['fields']['str'], 'foo\nbar')
        self.assertEqual(p['time'], 1000000000)

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

    def test_from_line_field_values_boolean_true_mixed_case_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('cpu value=truE 0')

    def test_from_line_field_values_boolean_false_mixed_case_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('cpu value=faLSe 0')

    def test_from_line_field_values_whitespace(self):
        p = parse_line('foobar,tag1=1 f1="with space" 0')
        self.assertEqual(p['fields']['f1'], "with space")

    def test_from_line_field_values_escape(self):
        p = parse_line('foobar,tag1=1 f1="\\ \\"\\,\\=" 0')
        self.assertEqual(p['fields']['f1'], '\\ "\\,\\=')

    def test_from_line_field_values_escaped_quotes_and_commas(self):
        p = parse_line(
            r'cpu value="{Hello\"{,}\" World}" 1000000000'
        )
        self.assertEqual(p['fields']['value'], '{Hello"{,}" World}')

        p = parse_line(
            r'cpu value="{Hello\"{\,}\" World}" 1000000000'
        )
        self.assertEqual(p['fields']['value'], r'{Hello"{\,}" World}')

    def test_from_line_field_values_escaped_quote(self):
        p = parse_line(r'foobar,tag1=1 f1="a\"b" 0')
        self.assertEqual(p['fields']['f1'], 'a"b')

    def test_from_line_field_values_escaped_backslash_before_quote(self):
        p = parse_line(r'foobar,tag1=1 f1="\\\"" 0')
        self.assertEqual(p['fields']['f1'], r'\"')

    def test_from_line_field_values_trailing_backslashes_collapse_before_quote(self):
        p = parse_line(r'foobar,tag1=1 f1="\\\\" 0')
        self.assertEqual(p['fields']['f1'], r'\\')

    def test_field_value_uinteger_overflow(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('measurement f=18446744073709551616u 0')

    def test_field_value_integer_overflow(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('measurement f=9223372036854775808i 0')

    def test_field_value_integer_underflow(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('measurement f=-9223372036854775809i 0')

    # TEST TIME
    def test_from_line_time(self):
        p = parse_line('foobar,tag1=1 f1=0 1134871200000000007')
        self.assertAlmostEqual(p['time'], 1134871200000000007)

    def test_from_line_time_omitted_is_none(self):
        p = parse_line('foobar,tag1=1 f1=0')
        self.assertIsNone(p['time'])

    def test_from_line_time_zero_is_not_none(self):
        p = parse_line('foobar,tag1=1 f1=0 0')
        self.assertEqual(p['time'], 0)

    def test_from_line_time_with_multiple_spaces_before_timestamp(self):
        p = parse_line('foobar,tag1=1 f1=0   0')
        self.assertEqual(p['time'], 0)

    def test_from_line_time_with_tab_after_timestamp_space(self):
        p = parse_line('foobar,tag1=1 f1=0 \t0')
        self.assertEqual(p['time'], 0)

    def test_from_line_quoted_measurement(self):
        p = parse_line('"cpu",host=serverA,region=us-east value=1.0 1000000000')
        self.assertEqual(p['measurement'], '"cpu"')

    def test_from_line_quoted_tags(self):
        p = parse_line('cpu,"host"="serverA",region=us-east value=1.0 1000000000')
        self.assertEqual(p['tags']['"host"'], '"serverA"')
        self.assertEqual(p['tags']['region'], 'us-east')

    def test_from_line_time_negative(self):
        p = parse_line('foobar,tag1=1 f1=0 -1')
        self.assertEqual(p['time'], -1)

    def test_from_line_time_minimum_valid(self):
        p = parse_line('foobar,tag1=1 f1=0 -9223372036854775806')
        self.assertEqual(p['time'], -9223372036854775806)

    def test_from_line_time_maximum_valid(self):
        p = parse_line('foobar,tag1=1 f1=0 9223372036854775806')
        self.assertEqual(p['time'], 9223372036854775806)

    def test_from_line_time_below_minimum_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('foobar,tag1=1 f1=0 -9223372036854775807')

    def test_from_line_time_above_maximum_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('foobar,tag1=1 f1=0 9223372036854775807')

    # TEST MULTILINE
    def test_newline(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\n')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=0,
        )

    # TEST MULTILINE
    def test_carriage_return(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\r\n')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=0,
        )

    def test_carriage_return_only(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\r')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=0,
        )

    def test_newline_without_timestamp(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1\n')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=None,
        )

    def test_carriage_return_without_timestamp(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1\r')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=None,
        )

    def test_trailing_spaces_after_timestamp(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0   ')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=0,
        )

    def test_trailing_spaces_without_timestamp(self):
        p = parse_line('foobar,t0=0,t1=1 f0=0,f1=1   ')
        self.assert_point(
            p,
            measurement='foobar',
            tags=dict(t0='0', t1='1'),
            fields=dict(f0=0.0, f1=1.0),
            time=None,
        )

    def test_trailing_tab_after_timestamp_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\t')

    def test_trailing_tab_without_timestamp_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('foobar,t0=0,t1=1 f0=0,f1=1\t')

    def test_newline_with_trailing_junk_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\nABC')

    def test_carriage_return_with_trailing_junk_is_rejected(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('foobar,t0=0,t1=1 f0=0,f1=1 0\r\nABC')

    def test_tag_value_error_newline_in_tag(self):
        with self.assertRaisesRegex(LineFormatError, 'value of tag'):
            parse_line('m,t=a\nb f=1 0')

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

    def test_tag_key_error_unescaped_space(self):
        with self.assertRaisesRegex(LineFormatError, 'key of tag'):
            parse_line('measurement,bad key=value field=1i 0')

    def test_tag_key_error_unescaped_comma(self):
        with self.assertRaisesRegex(LineFormatError, 'key of tag'):
            parse_line('measurement,bad,key=value field=1i 0')

    def test_tag_key_error_empty(self):
        with self.assertRaisesRegex(LineFormatError, 'key of tag'):
            parse_line('m,=v f=1')

    def test_tag_value_error(self):
        with self.assertRaisesRegex(LineFormatError, 'value of tag'):
            parse_line('measurement,tag=value')

    def test_tag_value_error_empty(self):
        with self.assertRaisesRegex(LineFormatError, 'value of tag'):
            parse_line('m,k= f=1')

    def test_tag_value_error_unescaped_equals(self):
        with self.assertRaisesRegex(LineFormatError, 'value of tag'):
            parse_line('m,k=fo=o f=1')

    def test_duplicate_tag_key_error(self):
        with self.assertRaisesRegex(LineFormatError, 'key of tag'):
            parse_line('m,t=a,t=b f=1')

    def test_duplicate_tag_key_error_unsorted(self):
        with self.assertRaisesRegex(LineFormatError, 'key of tag'):
            parse_line('cpu,b=2,c=3,b=1 value=1i 0')

    def test_field_key_error(self):
        with self.assertRaisesRegex(LineFormatError, 'key of field'):
            parse_line('measurement,tag=value field')

    def test_field_key_error_unescaped_space(self):
        with self.assertRaisesRegex(LineFormatError, 'key of field'):
            parse_line('measurement bad key=1i,other=2i 0')

    def test_field_key_error_unescaped_comma(self):
        with self.assertRaisesRegex(LineFormatError, 'key of field'):
            parse_line('measurement bad,key=1i,other=2i 0')

    def test_field_key_error_empty(self):
        with self.assertRaisesRegex(LineFormatError, 'key of field'):
            parse_line('m =1')

    def test_field_value_error(self):
        with self.assertRaisesRegex(LineFormatError, 'value of field'):
            parse_line('measurement,tag=value field=')

    def test_field_value_error_empty_before_time(self):
        with self.assertRaisesRegex(LineFormatError, 'value of field'):
            parse_line('m f= 0')

    def test_field_value_error_empty_before_comma(self):
        with self.assertRaisesRegex(LineFormatError, 'value of field'):
            parse_line('m f=,g=1 0')

    def test_field_value_error_unbalanced_quotes(self):
        with self.assertRaisesRegex(LineFormatError, 'value of field'):
            parse_line('cpu,host=serverA value="test')

    def test_field_value_error_trailing_text_after_string(self):
        with self.assertRaises(LineFormatError):
            parse_line('measurement,tag=value field="abc"x 0')

    def test_field_value_type_error(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('measurement,tag=value field=hej 123')

    def test_field_value_type_error_bad_number(self):
        for line in (
            'cpu v=- 0',
            'cpu v=-i 0',
            'cpu v=-. 0',
            'cpu v=. 0',
            'cpu v=1.0i 0',
            'cpu v=1ii 0',
            'cpu v=1a 0',
            'cpu v=-e-e-e 0',
            'cpu v=42+3 0',
        ):
            with self.assertRaisesRegex(LineFormatError, 'type of field'):
                parse_line(line)

    def test_field_value_type_error_multiple_decimals(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('cpu value=1.1.1 0')

    def test_field_value_type_error_scientific_integer_invalid(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('cpu value=9ie10 0')
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('cpu value=9e10i 0')

    def test_field_value_type_error_plus_float(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=+1 0')

    def test_field_value_type_error_plus_integer(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=+1i 0')

    def test_field_value_type_error_nan(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=nan 0')

    def test_field_value_type_error_nan_mixed_case(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=nAn 0')

    def test_field_value_type_error_nan_upper_case(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=NaN 0')

    def test_field_value_type_error_inf(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=inf 0')

    def test_field_value_type_error_float_overflow_to_inf(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=1e400 0')

    def test_field_value_type_error_float_above_maximum(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line(f'm f=1{float_info.max} 0')

    def test_field_value_type_error_float_below_minimum(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line(f'm f=-1{str(-float_info.max)[1:]} 0')

    def test_field_value_type_error_negative_unsigned(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=-1u 0')

    def test_field_value_type_error_plus_unsigned(self):
        with self.assertRaisesRegex(LineFormatError, 'type of field'):
            parse_line('m f=+1u 0')

    def test_time_error(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('measurement,tag=value field=1.23 time')

    def test_time_error_plus_sign(self):
        with self.assertRaisesRegex(LineFormatError, 'nanoseconds'):
            parse_line('m f=1 +1')

    def test_type_error(self):
        with self.assertRaises(TypeError):
            parse_line(123)

    def test_no_argument_error(self):
        with self.assertRaises(TypeError):
            parse_line()


if __name__ == '__main__':
    unittest.main()
