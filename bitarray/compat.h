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


/* Critical sections were added in Python 3.13.
   With the GIL they are no-ops. */
#if PY_VERSION_HEX < 0x030d0000
#define Py_BEGIN_CRITICAL_SECTION(obj) {
#define Py_END_CRITICAL_SECTION() }
#define Py_BEGIN_CRITICAL_SECTION2(a, b) {
#define Py_END_CRITICAL_SECTION2() }
#endif


#endif  /* BITARRAY_COMPAT_H */
