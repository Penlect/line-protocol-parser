from setuptools import setup, Extension, find_packages
import platform
import sys

sys.path.append('line_protocol_parser')
from _info import (
    __version__,
    __author__,
    __maintainer__,
    __email__,
    __status__,
    __url__,
    __license__
)
sys.path.remove('line_protocol_parser')
with open('README.rst') as readme_file:
    long_description = readme_file.read()

if platform.system() == 'Windows':
    # MSVC
    extra_compile_args = ['/DLP_MALLOC=PyMem_Malloc',
                          '/DLP_FREE=PyMem_Free',
                          '/DPY_SSIZE_T_CLEAN',
                          '/FI', 'Python.h']
elif platform.system() == 'Linux':
    # GCC
    extra_compile_args = ['-DLP_MALLOC=PyMem_Malloc',
                          '-DLP_FREE=PyMem_Free',
                          '-DPY_SSIZE_T_CLEAN',
                          '-include', 'Python.h']
else:
    # TODO: Test on Mac
    extra_compile_args = []

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
        Extension('line_protocol_parser._line_protocol_parser',
                  sources=['src/line_protocol_parser.c', 'src/module.c'],
                  include_dirs=['include'],
                  # undef_macros=['NDEBUG'],
                  extra_compile_args=extra_compile_args
                  )
    ],
    packages=find_packages(exclude=['tests']),
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Environment :: Console',
        'Intended Audience :: Developers',
        'Intended Audience :: Education',
        'Intended Audience :: Science/Research',
        'Topic :: Software Development :: Version Control :: Git',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Operating System :: Microsoft :: Windows :: Windows 10',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: C',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: Implementation :: CPython'
    ]
)

# For development, use "python3 setup.py develop".