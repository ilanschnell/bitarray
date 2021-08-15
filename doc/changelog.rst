Change log
==========

2021-08-XX   2.3.0:

* add optional ``buffer`` argument to ``bitarray()`` to import the buffer of
  another object, `#146 <https://github.com/ilanschnell/bitarray/issues/146>`__, see also: `buffer protocol <./buffer.rst>`__
* update ``.buffer_info()`` to include: a read-only flag, an imported buffer
  flag, and the number of buffer exports
* add optional start and stop arguments to ``util.rindex()``
* add `memory-mapped file <../examples/mmapped-file.py>`__ example
* ignore underscore (``_``) in string input, e.g. ``bitarray('1100_0111')``
* add missing type hinting for new ``.bytereverse()`` arguments
* fix ``.extend()`` type annotations, `#145 <https://github.com/ilanschnell/bitarray/issues/145>`__
* avoid ``.reverse()`` using temporary memory
* make ``.unpack()``, ``util.serialize()``, ``util.vl_encode()``
  and ``.__reduce__()`` more memory efficient


**2.2.5** (2021-08-07):

* speedup ``find_bit()`` and ``find_last()`` using uint64 checking, this means
  a speedup for ``.find()``, ``.index()``, ``.search()`` and ``util.rindex()``
* add optional start and stop arguments to ``.bytereverse()``
* add example to illustrate how
  `unaligned copying <../examples/copy_n.py>`__ works internally
* add documentation
* add tests


**2.2.4** (2021-07-29):

* use shift operations to speedup all unaligned copy operations, `#142 <https://github.com/ilanschnell/bitarray/issues/142>`__
* expose functionality to Python level only in debug mode for testing
* add and improve tests


**2.2.3** (2021-07-22):

* speedup ``repeat()``, `#136 <https://github.com/ilanschnell/bitarray/issues/136>`__
* speedup shift operations, `#139 <https://github.com/ilanschnell/bitarray/issues/139>`__
* optimize slice assignment with negative step, e.g.: ``a[::-1] = 1``
* add tests


**2.2.2** (2021-07-16):

* speedup slice assignment, see `#132 <https://github.com/ilanschnell/bitarray/issues/132>`__ and `#135 <https://github.com/ilanschnell/bitarray/issues/135>`__
* speedup bitwise operations, `#133 <https://github.com/ilanschnell/bitarray/issues/133>`__
* optimize ``getbit()`` and ``setbit()`` in ``bitarray.h``
* fix TypeError messages when bitarray or int (0, 1) are expected (bool
  is a subclass of int)
* add and improve tests


**2.2.1** (2021-07-06):

* improve documentation
* speedup ``vl_encode()``
* ``bitarray.h``: make ``getbit()`` always an (inline) function
* add assertions in C code


**2.2.0** (2021-07-03):

* add ``bitarray.util.vl_encode()`` and ``bitarray.util.vl_decode()`` which
  uses a `variable length bitarray format <variable_length.rst>`__, `#131 <https://github.com/ilanschnell/bitarray/issues/131>`__


**2.1.3** (2021-06-15):

* Fix building with MSVC / Bullseye, `#129 <https://github.com/ilanschnell/bitarray/issues/129>`__


**2.1.2** (2021-06-13):

* support type hinting for all Python 3 versions (that bitarray supports,
  3.5 and higher currently), fixed `#128 <https://github.com/ilanschnell/bitarray/issues/128>`__
* add explicit endianness to two tests, fixes `#127 <https://github.com/ilanschnell/bitarray/issues/127>`__


**2.1.1** (2021-06-11):

* add type hinting (see PEP 484, 561) using stub (``.pyi``) files
* add tests


**2.1.0** (2021-05-05):

* add ``.find()`` method, see `#122 <https://github.com/ilanschnell/bitarray/issues/122>`__
* ``.find()``, ``.index()``, ``.search()`` and ``.itersearch()`` now all except
  both (sub-) bitarray as well as bool items to be searched for
* improve encode/decode error messages
* add `lexicographical permutations example <../examples/lexico.py>`__
* add tests


**2.0.1** (2021-04-19):

* update documentation
* improve some error messages


**2.0.0** (2021-04-14):

* require more specific objects, int (0 or 1) or bool, see `#119 <https://github.com/ilanschnell/bitarray/issues/119>`__
* items are always returned as int 0 or 1, `#119 <https://github.com/ilanschnell/bitarray/issues/119>`__
* remove ``.length()`` method (deprecated since 1.5.1 - use ``len()``)
* in ``.unpack()`` the ``one`` argument now defaults to 0x01 (was 0xff)
* ``.tolist()`` now always returns a list of integers (0 or 1)
* fix frozenbitarray hash function, see `#121 <https://github.com/ilanschnell/bitarray/issues/121>`__
* fix frozenbitarray being mutable by ``<<=`` and ``>>=``
* support sequence protocol in ``.extend()`` (and bitarray creation)
* improve OverflowError messages from ``util.int2ba()``
* add `hexadecimal example <../examples/hexadecimal.py>`__


**1.9.2** (2021-04-10):

* update pythoncapi_compat: Fix support with PyPy 3.7, `#120 <https://github.com/ilanschnell/bitarray/issues/120>`__
* update readme


**1.9.1** (2021-04-05):

* switch documentation from markdown to reStructuredText
* add tests


**1.9.0** (2021-04-03):

* add shift operations (``<<``, ``>>``, ``<<=``, ``>>=``), see `#117 <https://github.com/ilanschnell/bitarray/issues/117>`__
* add ``bitarray.util.ba2base()`` and ``bitarray.util.base2ba()``,
  see last paragraph in `Bitarray representations <represent.rst>`__
* documentation and tests


**1.8.2** (2021-03-31):

* fix crash caused by unsupported types in binary operations, `#116 <https://github.com/ilanschnell/bitarray/issues/116>`__
* speedup initializing or extending a bitarray from another with different
  bit endianness
* add formatting options to ``bitarray.util.pprint()``
* add documentation on `bitarray representations <represent.rst>`__
* add and improve tests (all 291 tests run in less than half a second on
  a modern machine)


**1.8.1** (2021-03-25):

* moved implementation of and ``hex2ba()`` and ``ba2hex()`` to C-level
* add ``bitarray.util.parity()``


**1.8.0** (2021-03-21):

* add ``bitarray.util.serialize()`` and ``bitarray.util.deserialize()``
* allow whitespace (ignore space and ``\n\r\t\v``) in input strings,
  e.g. ``bitarray('01 11')`` or ``a += '10 00'``
* add ``bitarray.util.pprint()``
* When initializing a bitarray from another with different bit endianness,
  e.g. ``a = bitarray('110', 'little')`` and ``b = bitarray(a, 'big')``,
  the buffer used to be simply copied, with consequence that ``a == b`` would
  result in ``False``.  This is fixed now, that is ``a == b`` will always
  evaluate to ``True``.
* add test for loading existing pickle file (created using bitarray 1.5.0)
* add example showing how to `jsonize bitarrays <../examples/extend_json.py>`__
* add tests


**1.7.1** (2021-03-12):

* fix issue `#114 <https://github.com/ilanschnell/bitarray/issues/114>`__, raise TypeError when incorrect index is used during
  assignment, e.g. ``a[1.5] = 1``
* raise TypeError (not IndexError) when assigning slice to incorrect type,
  e.g. ``a[1:4] = 1.2``
* improve some docstrings and tests


**1.7.0** (2021-02-27):

* add ``bitarray.util.urandom()``
* raise TypeError when trying to extend bitarrays from bytes on Python 3,
  ie. ``bitarray(b'011')`` and ``.extend(b'110')``.  (Deprecated since 1.4.1)


**1.6.3** (2021-01-20):

* add missing .h files to sdist tarball, `#113 <https://github.com/ilanschnell/bitarray/issues/113>`__


**1.6.2** (2021-01-20):

* use ``Py_SET_TYPE()`` and ``Py_SET_SIZE()`` for Python 3.10, `#109 <https://github.com/ilanschnell/bitarray/issues/109>`__
* add official Python 3.10 support
* fix slice assignment to same object,
  e.g. ``a[2::] = a`` or ``a[::-1] = a``, `#112 <https://github.com/ilanschnell/bitarray/issues/112>`__
* add bitarray.h, `#110 <https://github.com/ilanschnell/bitarray/issues/110>`__


**1.6.1** (2020-11-05):

* use PyType_Ready for all types: bitarray, bitarrayiterator,
  decodeiterator, decodetree, searchiterator


**1.6.0** (2020-10-17):

* add ``decodetree`` object, for speeding up consecutive calls
  to ``.decode()`` and ``.iterdecode()``, in particular when dealing
  with large prefix codes, see `#103 <https://github.com/ilanschnell/bitarray/issues/103>`__
* add optional parameter to ``.tolist()`` which changes the items in the
  returned list to integers (0 or 1), as opposed to Booleans
* remove deprecated ``bitdiff()``, which has been deprecated since version
  1.2.0, use ``bitarray.util.count_xor()`` instead
* drop Python 2.6 support
* update license file, `#104 <https://github.com/ilanschnell/bitarray/issues/104>`__


**1.5.3** (2020-08-24):

* add optional index parameter to ``.index()`` to invert single bit
* fix ``sys.getsizeof(bitarray)`` by adding ``.__sizeof__()``, see issue `#100 <https://github.com/ilanschnell/bitarray/issues/100>`__


**1.5.2** (2020-08-16):

* add PyType_Ready usage, issue `#66 <https://github.com/ilanschnell/bitarray/issues/66>`__
* speedup search() for bitarrays with length 1 in sparse bitarrays,
  see issue `#67 <https://github.com/ilanschnell/bitarray/issues/67>`__
* add tests


**1.5.1** (2020-08-10):

* support signed integers in ``util.ba2int()`` and ``util.int2ba()``,
  see issue `#85 <https://github.com/ilanschnell/bitarray/issues/85>`__
* deprecate ``.length()`` in favor of ``len()``


**1.5.0** (2020-08-05):

* Use ``Py_ssize_t`` for bitarray index.  This means that on 32bit
  systems, the maximum number of elements in a bitarray is 2 GBits.
  We used to have a special 64bit index type for all architectures, but
  this prevented us from using Python's sequence, mapping and number
  methods, and made those method lookups slow.
* speedup slice operations when step size = 1 (if alignment allows
  copying whole bytes)
* Require equal endianness for operations: ``&``, ``|``, ``^``, ``&=``, ``|=``, ``^=``.
  This should have always been the case but was overlooked in the past.
* raise TypeError when trying to create bitarray from boolean
* This will be last release to still support Python 2.6 (which was retired
  in 2013).  We do NOT plan to stop support for Python 2.7 anytime soon.


**1.4.2** (2020-07-15):

* add more tests
* C-level:
    - simplify pack/unpack code
    - fix memory leak in ``~`` operation (bitarray_cpinvert)


**1.4.1** (2020-07-14):

* add official Python 3.9 support
* improve many docstrings
* add DeprecationWarning for ``bitdiff()``
* add DeprecationWarning when trying to extend bitarrays
  from bytes on Python 3 (``bitarray(b'011')`` and ``.extend(b'110')``)
* C-level:
    - Rewrote ``.fromfile()`` and ``.tofile()`` implementation,
      such that now the same code is used for Python 2 and 3.
      The new implementation is more memory efficient on
      Python 3.
    - use ``memcmp()`` in ``richcompare()`` to shortcut EQ/NE, when
      comparing two very large bitarrays for equality the
      speedup can easily be 100x
    - simplify how unpacking is handled
* add more tests


**1.4.0** (2020-07-11):

* add ``.clear()`` method (Python 3.3 added this method to lists)
* avoid over-allocation when bitarray objects are initially created
* raise BufferError when resizing bitarrays which is exporting buffers
* add example to study the resize() function
* improve some error messages
* add more tests
* raise ``NotImplementedError`` with (useful message) when trying to call
  the ``.fromstring()`` or ``.tostring()`` methods, which have been removed
  in the last release


**1.3.0** (2020-07-06):

* add ``bitarray.util.make_endian()``
* ``util.ba2hex()`` and ``util.hex2ba()`` now also support little-endian
* add ``bitarray.get_default_endian()``
* made first argument of initializer a positional-only parameter
* remove ``.fromstring()`` and ``.tostring()`` methods, these have been
  deprecated 8 years ago, since version 0.4.0
* add ``__all__`` in ``bitarray/__init__.py``
* drop Python 3.3 and 3.4 support


**1.2.2** (2020-05-18):

* ``util.ba2hex()`` now always return a string object (instead of bytes
  object for Python 3), see issue `#94 <https://github.com/ilanschnell/bitarray/issues/94>`__
* ``util.hex2ba`` allows a unicode object as input on Python 2
* Determine 64-bitness of interpreter in a cross-platform fashion `#91 <https://github.com/ilanschnell/bitarray/issues/91>`__,
  in order to better support PyPy


**1.2.1** (2020-01-06):

* simplify markdown of readme so PyPI renders better
* make tests for bitarray.util required (instead of warning when
  they cannot be imported)


**1.2.0** (2019-12-06):

* add bitarray.util module which provides useful utility functions
* deprecate ``bitarray.bitdiff()`` in favor of ``bitarray.util.count_xor``
* use markdown for documentation
* fix bug in ``.count()`` on 32bit systems in special cases when array size
  is 2^29 bits or larger
* simplified tests by using bytes syntax
* update smallints and sieve example to use new utility module
* simplified mandel example to use numba
* use file context managers in tests


**1.1.0** (2019-11-07):

* add frozenbitarray object
* add optional start and stop arguments to ``.count()`` method
* add official Python 3.8 support
* optimize ``setrange()`` C-function by using ``memset()``
* fix issue `#74 <https://github.com/ilanschnell/bitarray/issues/74>`__, bitarray is hashable on Python 2
* fix issue `#68 <https://github.com/ilanschnell/bitarray/issues/68>`__, ``unittest.TestCase.assert_`` deprecated
* improved test suite - tests should run in about 1 second
* update documentation to use positional-only syntax in docstrings
* update readme to pass Python 3 doctest
* add utils module to examples


**1.0.1** (2019-07-19):

* fix readme to pass ``twine check``


**1.0.0** (2019-07-15):

* fix bitarrays beings created from unicode in Python 2
* use ``PyBytes_*`` in C code, treating the Py3k function names as default,
  which also removes all redefinitions of ``PyString_*``
* handle negative arguments of .index() method consistently with how
  they are treated for lists
* add a few more comments to the C code
* move imports outside tests: pickle, io, etc.
* drop Python 2.5 support


**0.9.3** (2019-05-20):

* refactor resize() - only shrink allocated memory if new size falls
  lower than half the allocated size
* improve error message when trying to initialize from float or complex


**0.9.2** (2019-04-29):

* fix to compile on Windows with VS 2015, issue `#72 <https://github.com/ilanschnell/bitarray/issues/72>`__


**0.9.1** (2019-04-28):

* fix types to actually be types, `#29 <https://github.com/ilanschnell/bitarray/issues/29>`__
* check for ambiguous prefix codes when building binary tree for decoding
* remove Python level methods: encode, decode, iterdecode (in favor of
  having these implemented on the C-level along with check_codedict)
* fix self tests for Python 2.5 and 2.6
* move all Huffman code related example code into examples/huffman
* add code to generate graphviz .dot file of Huffman tree to examples


**0.9.0** (2019-04-22):

* more efficient decode and iterdecode by using C-level binary tree
  instead of a python one, `#54 <https://github.com/ilanschnell/bitarray/issues/54>`__
* added buffer protocol support for Python 3, `#55 <https://github.com/ilanschnell/bitarray/issues/55>`__
* fixed invalid pointer exceptions in pypy, `#47 <https://github.com/ilanschnell/bitarray/issues/47>`__
* made all examples Py3k compatible
* add gene sequence example
* add official Python 3.7 support
* drop Python 2.4, 3.1 and 3.2 support


**0.8.3** (2018-07-06):

* add exception to setup.py when README.rst cannot be opened


**0.8.2** (2018-05-30):

* add official Python 3.6 support (although it was already working)
* fix description of ``fill()``, `#52 <https://github.com/ilanschnell/bitarray/issues/52>`__
* handle extending self correctly, `#28 <https://github.com/ilanschnell/bitarray/issues/28>`__
* copy_n: fast copy with memmove fixed, `#43 <https://github.com/ilanschnell/bitarray/issues/43>`__
* minor clarity/wording changes to README, `#23 <https://github.com/ilanschnell/bitarray/issues/23>`__


**0.8.1** (2013-03-30):

* fix issue `#10 <https://github.com/ilanschnell/bitarray/issues/10>`__, i.e. ``int(bitarray())`` segfault
* added tests for using a bitarray object as an argument to functions
  like int, long (on Python 2), float, list, tuple, dict


**0.8.0** (2012-04-04):

* add Python 2.4 support
* add (module level) function bitdiff for calculating the difference
  between two bitarrays


**0.7.0** (2012-02-15):

* add iterdecode method (C level), which returns an iterator but is
  otherwise like the decode method
* improve memory efficiency and speed of pickling large bitarray objects


**0.6.0** (2012-02-06):

* add buffer protocol to bitarray objects (Python 2.7 only)
* allow slice assignment to 0 or 1, e.g. ``a[::3] = 0``  (in addition to
  booleans)
* moved implementation of itersearch method to C level (Lluis Pamies)
* search, itersearch now only except bitarray objects,
  whereas ``__contains__`` excepts either booleans or bitarrays
* use a priority queue for Huffman tree example (thanks to Ushma Bhatt)
* improve documentation


**0.5.2** (2012-02-02):

* fixed MSVC compile error on Python 3 (thanks to Chris Gohlke)
* add missing start and stop optional parameters to index() method
* add examples/compress.py


**0.5.1** (2012-01-31):

* update documentation to use tobytes and frombytes, rather than tostring
  and fromstring (which are now deprecated)
* simplified how tests are run


**0.5.0** (2012-01-23):

* added itersearch method
* added Bloom filter example
* minor fixes in docstrings, added more tests


**0.4.0** (2011-12-29):

* porting to Python 3.x (Roland Puntaier)
* introduced tobytes, frombytes (tostring, fromstring are now deprecated)
* updated development status
* added sieve prime number example
* moved project to github: https://github.com/ilanschnell/bitarray


**0.3.5** (2009-04-06):

* fixed reference counts bugs
* added possibility to slice assign to True or False, e.g. a[::3] = True
  will set every third element to True


**0.3.4** (2009-01-15):

* Made C code less ambiguous, such that the package compiles on
  Visual Studio, with all tests passing.


**0.3.3** (2008-12-14):

* Made changes to the C code to allow compilation with more compilers.
  Compiles on Visual Studio, although there are still a few tests failing.


**0.3.2** (2008-10-19):

* Added sequential search method.
* The special method ``__contains__`` now also takes advantage of the
  sequential search.


**0.3.1** (2008-10-12):

* Simplified state information for pickling.  Argument for count is now
  optional, defaults to True.  Fixed typos.


**0.3.0** (2008-09-30):

* Fixed a severe bug for 64-bit machines.  Implemented all methods in C,
  improved tests.
* Removed deprecated methods from01 and fromlist.


**0.2.5** (2008-09-23):

* Added section in README about prefix codes.  Implemented _multiply method
  for faster __mul__ and __imul__.  Fixed some typos.


**0.2.4** (2008-09-22):

* Implemented encode and decode method (in C) for variable-length prefix
  codes.
* Added more examples, wrote README for the examples.
* Added more tests, fixed some typos.


**0.2.3** (2008-09-16):

* Fixed a memory leak, implemented a number of methods in C.
  These include __getitem__, __setitem__, __delitem__, pop, remove,
  insert.  The methods implemented on the Python level is very limit now.
* Implemented bitwise operations.


**0.2.2** (2008-09-09):

* Rewrote parts of the README
* Implemented memory efficient algorithm for the reverse method
* Fixed typos, added a few tests, more C refactoring.


**0.2.1** (2008-09-07):

* Improved tests, in particular added checking for memory leaks.
* Refactored many things on the C level.
* Implemented a few more methods.


**0.2.0** (2008-09-02):

* Added bit endianness property to the bitarray object
* Added the examples to the release package.


**0.1.0** (2008-08-17):

* First official release; put project to
  http://pypi.python.org/pypi/bitarray/


May 2008:

Wrote the initial code, and put it on my personal web-site:
http://ilan.schnell-web.net/prog/
