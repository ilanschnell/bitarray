import re
import sys
import platform


if sys.version_info[0] == 2:
    sys.exit("""\
****************************************************************************
*   Python 2 support of bitarray has been removed.
*   The last version supporting Python 2 is bitarray 2.9.3.
****************************************************************************
""")

if "test" in sys.argv:
    import bitarray
    # when test was successful, return 0 (hence not)
    sys.exit(not bitarray.test().wasSuccessful())

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension


kwds = {}
try:
    kwds['long_description'] = open('README.rst').read()
except IOError:
    pass

# Read version from bitarray/bitarray.h
pat = re.compile(r'#define\s+BITARRAY_VERSION\s+"(\S+)"', re.M)
data = open('bitarray/bitarray.h').read()
kwds['version'] = pat.search(data).group(1)

macros = []
if platform.python_implementation() == 'PyPy':
    macros.append(("PY_LITTLE_ENDIAN", str(int(sys.byteorder == 'little'))))
    macros.append(("PY_BIG_ENDIAN", str(int(sys.byteorder == 'big'))))

setup(
    name = "bitarray",
    author = "Ilan Schnell",
    author_email = "ilanschnell@gmail.com",
    url = "https://github.com/ilanschnell/bitarray",
    license = "PSF-2.0",
    classifiers = [
        "Development Status :: 6 - Mature",
        "Intended Audience :: Developers",
        "Operating System :: OS Independent",
        "Programming Language :: C",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.5",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Topic :: Utilities",
    ],
    description = "efficient arrays of booleans -- C extension",
    packages = ["bitarray"],
    package_data = {"bitarray": ["*.h", "*.pickle",
                                 "py.typed",  # see PEP 561
                                 "*.pyi"]},
    ext_modules = [Extension(name = "bitarray._bitarray",
                             define_macros = macros,
                             sources = ["bitarray/_bitarray.c"]),
                   Extension(name = "bitarray._util",
                             sources = ["bitarray/_util.c"])],
    zip_safe = False,
    **kwds
)
