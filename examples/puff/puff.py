from collections import Counter, defaultdict

from bitarray import bitarray

from _puff import State, _set_bato

# tell the _puff extension what the bitarray type object is, such that it
# can check for instances thereof
_set_bato(bitarray)


class Puff(State):

    MAXLCODES = 286  # maximum number of literal/length codes
    MAXDCODES =  30  # maximum number of distance codes
    FIXLCODES = 288  # number of fixed literal/length codes

    # fixed literal/lengths and distance lengths
    FIXED_LENGTHS = tuple(
        # literal/length lengths (FIXLCODES elements)
        [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8 +
        # distance lengths (32 elements)
        [5] * 32
    )

    def process_blocks(self, callback=None):
        self.stats = defaultdict(Counter)

        while True:
            # read the three bit block header
            last = self.read_uint(1)   # 1 if last block
            btype = self.read_uint(2)  # block type
            self.stats['btype'][btype] += 1

            if btype == 0:                      # process stored block
                self.process_stored_block()

            elif btype == 1:                    # process fixed block
                self.decode_block(self.FIXED_LENGTHS, self.FIXLCODES, 32)

            elif btype == 2:                    # process dynamic block
                self.process_dynamic_block()

            else:
                assert btype == 3, "Impossible block type"
                raise ValueError("Reserved block type")

            if callback:
                callback(self.stats)

            if last:
                break

        return self.stats

    def process_stored_block(self):  # uncompressed block
        self.align_byte_boundary()

        # read length
        blen: int = self.read_uint(16)
        nlen: int = self.read_uint(16)
        if blen ^ 0xffff != nlen:
            raise ValueError("Invalid length in uncompressed block")

        self.stats['stored blen'][blen] += 1

        # copy bytes
        self.extend_block(blen)

    def process_dynamic_block(self) -> None:
        # permutation of code length codes
        order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                 11, 4, 12, 3, 13, 2, 14, 1, 15]

        # get number of lengths in each table, check lengths
        nlen = self.read_uint(5) + 257
        ndist = self.read_uint(5) + 1
        ncode = self.read_uint(4) + 4
        if nlen > self.MAXLCODES or ndist > self.MAXDCODES:
            raise ValueError("bad counts")

        self.stats['dynamic nlen'][nlen] += 1
        self.stats['dynamic ndist'][ndist] += 1
        self.stats['dynamic ncode'][ncode] += 1

        # read code length code lengths (really), missing lengths are zero
        lengths = 19 * [0]
        for index in range(ncode):
            lengths[order[index]] = self.read_uint(3)

        # decode literal/lengths and distance lengths
        lengths = self.decode_lengths(lengths, nlen, ndist)

        # decode actual block data
        self.decode_block(lengths, nlen, ndist)

    def align_byte_boundary(self):
        # discard bits to align to byte boundary
        skip = 8 - self.get_incnt() % 8  # bits to skip
        if skip != 8:
            self.read_uint(skip)
