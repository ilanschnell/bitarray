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


#endif  /* BITARRAY_COMPAT_H */
