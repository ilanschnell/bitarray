import os
import sys
import ctypes
from subprocess import call
from bitarray import bitarray

width, height = 4000, 3000


def compile_mandel():
    c_file = 'tmp.c'
    o_file = 'tmp.o'
    so_file = 'tmp.so'

    with open(c_file, 'w') as fo:
        fo.write('''
#define D 1001
int mandel(double cr, double ci)
{
    int d = 1;
    double zr = cr, zi = ci, zr2, zi2;
    while (1) {
        zr2 = zr * zr;
        zi2 = zi * zi;
        if (zr2+zi2 > 16.0)
            break;
        if (++d == D)
            break;
        zi = 2.0 * zr * zi + ci;
        zr = zr2 - zi2 + cr;
    }
    return d;
}
''')
    call(['gcc', '-c', '-O3', '-fpic', c_file])
    call(['gcc', '-shared', '-o', so_file, o_file])
    os.unlink(c_file)
    os.unlink(o_file)

    sobj = ctypes.CDLL(so_file)
    f = sobj.mandel
    f.argtypes = [ctypes.c_double, ctypes.c_double]
    f.restype = ctypes.c_int
    os.unlink(so_file)
    return f


def main():
    mandel = compile_mandel()

    data = bitarray(endian='big')

    for j in range(height):
        sys.stdout.write('.')
        sys.stdout.flush()
        y = +1.5 - 3.0 * j / height
        for i in range(width):
            x = -2.75 + 4.0 * i / width
            c = mandel(x, y) % 2
            data.append(c)
    print("done")

    with open('mandel.ppm', 'wb') as fo:
        fo.write(b'P4\n')
        fo.write(b'# partable bitmap image of the Mandelbrot set\n')
        fo.write(b'%i %i\n' % (width, height))
        data.tofile(fo)


if __name__ == '__main__':
    main()
