from bitarray import bitarray, bits2bytes


class PBM: # Portable Bitmap
    def __init__(self, w = 0, h = 0):
        self.size = w, h
        self.update()
        self.data = bitarray(self.bits, endian='big')

    def update(self):
        w, h = self.size
        self.bytes_per_row = bits2bytes(w)
        self.bits_per_row = 8 * self.bytes_per_row
        self.bytes = self.bytes_per_row * h
        self.bits = 8 * self.bytes

    def info(self):
        print('size: %d x %d' % self.size)
        print('bytes per row: %d' % self.bytes_per_row)
        print('bits per row: %d' % self.bits_per_row)
        print('bitarray: ' + repr(self.data.buffer_info()))

    def clear(self):
        self.data.setall(0)

    def save(self, filename):
        with open(filename, 'wb') as fo:
            fo.write(b'P4\n')
            fo.write(b'# This is a partable bitmap (pbm) file.\n')
            fo.write(('%d %d\n' % self.size).encode())
            self.data.tofile(fo)

    def load(self, filename):
        with open(filename, 'rb') as fi:
            assert fi.readline().strip() == b'P4'
            while True:
                line = fi.readline()
                if not line.startswith(b'#'):
                    self.size = tuple(map(int, line.split()))
                    break
            self.update()
            self.data = bitarray(endian='big')
            self.data.fromfile(fi)
            fi.close()
        assert self.data.nbytes == self.bytes

    def address(self, pt):
        x, y = pt
        return x + self.bits_per_row * y

    def __getitem__(self, pt):
        return self.data[self.address(pt)]

    def __setitem__(self, pt, val):
        self.data[self.address(pt)] = val


if __name__ == '__main__':
    # draw picture with straight line from (10, 10) to (390, 390)
    a = PBM(500, 400)
    a.info()
    a.clear()
    for x in range(10, 391):
        a[x, x] = True
    a.save('pic1.ppm')

    # copy the picture
    b = PBM()
    b.load('pic1.ppm')
    b.save('pic2.ppm')

    # draw a straight line from (490, 10) to (110, 390) on top
    for i in range(381):
        b[490 - i, 10 + i] = 1
    b.save('pic3.ppm')
