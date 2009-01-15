import sys
import re
from os.path import abspath, join
from distutils.core import setup, Extension

if sys.version_info[:2] < (2, 5):
    raise Exception('bitarray requires Python 2.5 or greater.')

kwds = {}

# Read the long description from README
kwds['long_description'] = open(abspath('README')).read()

# Read version from bitarray/__init__.py
pat = re.compile(r'__version__\s*=\s*(.+)')
for line in open(abspath(join('bitarray', '__init__.py'))):
    match = pat.match(line)
    if match:
        kwds['version'] = eval(match.group(1))
        break
assert 'version' in kwds


setup(
    name = "bitarray",
    author = "Ilan Schnell",
    author_email = "ilanschnell@gmail.com",
    url = "http://pypi.python.org/pypi/bitarray/",
    license = "PSF",
    classifiers = [
        "License :: OSI Approved :: Python Software Foundation License",
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Operating System :: OS Independent",
        "Programming Language :: C",
        "Programming Language :: Python",
        "Topic :: Utilities",
    ],
    description = "efficient arrays of booleans -- C extension",
    packages = ["bitarray"],
    ext_modules = [Extension(name = "bitarray._bitarray",
                             sources = ["bitarray/_bitarray.c"])],
    **kwds
)
