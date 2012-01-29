# does not work with Python 3, because weave is not yet supported

import hashlib

from bitarray import bitarray

import numpy
from scipy import weave


support_code = '''
#define D 501

int color(double cr, double ci)
{
    int d = 1;
    double zr=cr, zi=ci, zr2, zi2;
    for(;;) {
        zr2 = zr * zr;
        zi2 = zi * zi;
        if( zr2+zi2 > 16.0 ) goto finish;
        if( ++d == D ) goto finish;
        zi = 2.0 * zr * zi + ci;
        zr = zr2 - zi2 + cr;
    }
 finish:
    return d % 2;
}

static void
PyUFunc_0(char **args, npy_intp *dimensions, npy_intp *steps, void *func)
{
    npy_intp i, n;
    npy_intp is0 = steps[0];
    npy_intp is1 = steps[1];
    npy_intp os = steps[2];
    char *ip0 = args[0];
    char *ip1 = args[1];
    char *op = args[2];
    n = dimensions[0];

    for(i = 0; i < n; i++) {
        *(long *)op = color(*(double *)ip0, *(double *)ip1);
        ip0 += is0;
        ip1 += is1;
        op += os;
    }
}

static PyUFuncGenericFunction f_functions[] = {
    PyUFunc_0,
};
static char f_types[] = {
    NPY_DOUBLE, NPY_DOUBLE, NPY_BOOL,
};
'''
ufunc_info = weave.base_info.custom_info()
ufunc_info.add_header('"numpy/ufuncobject.h"')

mandel = weave.inline('/* ' + hashlib.md5(support_code).hexdigest() + ''' */
import_ufunc();

return_val = PyUFunc_FromFuncAndData(f_functions,
                                     NULL,
                                     f_types,
                                     1,             /* ntypes */
                                     2,             /* nin */
                                     1,             /* nout */
                                     PyUFunc_None,  /* identity */
                                     "mandel",      /* name */
                                     "doc",         /* doc */
                                     0);
''',
                      support_code=support_code,
                      verbose=0,
                      customize=ufunc_info)

# ----------------------------------------------------------------------------

w, h = 8000, 6000

y, x = numpy.ogrid[-1.5:+1.5:h*1j, -2.75:+1.25:w*1j]

data = mandel(x, y)

bitdata = bitarray(endian='big')
bitdata.pack(data.tostring())

fo = open('mandel.ppm', 'wb')
fo.write('P4\n')
fo.write('# This is a partable bitmap image of the Mandelbrot set.\n')
fo.write('%i %i\n' % (w, h))
bitdata.tofile(fo)
fo.close()
