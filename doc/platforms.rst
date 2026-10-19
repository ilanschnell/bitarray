Platform support
================

Bitarray aims to support every platform supported by CPython or PyPy,
provided that a suitable C compiler is available.  It does not maintain a
closed list of supported operating systems or processor architectures.  Both
32-bit and 64-bit systems, little-endian and big-endian machines, and systems
with strict memory-alignment requirements are intended to work.

The test suite is run on Linux, macOS and Windows, using ``x86``, ``x86_64``,
``aarch64``, ``ppc64le``, ``s390x`` and Pyodide ``wasm32`` architectures and
both CPython and PyPy.
Additional workflows test NetBSD on ``amd64``, and Linux on ``riscv64``
and ``sparc64``.  Platforms not exercised by these workflows are not
necessarily unsupported.


Wheels and source builds
------------------------

Binary wheels are published on PyPI for common combinations of Python,
operating system and processor architecture.  The absence of a compatible
wheel does not mean that a platform is unsupported.  In that case, ``pip``
can install the source distribution and compile the extension locally:

.. code-block:: shell-session

    $ python -m pip install bitarray

A compiler and the development files required for building Python extension
modules must be installed.  After installation, the complete test suite can
be run with:

.. code-block:: shell-session

    $ python -c 'import bitarray; bitarray.test()'

Failures on a platform supported by CPython or PyPy should be reported on the
`issue tracker <https://github.com/ilanschnell/bitarray/issues>`__, preferably
including the output from this command.
