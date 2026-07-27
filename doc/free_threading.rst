Free-threading support
======================

Bitarray (as of version 3.9.3) supports free-threaded CPython 3.14 and later.
On a Python interpreter configured using ``--disable-gil``,
importing :mod:`bitarray` does not enable the global interpreter lock (GIL).

Free-threading support is classified as beta.  The regular test suite and a
separate set of concurrent stress tests are used to exercise the extension
without the GIL.


Checking the interpreter
------------------------

``Py_GIL_DISABLED`` indicates whether CPython was built with free-threading
support:

.. code-block:: python

    >>> import sysconfig
    >>> sysconfig.get_config_var("Py_GIL_DISABLED")
    1

On Python 3.14 and later, the current runtime state of the GIL can be checked
separately:

.. code-block:: python

    >>> import sys
    >>> sys._is_gil_enabled()
    False

A free-threaded build may still be run with the GIL enabled.  The two checks
therefore answer different questions.


Thread safety
-------------

Bitarray operations use Python critical sections to protect the internal
state of bitarray objects.  In particular, concurrent operations must not
access a buffer after it has been resized or freed.  This applies to both
read-only operations and operations which resize or modify a bitarray.

Many individual operations produce a consistent result.  For example, when
one thread calls ``a.setall(0)`` or ``a.setall(1)`` while another calls
``a.tobytes()``, the result of ``tobytes()`` represents one complete state,
not a partially completed ``setall()`` operation.

Operations involving two bitarrays protect access to each operand.  This
includes comparisons, bitwise operations, concatenation and functions such
as ``count_and()``.  This guarantees safe access to both buffers, but does
not necessarily provide one simultaneous snapshot of both operands.
Operations involving a target, a mask and assigned values materialize
temporary information where necessary so that three mutable bitarrays do not
have to remain locked simultaneously.

These protections provide memory safety and operation-level consistency.
They do not make a sequence of Python statements atomic.  For example:

.. code-block:: python

    if a:
        a.pop()

another thread may clear ``a`` between the two operations.  User-level
synchronization is required when several operations form one transaction:

.. code-block:: python

    lock = threading.Lock()

    with lock:
        if a:
            a.pop()

Likewise, the order in which concurrent writers take effect is unspecified.


Concurrent mutation
-------------------

It is safe for threads to read and modify the same bitarray concurrently in
the sense that the bitarray remains a valid Python object and invalid memory
must not be accessed.  The result is not necessarily deterministic.

An operation may also fail when a relevant object changes concurrently.  For
example, an index or mask which was valid at the start of an assignment may
no longer match the target by the time it is used.  In such cases exceptions
such as ``IndexError`` or ``ValueError`` are possible.

Code dictionaries used by ``encode()``, ``decode()`` and ``decodetree`` are
similar.  Construction and lookup tolerate a dictionary, or code bitarrays
stored in it, changing concurrently.  There is no atomic snapshot of the
entire dictionary and all its values, however.  The operation may observe a
mixture of states or raise an exception.  Applications which require a
particular code must not mutate that code dictionary concurrently.

The same general rule applies to mutable sequences, arbitrary iterables and
file-like objects supplied to bitarray operations.  Calling Python code may
allow other threads to run.  Concurrent changes may affect the result even
though the bitarray itself remains memory-safe.


Iterators
---------

Bitarray iterators protect their internal position and the bitarray buffer
while producing an item.  Changing the source bitarray during iteration must
not cause an invalid memory access, but it may change the observed sequence.
Items may be omitted or repeated, and iteration may stop earlier or later
than it would without concurrent mutation.

Sharing one iterator between threads is not recommended.  In particular,
``searchiterator`` does not guarantee exact-once delivery when several
threads call ``next()`` on the same iterator.  Use a separate iterator in
each thread, or protect access to the shared iterator with a lock, when the
precise result matters.


Shared buffers
--------------

Object locks belong to Python objects, not to regions of memory.  Consequently
they cannot coordinate independent objects which happen to expose or import
the same buffer.

For example, these three objects share storage but have different locks:

.. code-block:: python

    data = bytearray(100)
    a = bitarray(buffer=data)
    b = bitarray(buffer=data)

Concurrent modification through ``a``, ``b`` and ``data`` is not guaranteed
to produce coherent snapshots.  The same limitation applies when a bitarray
buffer is changed through a ``memoryview``.  Overlapping imported buffers are
another instance of this limitation.

An exported buffer prevents operations which would resize its owning
bitarray.  This keeps existing views valid, but does not serialize writes
performed through those views.  Applications must use their own lock whenever
shared-buffer updates need synchronization.

See the `buffer protocol <buffer.rst>`__ documentation for more information
about importing and exporting buffers.


Subinterpreters and module-global state
---------------------------------------

Bitarray currently uses static type objects and process-global C state rather
than per-interpreter module state.  Its free-threading support therefore does
not guarantee safe, isolated use from multiple subinterpreters running
concurrently.  The guarantees described above apply to threads operating
within a single interpreter.


Testing
-------

The normal bitarray test entry point automatically includes
``bitarray.test_free_threading`` when CPython was configured using
``--disable-gil``:

.. code-block:: console

    $ python -c "import bitarray; assert bitarray.test().wasSuccessful()"

The stress workload can be increased using environment variables:

.. code-block:: console

    $ BITARRAY_TD_ROUNDS=1000 python -c \
        "import bitarray; assert bitarray.test().wasSuccessful()"

``BITARRAY_TD_NBITS`` controls the default bitarray size and
``BITARRAY_TD_TIMEOUT`` controls the worker timeout in seconds.

A debug free-threaded build (commonly named ``python3.14td``) is useful
because it enables additional CPython and bitarray assertions.  A regular
free-threaded build (commonly named ``python3.14t``) exercises actual parallel
execution as well.  Passing the stress tests increases confidence but does
not prove the absence of data races; sanitizer builds can provide additional
coverage.
