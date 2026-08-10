import os
import sys
import unittest
from random import getrandbits, randint, randrange, random, sample, choice

from bitarray import bitarray, decodetree, _sysinfo
from bitarray.util import zeros, ones, int2ba, parity, huffman_code

from bitarray.test_bitarray import (Util, urandom_2, ENDIANS, PTRSIZE,
                                    show_info, alphabet_code)

# --------------------- internal C-level debug tests ------------------------

from bitarray._util import (
    _setup_table, _zlw, _adjust_slice,                 # in bitarray.h
    _cfw, _d2i, _read_n, _write_n, _sc_rts, _SEGSIZE,  # in _util.h
)
SEGBITS = 8 * _SEGSIZE

# ---------------------------- bitarray.h -----------------------------------

class SetupTableTests(unittest.TestCase):

    max_char = {'a': 28, 'A': 28, 's': 140, 'S': 140, 'x': 7, 'X': 7,
                'c': 8, 'p': 1, 'r': 255}

    def test_common(self):
        for kop in 'aAsSxXcpr':
            table = _setup_table(kop)
            self.assertIs(type(table), bytes)
            self.assertEqual(len(table), 256)
            self.assertEqual(table[0], 0)  # all tables start with 0
            self.assertEqual(max(table), self.max_char[kop])

    def test_add(self):
        table = _setup_table('a')
        self.assertEqual(max(table), 28)
        self.assertTrue(table[255] == sum(range(8)) == 28)
        self.assertEqual(table[15], 0+1+2+3)

        table = _setup_table('A')
        self.assertEqual(table[15], 4+5+6+7)

        for kop, endian in ('a', 'little'), ('A', 'big'):
            t = _setup_table(kop)
            for i in range(256):
                a = int2ba(i, 8, endian)
                self.assertEqual(t[i], sum(i for i, v in enumerate(a) if v))

    def test_add_sqr(self):
        table = _setup_table('s')
        self.assertEqual(max(table), 140)
        for kop, endian in ('s', 'little'), ('S', 'big'):
            t = _setup_table(kop)
            for i in range(256):
                a = int2ba(i, 8, endian)
                self.assertEqual(t[i],
                                 sum(i * i for i, v in enumerate(a) if v))

    def test_xor(self):
        table = _setup_table('x')
        self.assertEqual(max(table), 7)  # max index is 7
        self.assertTrue(table[255] == 0^1^2^3^4^5^6^7 == 0)
        self.assertEqual(table[2], 1)
        self.assertTrue(table[29] == table[0b11101] == 0^2^3^4 == 5)
        self.assertTrue(table[34] == table[0b100010] == 1^5 == 4)
        self.assertTrue(table[157] == table[0b10011101] == 2^3^4^7 == 2)

        table = _setup_table('X')
        self.assertEqual(table[2], 6)
        self.assertTrue(table[157] == 3^4^5^7 == 5)

        for kop, endian in ('x', 'little'), ('X', 'big'):
            t = _setup_table(kop)
            for i in range(256):
                a = int2ba(i, 8, endian)
                c = 0
                for j, v in enumerate(a):
                    c ^= j * v
                self.assertEqual(t[i], c)

    def test_count(self):
        table = _setup_table('c')
        self.assertEqual(max(table), 8)  # 8 active bits the most
        self.assertEqual(table[255], 8)
        self.assertTrue(table[29] == table[0b11101] == 4)
        for endian in 'little', 'big':
            for i in range(256):
                a = int2ba(i, 8, endian)
                self.assertEqual(table[i], a.count())

    def test_parity(self):
        table = _setup_table('p')
        self.assertEqual(max(table), 1)
        self.assertEqual(table[254], 1)
        self.assertEqual(table[255], 0)
        for endian in 'little', 'big':
            for i in range(256):
                a = int2ba(i, 8, endian)
                self.assertEqual(table[i], parity(a))

    def test_reverse(self):
        table = _setup_table('r')
        self.assertEqual(max(table), 255)
        self.assertEqual(table[255], 255)  # reversed is still 255
        self.assertEqual(table[1], 128)
        for i in range(256):
            j = table[i]
            self.assertEqual(table[j], i)
            self.assertEqual(int2ba(i, 8, 'little'), int2ba(j, 8, 'big'))

    def test_opposite_endian(self):
        reverse_trans = _setup_table('r')
        for kop1, kop2 in 'aA', 'xX', 'sS':
            a = _setup_table(kop1)
            b = _setup_table(kop2)
            for i in range(256):
                j = reverse_trans[i]
                self.assertEqual(a[i], b[j])


class ZLW_Tests(unittest.TestCase, Util):

    def test_zeros(self):
        for n in range(200):
            a = zeros(n, choice(ENDIANS))
            self.assertEqual(_zlw(a), zeros(64))

    def test_ones(self):
        for n in range(200):
            a = ones(n, choice(ENDIANS))
            b = _zlw(a)
            self.assertEqual(len(b), 64)
            self.assertEqual(b.endian, a.endian)
            r = n % 64
            self.assertEqual(b, ones(r) + zeros(64 - r))

    def test_random(self):
        for n in range(200):
            a = urandom_2(n)
            b = _zlw(a)
            self.assertEqual(len(b), 64)
            self.assertEqual(a.endian, b.endian)
            self.assertEqual(b[63], 0)  # last bit is always 0
            q, r = divmod(n, 64)
            self.assertEqual(b, a[64 * q:] + zeros(64 - r))


class Adjust_Slice_Tests(unittest.TestCase):

    def test_explicit(self):
        A = _adjust_slice
        # already normalized, slice all elements, step = 1: do nothing
        self.assertEqual(A(19, 0, 19, 1), (19, 0, 19, 1))

        # only normalize start and stop
        self.assertEqual(A(20, -99, 99, 1), (20, 0, 20, 1))

        # already normalized, but slice selects only 6 elements
        self.assertEqual(A(19, 1, 17, 3), (6, 1, 17, 3))

        # stop not normalized, slice selects 6 elements again
        self.assertEqual(A(19, 1, -2, 3), (6, 1, 17, 3))

        # general case, negative step
        self.assertEqual(list(range(10)[-2:3:-2]), [8, 6, 4])
        self.assertEqual(A(10, -2, 3, -2), (3, 4, 9, 2))

        # slicelength = 0
        self.assertEqual(A(23, 17, 3, 1), (0, 17, 3, 1))
        self.assertEqual(A(27, -1, 4, 2), (0, 26, 4, 2))
        self.assertEqual(A(19, 0, 19, -1), (0, 1, 1, 1))
        self.assertEqual(A(21, 0, 17, -3), (0, 3, 1, 3))

    def test_random(self):
        for _ in range(10_000):
            n = randrange(0, 100)  # length
            s = slice(randint(-n - 3, n + 3),
                      randint(-n - 3, n + 3),
                      randint(-5, 5) or -1)
            r = range(n)[s]

            # test Python's own behavior
            self.assertEqual(r, range(*s.indices(n)))
            self.assertEqual(r, range(r.start, r.stop, r.step))
            self.assertEqual(s.indices(n), (r.start, r.stop, r.step))

            res = _adjust_slice(n, s.start, s.stop, s.step)
            slicelength, start, stop, step = res

            self.assertEqual(slicelength, len(r))
            # Unlike PySlice_AdjustIndices(), _adjust_slice() also
            # adjusts the step to be positive.  It preserves selected
            # indices but may reverse their order.
            self.assertGreater(step, 0)

            r2 = range(start, stop, step)
            self.assertEqual(r2, r if r.step > 0 else r[::-1])
            self.assertEqual(set(r), set(r2))


# ----------------------------  _bitarray.c  --------------------------------

class SysInfo_Tests(unittest.TestCase):

    def test_debug(self):
        self.assertTrue(_sysinfo("DEBUG"))


class ShiftR8_Tests(unittest.TestCase, Util):

    def test_empty(self):
        a = bitarray()
        a._shift_r8(0, 0, 3)
        self.assertEqual(a, bitarray())

    def test_explicit(self):
        x = bitarray('11000100 11111111 11100111 10111111 00001000')
        y = bitarray('11000100 00000111 11111111 00111101 00001000')
        x._shift_r8(1, 4, 5)
        self.assertEqual(x, y)
        x._shift_r8(2, 1, 5)  # start > stop  --  do nothing
        self.assertEqual(x, y)
        x._shift_r8(0, 5, 0)  # shift = 0  --  do nothing
        self.assertEqual(x, y)

        x = bitarray('11000100 11110')
        y = bitarray('00011000 10011')
        x._shift_r8(0, 2, 3)
        self.assertEqual(x, y)

        x = bitarray('1100011')
        y = bitarray('0110001')
        x._shift_r8(0, 1, 1)
        self.assertEqual(x, y)

    def test_random(self):
        for _ in range(2000):
            n = randrange(200)
            x = urandom_2(n)
            a = randint(0, x.nbytes)
            b = randint(a, x.nbytes)
            k = randrange(8)
            y = x.copy()
            y[8 * a : 8 * b] >>= k
            s = x.to01()
            if a < b:
                s = s[:8 * a] + k * "0" + s[8 * a : 8 * b - k] + s[8 * b:]
                if 8 * b > n:
                    s = s[:n]
            x._shift_r8(a, b, k)
            self.assertEqual(x.to01(), s)
            self.assertEqual(x, y)
            self.assertEqual(x.endian, y.endian)
            self.assertEqual(len(x), n)


class CopyN_Tests(unittest.TestCase, Util):

    def test_explicit(self):
        x = bitarray('11000100 11110')
        #                 ^^^^ ^
        y = bitarray('0101110001')
        #              ^^^^^
        x._copy_n(4, y, 1, 5)
        self.assertEqual(x, bitarray('11001011 11110'))
        #                                 ^^^^ ^
        x = bitarray('10110111 101', 'little')
        y = x.copy()
        x._copy_n(3, x, 3, 7)  # copy region of x onto x
        self.assertEqual(x, y)
        x._copy_n(3, bitarray(x, 'big'), 3, 7)  # as before but other endian
        self.assertEqual(x, y)
        x._copy_n(5, bitarray(), 0, 0)  # copy empty bitarray onto x
        self.assertEqual(x, y)

    def test_example(self):
        # example given in devel/copy_n.py
        y = bitarray(
            '00101110 11111001 01011101 11001011 10110000 01011110 011')
        x =  bitarray(
            '01011101 11100101 01110101 01011001 01110100 10001010 01111011')
        x._copy_n(21, y, 6, 31)
        self.assertEqual(x, bitarray(
            '01011101 11100101 01110101 11110010 10111011 10010111 01101011'))

    def check_copy_n(self, N, M, a, b, n):
        x = urandom_2(N)
        x_lst = x.tolist()
        y = x if M < 0 else urandom_2(M)
        y_lst = y.tolist()
        x_lst[a:a + n] = y_lst[b:b + n]
        x._copy_n(a, y, b, n)
        self.assertEqual(x, bitarray(x_lst))
        self.assertEqual(len(x), N)
        self.check_obj(x)

        if M < 0:
            return
        self.assertEqual(y, bitarray(y_lst))
        self.assertEqual(len(y), M)
        self.check_obj(y)

    def test_random(self):
        for _ in range(1000):
            N = randrange(1000)
            n = randint(0, N)
            a = randint(0, N - n)
            b = randint(0, N - n)
            self.check_copy_n(N, -1, a, b, n)

            M = randrange(1000)
            n = randint(0, min(N, M))
            a = randint(0, N - n)
            b = randint(0, M - n)
            self.check_copy_n(N, M, a, b, n)

    @staticmethod
    def getslice(a, start, slicelength):
        # this is the Python equivalent of __getitem__ for slices with step=1
        b = bitarray(slicelength, a.endian)
        b._copy_n(0, a, start, slicelength)
        return b

    def test_getslice(self):
        for a in self.randombitarrays():
            a_lst = a.tolist()
            n = len(a)
            i = randint(0, n)
            j = randint(i, n)
            b = self.getslice(a, i, j - i)
            self.assertEqual(b.tolist(), a_lst[i:j])
            self.assertEQUAL(b, a[i:j])


class Overlap_Tests(unittest.TestCase, Util):

    def check_overlap(self, a, b, res):
        self.assertIs(a._overlap(b), res)
        self.assertIs(b._overlap(a), res)  # symmetry
        self.check_obj(a)
        self.check_obj(b)

    def test_empty(self):
        a = bitarray()
        self.check_overlap(a, a, False)
        b = bitarray()
        self.check_overlap(a, b, False)

    def test_distinct(self):
        for a in self.randombitarrays():
            # buffer overlaps with itself, unless buffer is NULL
            self.check_overlap(a, a, bool(a))
            b = a.copy()
            self.check_overlap(a, b, False)

    def test_shared(self):
        a = bitarray(64)
        b = bitarray(buffer=a)
        self.check_overlap(b, a, True)

        c = bitarray(buffer=memoryview(a)[2:4])
        self.check_overlap(c, a, True)

        d = bitarray(buffer=memoryview(a)[5:])
        self.check_overlap(d, c, False)
        self.check_overlap(d, b, True)

        e = bitarray(buffer=memoryview(a)[3:3])
        self.check_overlap(e, c, False)
        self.check_overlap(e, d, False)

    def test_shared_random(self):
        n = 100  # buffer size in bytes
        a = bitarray(8 * n)
        for _ in range(1000):
            i1 = randint(0, n)
            j1 = randint(i1, n)
            b1 = bitarray(buffer=memoryview(a)[i1:j1])

            i2 = randint(0, n)
            j2 = randint(i2, n)
            b2 = bitarray(buffer=memoryview(a)[i2:j2])

            r1, r2 = range(i1, j1), range(i2, j2)
            res = bool(r1) and bool(r2) and (i2 in r1 or i1 in r2)
            self.check_overlap(b1, b2, res)


class Decodetree_Getnode_Tests(unittest.TestCase):

    child_names = ("left", "right")

    def check_internal_node(self, nd):
        self.assertIs(type(nd), dict)
        self.assertIn(len(nd), (1, 2))
        for key, counts in nd.items():
            self.assertIs(type(key), str)
            self.assertIn(key, self.child_names)
            self.assertIs(type(counts), tuple)
            self.assertEqual(len(counts), 3)
            for i in range(3):
                self.assertIs(type(counts[i]), int)
            # every binary tree has one fewer two-child nodes than leaf nodes
            self.assertEqual(counts[1], counts[2] - 1)

    def check_subtree(self, tree, code, prefix):
        nd = tree._getnode(prefix)
        it = prefix.decode(tree)
        if type(nd) is not dict:  # leaf node
            self.assertIn(nd, code)
            self.assertEqual(prefix, code[nd])
            self.assertEqual(list(it), [nd])
            return 0, 0, 1

        self.check_internal_node(nd)
        self.assertRaises(ValueError if prefix else StopIteration, next, it)
        counts = [0, 0, 0]
        counts[len(nd) - 1] = 1  # count this one- or two-child node
        for k, key in enumerate(self.child_names):
            if key in nd:
                child_prefix = prefix + bitarray([k])
                child_counts = self.check_subtree(tree, code, child_prefix)
                self.assertEqual(nd[key], child_counts)
                for i in range(3):
                    counts[i] += child_counts[i]
        return tuple(counts)

    def test_simple(self):
        code = {'a': bitarray('0'), 'b': bitarray('1')}
        tree = decodetree(code)
        counts = self.check_subtree(tree, code, bitarray())
        self.assertEqual(counts, tree.nodes())
        self.assertEqual(counts, (0, 1, 2))

        root = tree._getnode(bitarray())
        self.check_internal_node(root)
        self.assertEqual(root, {'left': (0, 0, 1), 'right': (0, 0, 1)})

        self.assertEqual(tree._getnode(bitarray('0')), 'a')
        self.assertEqual(tree._getnode(bitarray('1')), 'b')
        self.assertRaises(ValueError, tree._getnode, bitarray('00'))

    def test_mortal_symbol(self):
        symbol = object()
        bit = urandom_2(1)
        self.assertEqual(len(bit), 1)
        code = {symbol: bit}
        tree = decodetree(code)
        n = sys.getrefcount(symbol)
        self.assertIs(tree._getnode(bit), symbol)
        self.assertEqual(sys.getrefcount(symbol), n)

        counts = self.check_subtree(tree, code, bitarray())
        self.assertEqual(counts, tree.nodes())

    def test_alphabet_code(self):
        tree = decodetree(alphabet_code)
        counts = self.check_subtree(tree, alphabet_code, bitarray())
        self.assertEqual(counts, tree.nodes())

        for sym, word in alphabet_code.items():
            self.assertEqual(tree._getnode(word), sym)
            for i in range(len(word)):
                p = word[:i]  # partial word
                self.check_internal_node(tree._getnode(p))

    def test_max_chain(self):
        word = urandom_2(256)
        code = {123: word}
        tree = decodetree(code)
        self.assertIs(tree._getnode(word), 123)
        for i in range(256):
            nd = tree._getnode(word[:i])
            self.assertIs(type(nd), dict)  # internal node
            self.assertEqual(nd, {self.child_names[word[i]]:
                                  (255 - i, 0, 1)})

        # invert any bit and we get off the chain
        for i in range(256):
            a = word.copy()
            a.invert(i)
            self.assertRaises(ValueError, tree._getnode, a)
            a.invert(i)
            self.assertIs(tree._getnode(a), 123)

        counts = self.check_subtree(tree, code, bitarray())
        self.assertEqual(counts, tree.nodes())

    def test_huffman(self):
        N = randrange(1000, 2000)
        freq = {i: random() ** 3 for i in range(N)}
        code = huffman_code(freq)
        tree = decodetree(code)
        # the following is true for every complete tree
        self.assertEqual(tree.nodes(), (0, N - 1, N))
        nd = tree._getnode(bitarray())  # root
        self.assertTrue(all(nd[k][0] == 0 for k in self.child_names))
        # excluding the root, full tree with N leaves has N-2 internal nodes
        self.assertEqual(sum(nd[k][1] for k in self.child_names), N - 2)
        self.assertEqual(sum(nd[k][2] for k in self.child_names), N)

        for sym, word in code.items():
            self.assertEqual(tree._getnode(word), sym)
            n = len(word)
            p = word[:randrange(n)]
            nd = tree._getnode(p)
            self.check_internal_node(nd)
            for key in self.child_names:
                self.assertEqual(nd[key][0], 0)  # completeness

            a = bitarray(word)
            a.append(getrandbits(1))  # make code too long
            self.assertRaises(ValueError, tree._getnode, a)

        counts = self.check_subtree(tree, code, bitarray())
        self.assertEqual(counts, tree.nodes())

    def test_random_tree(self):
        for _ in range(100):
            N = randrange(5, 100)
            freq = {i: random() ** 3 for i in range(N)}
            code = huffman_code(freq)

            # retain a random sample of the original prefixes
            prefixes = sample(list(code.values()), randrange(1, N))
            code = dict(enumerate(prefixes))

            self.assertTrue(1 <= len(code) < N)

            tree = decodetree(code)
            n1, n2, ns = tree.nodes()
            # the sampled code is incomplete
            self.assertGreater(n1, 0)
            # every binary tree has one fewer two-child nodes than leaf nodes
            self.assertEqual(n2, ns - 1)
            # every retained prefix produces one leaf
            self.assertEqual(ns, len(code))

            counts = self.check_subtree(tree, code, bitarray())
            self.assertEqual(counts, tree.nodes())


# -------------------------------- _util.c ----------------------------------

class CountFromWord_Tests(unittest.TestCase, Util):

    def test_ones_zeros_empty(self):
        for _ in range(1000):
            n = randrange(1024)
            a = ones(n)  # ones
            i = randrange(20)
            self.assertEqual(_cfw(a, i), max(0, n - i * 64))
            a.setall(0)  # zeros
            self.assertEqual(_cfw(a, i), 0)
            a.clear()    # empty
            self.assertEqual(_cfw(a, i), 0)

    def test_random(self):
        for _ in range(1000):
            n = randrange(1024)
            a = urandom_2(n)
            i = randrange(20)
            self.assertEqual(_cfw(a, i), a[64 * i:].count())


class DigitToInt_Tests(unittest.TestCase):

    # digit_to_int()

    alphabets = [
        (1, b'01'),
        (2, b'0123'),
        (3, b'01234567'),
        (4, b'0123456789abcdef'),
        (4, b'0123456789ABCDEF'),
        (5, b'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567'),
        (6, b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef'
            b'ghijklmnopqrstuvwxyz0123456789+/'),
    ]

    def test_alphabets(self):
        for m, alphabet in self.alphabets:
            self.assertEqual(len(alphabet), 1 << m)
            for i, c in enumerate(alphabet):
                self.assertEqual(_d2i(m, bytes([c])), i)

    def test_not_alphabets(self):
        for m, alphabet in self.alphabets:
            for c in range(256):
                if c in alphabet or (m == 4 and c in b'abcdefABCDEF'):
                    continue
                self.assertEqual(_d2i(m, bytes([c])), -1)


class RTS_Tests(unittest.TestCase):

    # _sc_rts()   (running totals debug test)

    def test_segsize(self):
        self.assertIs(type(_SEGSIZE), int)
        self.assertIn(_SEGSIZE, (8, 16, 32))

    def test_empty(self):
        rts = _sc_rts(bitarray())
        self.assertEqual(len(rts), 1)
        self.assertEqual(rts, [0])

    @unittest.skipIf(SEGBITS != 256, "SEGBITS mismatch")
    def test_example(self):
        # see example before sc_calc_rts() in _util.c
        a = zeros(987)
        a[[0, 17, 31, 149, 255, 512, 637, 767, 768, 813, 899, 986]] = 1
        self.assertEqual(a.count(), 12)
        rts = _sc_rts(a)
        self.assertIs(type(rts), list)
        self.assertEqual(len(rts), 5)
        self.assertEqual(rts, [0, 5, 5, 8, 12])

    @staticmethod
    def nseg(a):  # number of segments, see also SegmentTests in tricks.py
        return (a.nbytes + _SEGSIZE - 1) // _SEGSIZE

    def test_ones(self):
        for n in range(1000):
            a = ones(n)
            rts = _sc_rts(a)
            self.assertEqual(len(rts), self.nseg(a) + 1)
            self.assertEqual(rts[0], 0)
            self.assertEqual(rts[-1], n)
            for i, v in enumerate(rts):
                self.assertEqual(v, min(SEGBITS * i, n))

    def test_random(self):
        for _ in range(200):
            a = urandom_2(randrange(10000))
            rts = _sc_rts(a)
            self.assertEqual(len(rts), self.nseg(a) + 1)
            self.assertEqual(rts[0], 0)
            self.assertEqual(rts[-1], a.count())
            for i in range(self.nseg(a)):
                self.assertEqual(rts[i + 1] - rts[i],
                                 a[SEGBITS * i : SEGBITS * (i + 1)].count())


class ReadN_WriteN_Tests(unittest.TestCase, Util):

    # Regardless of machine byte-order, read_n() and write_n() use
    # little endian byte-order.

    def test_explicit(self):
        for blob, x in [(b"", 0),
                        (b"\x00", 0),
                        (b"\x01", 1),
                        (b"\xff", 255),
                        (b"\xff\x00", 255),
                        (b"\xaa\xbb\xcc", 0xccbbaa)]:
            n = len(blob)
            self.assertEqual(_read_n(iter(blob), n), x)
            self.assertEqual(_write_n(n, x), blob)

    def test_zeros(self):
        for n in range(PTRSIZE + 1):
            blob = n * b"\x00"
            self.assertEqual(_read_n(iter(blob), n), 0)
            self.assertEqual(_write_n(n, 0), blob)

    def test_max(self):
        blob = (PTRSIZE - 1) * b"\xff" + b"\x7f"
        self.assertEqual(_read_n(iter(blob), PTRSIZE), sys.maxsize)
        self.assertEqual(_write_n(PTRSIZE, sys.maxsize), blob)

    def test_round_trip_random(self):
        for _ in range(1000):
            n = randint(1, PTRSIZE - 1)
            blob = os.urandom(n)
            i = _read_n(iter(blob), n)
            self.assertEqual(_write_n(n, i), blob)

    def test_read_n_untouch(self):
        it = iter(b"\x00XY")
        self.assertEqual(_read_n(it, 1), 0)
        self.assertEqual(next(it), ord('X'))
        self.assertEqual(_read_n(it, 0), 0)
        self.assertEqual(next(it), ord('Y'))
        self.assertRaises(StopIteration, _read_n, it, 1)

    def test_read_n_item_errors(self):
        for v in -1, 256:
            self.assertRaises(ValueError, _read_n, iter([3, v]), 2)

        for v in None, "F", Ellipsis, []:
            self.assertRaises(TypeError, _read_n, iter([3, v]), 2)

    def test_read_n_negative(self):
        it = iter(PTRSIZE * b"\xff")
        self.assertRaisesMessage(
            ValueError,
            "read %d bytes got negative value: -1" % PTRSIZE,
            _read_n, it, PTRSIZE)

# ---------------------------------------------------------------------------

if __name__ == '__main__':
    show_info()
    unittest.main()
