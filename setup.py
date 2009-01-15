import os, sys
from distutils.core import setup, Extension

if sys.version_info[:2] < (2, 5):
    raise Exception('bitarray requires Python 2.5 or greater.')

kwds = {}

# Read the long description from README
f = open(os.path.abspath('README'))
kwds['long_description'] = f.read()
f.close()

setup(
    name = "bitarray",
    version = "0.3.5",
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
