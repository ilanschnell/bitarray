import re
import sys
from doctest import testfile
from io import StringIO

import bitarray.util


BASE_URL = "https://github.com/ilanschnell/bitarray"

NEW_IN = {
    'bitarray':               '2.3: optional `buffer` argument',
    'bitarray.bytereverse':   '2.2.5: optional start and stop arguments',
    'bitarray.clear':         '1.4',
    'bitarray.count':        ['1.1.0: optional start and stop arguments',
                              '2.3.7: optional step argument',
                              '2.9: add non-overlapping sub-bitarray count'],
    'bitarray.find':         ['2.1',
                              '2.9: add optional keyword argument `right`'],
    'bitarray.frombytes':     '2.5.0: allow bytes-like argument',
    'bitarray.decode':        '3.0: changed to return iterator, '
                              'was `.iterdecode()`',
    'bitarray.index':        ['2.9: add optional keyword argument `right`'],
    'bitarray.search':       ['2.9: optional start and stop arguments - '
                              'add optional keyword argument `right`',
                              '3.0: changed to return iterator, '
                              'was `.itersearch()`'],
    'bitarray.invert':        '1.5.3: optional index argument',
    'bitarray.pack':          '2.5.0: allow bytes-like argument',
    'decodetree':             '1.6',
    'frozenbitarray':         '1.1',
    'get_default_endian':     '1.3',
    'util.any_and':           '2.7',
    'util.ba2base':           '1.9',
    'util.base2ba':           '1.9',
    'util.count_n':           '2.3.6: optional value argument',
    'util.deserialize':      ['1.8',
                              '2.5.0: allow bytes-like argument'],
    'util.intervals':         '2.7',
    'util.ones':              '2.9',
    'util.parity':            '1.9',
    'util.pprint':            '1.8',
    'util.serialize':         '1.8',
    'util.urandom':           '1.7',
    'util.sc_encode':         '2.7',
    'util.sc_decode':         '2.7',
    'util.vl_decode':         '2.2',
    'util.vl_encode':         '2.2',
    'util.canonical_huffman': '2.5',
    'util.canonical_decode':  '2.5',
}

DOCS = {
    'chc': ('Canonical Huffman Coding', 'canonical.rst'),
    'rep': ('Bitarray representations', 'represent.rst'),
    'sc':  ('Compression of sparse bitarrays', 'sparse_compression.rst'),
    'vlf': ('Variable length bitarray format', 'variable_length.rst'),
}

DOC_LINKS = {
    'util.canonical_huffman':  'chc',
    'util.canonical_decode':   'chc',
    'util.ba2base':            'rep',
    'util.base2ba':            'rep',
    'util.deserialize':        'rep',
    'util.serialize':          'rep',
    'util.sc_decode':          'sc',
    'util.sc_encode':          'sc',
    'util.vl_decode':          'vlf',
    'util.vl_encode':          'vlf',
}

NOTES = {
    'bitarray.pack': """\
   This method, as well as the ``.unpack()`` method, are meant for efficient
   transfer of data between bitarray objects to other Python objects (for
   example NumPy's ndarray object) which have a different memory view.""",

    'bitarray.tolist': """\
   Note that the list object being created will require 32 or 64 times more
   memory (depending on the machine architecture) than the bitarray object,
   which may cause a memory error if the bitarray is very large.""",

    'util.count_xor': "   This is also known as the Hamming distance."
}

GETSET = {
    'bitarray.nbytes':     'int',
    'bitarray.padbits':    'int',
    'bitarray.readonly':   'bool',
}

_NAMES = set()

sig_pat = re.compile(r"""
(                # group 1
  (\w+)          # function name, group 2
  \([^()]*\)     # (...)
)
(                # optional group 3
  \s->\s(.+)     # return type, group 4
)?
""", re.VERBOSE)

def get_doc(name):
    parts = name.split('.')
    obj = bitarray
    while parts:
        obj = getattr(obj, parts.pop(0))

    lines = obj.__doc__.splitlines()

    if len(lines) == 1:
        sig = '``%s`` -> %s' % (obj.__name__, GETSET[name])
        return sig, lines

    m = sig_pat.match(lines[0])
    if m is None:
        raise Exception("signature invalid: %r" % lines[0])
    sig = '``%s``' %  m.group(1)
    assert m.group(2) == obj.__name__
    if m.group(4):
        sig += ' -> %s' % m.group(4)
    assert lines[1] == ''
    return sig, lines[2:]


def write_doc(fo, name):
    _NAMES.add(name)
    sig, lines = get_doc(name)
    fo.write(sig + '\n')
    for line in lines:
        out = line.rstrip()
        fo.write("   %s\n" % out.replace('`', '``') if out else "\n")

    note = NOTES.get(name)
    if note:
        fo.write("\n%s\n" % note)

    link = DOC_LINKS.get(name)
    if link:
        title, filename = DOCS[link]
        url = BASE_URL + '/blob/master/doc/' + filename
        fo.write("\n   See also: `%s <%s>`__\n" % (title, url))

    new_in = NEW_IN.get(name)
    if new_in:
        for line in new_in if isinstance(new_in, list) else [new_in]:
            fo.write("\n   New in version %s.\n" % line.replace('`', '``'))

    fo.write('\n\n')


def write_reference(fo):
    fo.write("""\
Reference
=========

bitarray version: %s -- `change log <%s>`__

In the following, ``item`` and ``value`` are usually a single bit -
an integer 0 or 1.

Also, ``sub_bitarray`` refers to either a bitarray, or an ``item``.


The bitarray object:
--------------------

""" % (bitarray.__version__, BASE_URL + "/blob/master/doc/changelog.rst"))
    write_doc(fo, 'bitarray')

    fo.write("bitarray methods:\n"
             "-----------------\n\n")
    for method in sorted(dir(bitarray.bitarray)):
        if method.startswith('_'):
            continue
        name = 'bitarray.%s' % method
        if name not in GETSET:
            write_doc(fo, name)

    fo.write("bitarray data descriptors:\n"
             "--------------------------\n\n"
             "Data descriptors were added in version 2.6.\n\n")
    for getset in sorted(dir(bitarray.bitarray)):
        name = 'bitarray.%s' % getset
        if name in GETSET:
            write_doc(fo, name)

    fo.write("Other objects:\n"
             "--------------\n\n")
    write_doc(fo, 'frozenbitarray')
    write_doc(fo, 'decodetree')

    fo.write("Functions defined in the `bitarray` module:\n"
             "-------------------------------------------\n\n")
    for func in sorted(['test', 'bits2bytes', 'get_default_endian']):
        write_doc(fo, func)

    fo.write("Functions defined in `bitarray.util` module:\n"
             "--------------------------------------------\n\n"
             "This sub-module was added in version 1.2.\n\n")
    for func in bitarray.util.__all__:
        write_doc(fo, 'util.%s' % func)

    for name in list(NEW_IN) + list(DOC_LINKS):
        assert name in _NAMES, name


def update_readme(path):
    ver_pat = re.compile(r'(bitarray.+?)\s(\d+\.\d+\.\d+)')

    with open(path, 'r') as fi:
        data = fi.read()

    with StringIO() as fo:
        for line in data.splitlines():
            if line == 'Reference':
                break
            line = ver_pat.sub(r'\1 ' + bitarray.__version__, line)
            fo.write("%s\n" % line.rstrip())

        write_reference(fo)
        new_data = fo.getvalue()

    if new_data == data:
        print("already up-to-date")
    else:
        with open(path, 'w') as f:
            f.write(new_data)


def write_changelog(fo):
    ver_pat = re.compile(r'(\d{4}-\d{2}-\d{2})\s+(\d+\.\d+\.\d+)')
    hash_pat = re.compile(r'#([0-9a-f]+)')
    link_pat = re.compile(r'\[(.+)\]\((.+)\)')

    def hash_replace(match):
        group1 = match.group(1)
        if len(group1) >= 7:
            if len(group1) != 8:
                print("Warning: commit hash length != 8, got", len(group1))
            url = "%s/commit/%s" % (BASE_URL, group1)
        else:
            url = "%s/issues/%d" % (BASE_URL, int(group1))
        return "`%s <%s>`__" % (match.group(0), url)

    fo.write("Change log\n"
             "==========\n\n")

    for line in open('./CHANGE_LOG'):
        line = line.rstrip()
        match = ver_pat.match(line)
        if match:
            line = match.expand(r'**\2** (\1):')
        elif line.startswith('-----'):
            line = ''
        elif line.startswith('  '):
            line = line[2:]
            line = line.replace('`', '``')
            line = hash_pat.sub(hash_replace, line)
            line = link_pat.sub(r"`\1 <\2>`__", line)
        fo.write(line + '\n')


def main():
    if len(sys.argv) > 1:
        sys.exit("no arguments expected")

    update_readme('./README.rst')
    with open('./doc/reference.rst', 'w') as fo:
        write_reference(fo)
    with open('./doc/changelog.rst', 'w') as fo:
        write_changelog(fo)

    testfile('./README.rst')
    testfile('./doc/buffer.rst')
    testfile('./doc/canonical.rst')
    testfile('./doc/indexing.rst')
    testfile('./doc/represent.rst')
    testfile('./doc/sparse_compression.rst')
    testfile('./doc/variable_length.rst')


if __name__ == '__main__':
    main()
