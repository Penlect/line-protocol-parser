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


def msvc_environment():
    """Load the MSVC environment for the running Python's architecture."""
    architecture = 'amd64' if sys.maxsize > 2**32 else 'x86'
    vswhere = (Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))
               / 'Microsoft Visual Studio' / 'Installer' / 'vswhere.exe')
    if not vswhere.is_file():
        if shutil.which('cl'):
            return os.environ.copy()
        raise unittest.SkipTest('no MSVC installation available for standalone tests')
    result = subprocess.run(
        [str(vswhere), '-latest', '-products', '*', '-requires',
         'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
         '-property', 'installationPath', '-utf8'],
        capture_output=True, text=True, encoding='utf-8', check=True,
    )
    installation = result.stdout.strip()
    if not installation:
        raise unittest.SkipTest('no MSVC C tools available for standalone tests')
    vcvarsall = Path(installation) / 'VC' / 'Auxiliary' / 'Build' / 'vcvarsall.bat'
    # cmd.exe needs its own quoting, not subprocess's C argv list escaping.
    result = subprocess.run(
        f'cmd /d /u /c call "{vcvarsall}" {architecture} >nul && set',
        capture_output=True, text=True, encoding='utf-16-le', check=False,
    )
    if result.returncode:
        raise AssertionError('MSVC environment setup failed: ' + result.stderr)
    environment = os.environ.copy()
    for line in result.stdout.splitlines():
        name, separator, value = line.partition('=')
        if name and separator:
            environment[name.upper()] = value
    return environment


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
        environment = os.environ.copy()
        if os.name == 'nt' and not os.environ.get('CC'):
            # Match the wheel's MSVC/UCRT toolchain instead of a MinGW cc on PATH.
            compiler = ['cl']
            environment = msvc_environment()
        else:
            compiler = shlex.split(os.environ.get('CC') or
                                   sysconfig.get_config_var('CC') or 'cc')
        compiler_path = shutil.which(compiler[0], path=environment.get('PATH'))
        if not compiler_path:
            raise unittest.SkipTest('no C compiler available for standalone tests')
        # Windows executable lookup ignores PATH supplied only in the child env.
        compiler[0] = compiler_path
        cls.directory = tempfile.TemporaryDirectory(prefix='TMP_lpp_tests_')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / 'TMP_test_parser'
        root = Path(__file__).resolve().parent.parent
        source = root / 'tests' / 'test_parser.c'
        standalone_source = root / 'src' / 'line_protocol_parser.c'
        cls.compiler_environment = environment
        if Path(compiler[0]).name.lower() in ('cl', 'cl.exe'):
            cls.binary = cls.binary.with_suffix('.exe')
            flags = ['/nologo', '/std:c11', '/W4', '/fp:strict',
                     '/I' + str(root / 'include')]
            command = compiler + flags + [str(source), '/Fe:' + str(cls.binary)]
            cls.standalone_command = compiler + flags + [
                '/c', str(standalone_source),
                '/Fo:' + str(cls.binary.with_suffix('.obj'))]
        else:
            flags = ['-std=c11', '-Wall', '-Wextra', '-frounding-math',
                     '-Werror=implicit-function-declaration',
                     '-I', str(root / 'include')]
            command = compiler + flags + [str(source), '-o', str(cls.binary), '-lm']
            cls.standalone_command = compiler + flags + [
                '-c', str(standalone_source),
                '-o', str(cls.binary.with_suffix('.o'))]
        # Rounding-mode tests require the compiler to honor runtime fenv changes.
        print('Standalone C compiler:', command, flush=True)
        result = subprocess.run(command, cwd=cls.directory.name, env=environment,
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

    def test_standalone_source_compiles_without_harness_headers(self):
        # The harness and Python.h can hide missing platform-specific includes.
        result = subprocess.run(
            self.standalone_command, cwd=self.directory.name,
            env=self.compiler_environment, capture_output=True, text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_positive_float_overflow_and_subnormals_in_all_rounding_modes(self):
        self.run_case('rounding-positive')

    def test_negative_float_overflow_and_subnormals_in_all_rounding_modes(self):
        self.run_case('rounding-negative')
