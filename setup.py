"""line-protocol-parser build script"""

import platform
from setuptools import setup, Extension, find_packages

with open('README.rst') as readme_file:
    long_description = readme_file.read()

# The package can't be imported at this point since the extension
# module is imported in __init__ and it does not exist yet. Therefore,
# we grab the metadata lines manually.
with open('line_protocol_parser/__init__.py') as init:
    lines = [line for line in init.readlines() if line.startswith('__')]
exec(''.join(lines), globals())

if platform.system() == 'Windows':
    # MSVC
    extra_compile_args = ['/FI', 'Python.h']
else:
    # GCC & clang
    extra_compile_args = ['-include', 'Python.h']

setup(
    name='line-protocol-parser',
    version=__version__,
    author=__author__,
    author_email=__email__,
    description='Parse InfluxDB line protocol string into Python dictionary',
    long_description=long_description,
    license=__license__,
    keywords='InfluxDB influx line protocol parser reader',
    url=__url__,
    ext_modules=[
        Extension(
            'line_protocol_parser._line_protocol_parser',
            sources=['src/line_protocol_parser.c', 'src/module.c'],
            include_dirs=['include'],
            define_macros=[
                ('LP_MALLOC', 'PyMem_Malloc'),
                ('LP_FREE', 'PyMem_Free'),
                ('PY_SSIZE_T_CLEAN', None)
            ],
            extra_compile_args=extra_compile_args)
    ],
    packages=find_packages(exclude=['tests']),
    include_package_data=True,
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Environment :: Console',
        'Intended Audience :: Developers',
        'Intended Audience :: Education',
        'Intended Audience :: Science/Research',
        'Topic :: Software Development :: Version Control :: Git',
        'Natural Language :: English',
        'Operating System :: POSIX :: Linux',
        'Operating System :: MacOS',
        'Operating System :: Microsoft :: Windows :: Windows 10',
        'Programming Language :: C',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3.12',
        'Programming Language :: Python :: 3.13',
        'Programming Language :: Python :: 3.14',
        'Programming Language :: Python :: Implementation :: CPython'
    ]
)
