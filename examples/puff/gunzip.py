import sys
import datetime
import zlib
from pprint import pprint

from bitarray import bitarray

from puff import Puff


class GunZip(Puff):

    operating_system = {
        0: "FAT",      1: "Amiga",          2: "VMS",      3: "Unix",
        4: "VM/CMS",   5: "Atari TOS",      6: "HPFS",     7: "Macintosh",
        8: "Z-System", 9: "CP/M",          10: "TOPS-20", 11: "NTFS",
        12: "QDOS",   13: "Acorn RISCOS", 255: "Unknown",
    }

    def read_nul_terminated_string(self) -> str:
        a = bytearray()
        while True:
            b: int = self.read_uint(8)
            if b == 0:
                return a.decode("UTF-8")
            a.append(b)

    def read_header(self, verbose=False) -> None:

        def vprint(txt):
            if verbose:
                print(txt)

        if self.read_uint(16) != 0x8b1f:
            raise ValueError("Invalid GZIP magic number")

        cmeth = self.read_uint(8)
        if cmeth != 8:
            raise ValueError(f"Unsupported compression method: {str(cmeth)}")

        # reserved flags
        flags: int = self.read_uint(8)
        if flags & 0xe0 != 0:
            vprint("Reserved flags are set")

        # modification time
        mtime = self.read_uint(32)
        if mtime != 0:
            dt = datetime.datetime.fromtimestamp(mtime, datetime.timezone.utc)
            vprint(f"Last modified: {dt}")
        else:
            vprint("Last modified: N/A")

        # extra flags
        extraflags = self.read_uint(8)
        if extraflags == 2:
            vprint("Extra flags: Maximum compression")
        elif extraflags == 4:
            vprint("Extra flags: Fastest compression")
        else:
            vprint(f"Extra flags: Unknown ({extraflags})")

        osbyte = self.read_uint(8)
        osstr: str = self.operating_system.get(osbyte, "Really unknown")
        vprint(f"Operating system: {osstr}")

        # handle assorted flags
        if flags & 0x01:
            vprint("Flag: Text")
        if flags & 0x04:
            vprint("Flag: Extra")
            count: int = self.read_uint(16)
            while count > 0:  # Skip extra data
                self.read_uint(8)
                count -= 1
        if flags & 0x08:
            vprint(f"File name: {self.read_nul_terminated_string()}")
        if flags & 0x02:
            vprint(f"Header CRC-16: {self.read_uint(16):04X}")
        if flags & 0x10:
            vprint(f"Comment: {self.read_nul_terminated_string()}")

    def check_footer(self, decomp):
        self.align_byte_boundary()

        crc = self.read_uint(32)
        size = self.read_uint(32)

        # check decompressed data's length and CRC
        if size != len(decomp):
            raise ValueError(f"Size mismatch: expected={size}, "
                             f"actual={len(decomp)}")

        actualcrc = zlib.crc32(decomp) & 0xffffffff
        if crc != actualcrc:
            raise ValueError(f"CRC-32 mismatch: expected={crc:08X}, "
                             f"actual={actualcrc:08X}")

def print_dot(*args):
    sys.stdout.write('.')
    sys.stdout.flush()

def decompress_file(infile, outfile, opts):
    # read input file and store content in little endian bitarray
    input_bits = bitarray(0, 'little')
    with open(infile, "rb") as fi:
        input_bits.fromfile(fi)

    # gunzip: the output is accumulated in a bytearray
    output = bytearray()
    d = GunZip(input_bits, output)
    d.read_header(verbose=opts.verbose)
    stats = d.process_blocks(print_dot if opts.progress else None)
    d.check_footer(output)

    if opts.progress:
        sys.stdout.write('\n')
    if opts.stats:
        pprint(stats)

    # write output to file
    with open(outfile, "wb") as fo:
        fo.write(output)


def main():
    from argparse import ArgumentParser

    p = ArgumentParser()

    p.add_argument('-p', '--progress', action="store_true",
                   help="show progress while decoding")

    p.add_argument('-s', '--stats', action="store_true",
                   help="show block statistics")

    p.add_argument('-v', '--verbose', action="store_true")

    p.add_argument('-o', '--out', action="store", dest='dst',
                   help='output filename')

    p.add_argument(dest='src', metavar='SRC')

    args = p.parse_args()

    if args.dst is None:
        if args.src.endswith('.gz'):
            args.dst = args.src[:-3]
        elif args.src.endswith('.tgz'):
            args.dst = '%s.tar' % args.src[:-4]
        else:
            p.error('cannot guess uncompressed filename from %r, '
                    'please provide -o/-out option' % args.src)

    decompress_file(args.src, args.dst, args)


if __name__ == "__main__":
    main()
