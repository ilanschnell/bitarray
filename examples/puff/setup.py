import sys
from os.path import dirname

if sys.version_info[:2] < (3, 6):
    sys.exit("This example requires Python 3.6 or higher")

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension

import bitarray


setup(
    name = "puff",
    ext_modules = [Extension(
        name = "_puff",
        sources = ["_puff.c"],
        include_dirs = [dirname(bitarray.__file__)],
    )],
)
