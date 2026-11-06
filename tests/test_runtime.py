"""Exercise crash, locale, resource-use, and standalone C regressions.

Run crash-prone inputs and locale changes in child processes. Compile the C
harness when a compiler is available, then run its deterministic allocation
failure and comparison-count checks. No external Python packages are needed.
"""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest


def decimal_comma_locale():
    """Find a usable numeric locale without changing the test runner's locale."""
    result = subprocess.run(
        [sys.executable, '-c', '''
import locale
import os
for name in filter(None, (
    os.environ.get('LP_TEST_LOCALE'), 'sv_SE.UTF-8', 'sv_SE.utf8',
    'de_DE.UTF-8', 'fr_FR.UTF-8', 'Swedish_Sweden.1252',
    'German_Germany.1252',
)):
    try:
        locale.setlocale(locale.LC_NUMERIC, name)
    except locale.Error:
        continue
    if locale.localeconv()['decimal_point'] == ',':
        print(name)
        break
else:
    raise SystemExit(77)
'''], capture_output=True, text=True, check=False,
    )
    if result.returncode == 77:
        raise unittest.SkipTest('no decimal-comma locale; set LP_TEST_LOCALE/LOCPATH')
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout.strip()


class TestRuntime(unittest.TestCase):
    def test_invalid_utf8_field_values_raise_without_crashing(self):
        result = subprocess.run(
            [sys.executable, '-c', '''
try:
    import resource
except ImportError:
    pass
else:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
from line_protocol_parser import parse_line
for line in (
    b'm f="\\xff"', b'm f="\\xc0\\xaf"', b'm f="\\xed\\xa0\\x80"',
    b'm f="\\xff",f="ok"', b'm f="ok",f="\\xff"',
):
    try:
        parse_line(line)
    except UnicodeDecodeError:
        pass
    else:
        raise AssertionError(repr(line))
'''], capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_float_parsing_does_not_depend_on_or_change_numeric_locale(self):
        name = decimal_comma_locale()
        result = subprocess.run(
            [sys.executable, '-c', '''
import locale
import sys
from line_protocol_parser import parse_line, LineFormatError
locale.setlocale(locale.LC_NUMERIC, sys.argv[1])
before = locale.setlocale(locale.LC_NUMERIC)
for token in ('1.5', '.25', '1.', '-2.5e-3', '5e-324', '1e3'):
    assert parse_line('m f=' + token)['fields']['f'] == float(token)
for token in ('1e400', '1e-400', '+1.5', 'nan', '0x1.8p1'):
    try:
        parse_line('m f=' + token)
    except LineFormatError:
        pass
    else:
        raise AssertionError(token)
assert locale.setlocale(locale.LC_NUMERIC) == before
assert locale.localeconv()['decimal_point'] == ','
''', name], capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class TestStandaloneParser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get('CC') or
                               sysconfig.get_config_var('CC') or 'cc')
        if not shutil.which(compiler[0]):
            raise unittest.SkipTest('no C compiler available for standalone tests')
        cls.directory = tempfile.TemporaryDirectory(prefix='TMP_lpp_tests_')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / 'TMP_test_parser'
        root = Path(__file__).resolve().parent.parent
        source = root / 'tests' / 'test_parser.c'
        if Path(compiler[0]).name.lower() in ('cl', 'cl.exe'):
            cls.binary = cls.binary.with_suffix('.exe')
            command = compiler + ['/nologo', '/std:c11', '/W4',
                                  '/I' + str(root / 'include'), str(source),
                                  '/Fe:' + str(cls.binary)]
        else:
            command = compiler + ['-std=c11', '-Wall', '-Wextra',
                                  '-Werror=implicit-function-declaration',
                                  '-I', str(root / 'include'), str(source),
                                  '-o', str(cls.binary), '-lm']
        result = subprocess.run(command, cwd=cls.directory.name,
                                capture_output=True, text=True, check=False)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, *args):
        result = subprocess.run([str(self.binary), *args],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_allocation_failures_release_every_allocation(self):
        self.run_case('allocations')

    def test_tag_validation_has_bounded_comparison_cost(self):
        self.run_case('tags')

    def test_standalone_numeric_locale(self):
        self.run_case('locale', decimal_comma_locale())
