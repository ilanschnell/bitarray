Contributing to bitarray
========================

The bitarray type is very stable and feature complete at this point,
which means that pull requests to `bitarray/_bitarray.c` will most likely
be rejected, unless they improve readability and performance.

There may be room for improvements/additions in the `bitarray.util` module,
added in the 1.2.0 release.  However, due to the slow release cycle of this
package, it may be more practical to create your own library which depends
on bitarray.  This is completely possible, even on the C-level.  Please
study the implementation of `bitarray/_util.c` for details.  In particular for
C extensions to work with the bitarray type, it is important that
the `bitarrayobject` struct is defined in the same way:

    typedef struct {
        PyObject_VAR_HEAD
        char *ob_item;              /* buffer */
        Py_ssize_t allocated;       /* allocated buffer size (in bytes) */
        Py_ssize_t nbits;           /* length of bitarray, i.e. elements */
        int endian;                 /* bit-endianness of bitarray */
        int ob_exports;             /* how many buffer exports */
        PyObject *weakreflist;      /* list of weak references */
        Py_buffer *buffer;          /* used when importing a buffer */
        int readonly;               /* buffer is readonly */
    } bitarrayobject;

    /* member endian may have these values */
    #define ENDIAN_LITTLE  0
    #define ENDIAN_BIG     1

The last two members, `buffer` and `readonly`, were introduced in bitarray
version 2.3.  If those are not needed, and one wants to maintain backward
compatibility with earlier version, one can omit them.
These essential (as well as other useful) declarations can be found
in `bitarray/bitarray.h`.
