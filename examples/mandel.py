import sys
from bitarray import bitarray
from numba import jit

width, height = 4000, 3000
maxdepth = 500


@jit(nopython=True)
def mandel(c):
    d = 0
    z = c
    while abs(z) < 4.0 and d <= maxdepth:
        d += 1
        z = z * z + c
    return d


def main():
    data = bitarray(endian='big')

    for j in range(height):
        sys.stdout.write('.')
        sys.stdout.flush()
        y = +1.5 - 3.0 * j / height
        for i in range(width):
            x = -2.75 + 4.0 * i / width
            c = mandel(complex(x, y)) % 2
            data.append(c)
    print("done")

    with open('out.ppm', 'wb') as fo:
        fo.write(b'P4\n')
        fo.write(b'# partable bitmap image of the Mandelbrot set\n')
        fo.write(b'%i %i\n' % (width, height))
        data.tofile(fo)


if __name__ == '__main__':
    main()
