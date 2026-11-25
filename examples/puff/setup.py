from os.path import dirname

from setuptools import setup, Extension
import bitarray


setup(
    name = "puff",
    ext_modules = [Extension(
        name = "_puff",
        sources = ["_puff.c"],
        include_dirs = [dirname(bitarray.__file__)],
    )],
)
