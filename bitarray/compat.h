#ifndef BITARRAY_COMPAT_H
#define BITARRAY_COMPAT_H


/* Py_SET_SIZE() was added in Python 3.9. */
#if !defined(Py_SET_SIZE) && PY_VERSION_HEX < 0x03090000
static inline void
bitarray_set_size(PyVarObject *obj, Py_ssize_t size)
{
    obj->ob_size = size;
}
#define Py_SET_SIZE(obj, size) \
    bitarray_set_size((PyVarObject *) (obj), (size))
#endif


/* Py_NewRef() was added in Python 3.10. */
#if !defined(Py_NewRef) && PY_VERSION_HEX < 0x030a0000
static inline PyObject *
bitarray_newref(PyObject *obj)
{
    Py_INCREF(obj);
    return obj;
}
#define Py_NewRef(obj) bitarray_newref((PyObject *) (obj))
#endif


/* PyDict_GetItemRef() was added in Python 3.13. */
#if PY_VERSION_HEX < 0x030d0000
static inline int
PyDict_GetItemRef(PyObject *dict, PyObject *key, PyObject **result)
{
    PyObject *item = PyDict_GetItemWithError(dict, key);

    if (item != NULL) {
        *result = Py_NewRef(item);
        return 1;
    }
    *result = NULL;
    return PyErr_Occurred() ? -1 : 0;
}
#endif


/* Py_GetConstant() was added in Python 3.13. */
#if PY_VERSION_HEX < 0x030d0000
#define Py_CONSTANT_NONE             0
#define Py_CONSTANT_FALSE            1
#define Py_CONSTANT_TRUE             2
#define Py_CONSTANT_ELLIPSIS         3
#define Py_CONSTANT_NOT_IMPLEMENTED  4
#define Py_CONSTANT_ZERO             5
#define Py_CONSTANT_ONE              6
#define Py_CONSTANT_EMPTY_STR        7
#define Py_CONSTANT_EMPTY_BYTES      8
#define Py_CONSTANT_EMPTY_TUPLE      9

static inline PyObject *
Py_GetConstant(unsigned int constant_id)
{
    static PyObject *constants[Py_CONSTANT_EMPTY_TUPLE + 1] = {NULL};

    if (constants[Py_CONSTANT_NONE] == NULL) {
        constants[Py_CONSTANT_NONE] = Py_None;
        constants[Py_CONSTANT_FALSE] = Py_False;
        constants[Py_CONSTANT_TRUE] = Py_True;
        constants[Py_CONSTANT_ELLIPSIS] = Py_Ellipsis;
        constants[Py_CONSTANT_NOT_IMPLEMENTED] = Py_NotImplemented;

        constants[Py_CONSTANT_ZERO] = PyLong_FromLong(0);
        if (constants[Py_CONSTANT_ZERO] == NULL)
            goto error;

        constants[Py_CONSTANT_ONE] = PyLong_FromLong(1);
        if (constants[Py_CONSTANT_ONE] == NULL)
            goto error;

        constants[Py_CONSTANT_EMPTY_STR] = PyUnicode_FromStringAndSize("", 0);
        if (constants[Py_CONSTANT_EMPTY_STR] == NULL)
            goto error;

        constants[Py_CONSTANT_EMPTY_BYTES] = PyBytes_FromStringAndSize("", 0);
        if (constants[Py_CONSTANT_EMPTY_BYTES] == NULL)
            goto error;

        constants[Py_CONSTANT_EMPTY_TUPLE] = PyTuple_New(0);
        if (constants[Py_CONSTANT_EMPTY_TUPLE] == NULL)
            goto error;
    }

    if (constant_id <= Py_CONSTANT_EMPTY_TUPLE)
        return Py_NewRef(constants[constant_id]);

    PyErr_BadInternalCall();
    return NULL;

error:
    Py_FatalError("Py_GetConstant() failed to get constants");
}
#endif


/* Critical sections were added in Python 3.13.
   With the GIL they are no-ops. */
#if PY_VERSION_HEX < 0x030d0000
#define Py_BEGIN_CRITICAL_SECTION(obj) {
#define Py_END_CRITICAL_SECTION() }
#define Py_BEGIN_CRITICAL_SECTION2(a, b) {
#define Py_END_CRITICAL_SECTION2() }
#endif


#endif  /* BITARRAY_COMPAT_H */
