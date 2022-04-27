from distutils.core import setup, Extension

setup(
    name = "puff",
    ext_modules = [Extension(
        name = "_puff",
        sources = ["_puff.c"],
        include_dirs = ["../../bitarray"],
    )],
)
