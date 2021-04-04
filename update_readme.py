import sys
if sys.version_info[0] != 3:
    sys.exit("This program requires Python 3")

import re
import doctest
from io import StringIO

import bitarray
import bitarray.util


fo = None


sig_pat = re.compile(r'(\w+\([^()]*\))( -> (.+))?')
def write_doc(name):
    doc = eval('bitarray.%s.__doc__' % name)
    assert doc, name
    lines = doc.splitlines()
    m = sig_pat.match(lines[0])
    if m is None:
        raise Exception("signature line invalid: %r" % lines[0])
    s = '`%s`' %  m.group(1)
    if m.group(3):
        s += ' -> %s' % m.group(3)
    fo.write(s + '\n\n')
    assert lines[1] == ''
    for line in lines[2:]:
        fo.write(line.rstrip() + '\n')
    fo.write('\n\n')


def write_reference():
    fo.write("Reference\n"
             "=========\n\n"
             "The bitarray object:\n"
             "--------------------\n\n")
    write_doc('bitarray')

    fo.write("**A bitarray object supports the following methods:**\n\n")
    for method in sorted(dir(bitarray.bitarray)):
        if method.startswith('_'):
            continue
        write_doc('bitarray.%s' % method)

    fo.write("""\
The frozenbitarray object:
--------------------------

This object is very similar to the bitarray object.  The difference is that
this a frozenbitarray is immutable, and hashable:

    >>> from bitarray import frozenbitarray
    >>> a = frozenbitarray('1100011')
    >>> a[3] = 1
    Traceback (most recent call last):
      File "<stdin>", line 1, in <module>
      File "bitarray/__init__.py", line 40, in __delitem__
        raise TypeError("'frozenbitarray' is immutable")
    TypeError: 'frozenbitarray' is immutable
    >>> {a: 'some value'}
    {frozenbitarray('1100011'): 'some value'}

""")
    write_doc('frozenbitarray')

    fo.write("""\
The decodetree object:
----------------------

This (immutable and unhashable) object stores a binary tree initialized
from a prefix code dictionary.  It's sole purpose is to be passed to
bitarray's `.decode()` and `.iterdecode()` methods, instead of passing
the prefix code dictionary to those methods directly:

    >>> from bitarray import bitarray, decodetree
    >>> t = decodetree({'a': bitarray('0'), 'b': bitarray('1')})
    >>> a = bitarray('0110')
    >>> a.decode(t)
    ['a', 'b', 'b', 'a']
    >>> ''.join(a.iterdecode(t))
    'abba'

""")
    write_doc('decodetree')

    fo.write("Functions defined in the `bitarray` module:\n"
             "--------------------------------------------\n\n")
    for func in sorted(['test', 'bits2bytes', 'get_default_endian']):
        write_doc(func)

    fo.write("Functions defined in `bitarray.util` module:\n"
             "--------------------------------------------\n\n")
    for func in bitarray.util.__all__:
        write_doc('util.%s' % func)


def write_all(data):
    ver_pat = re.compile(r'(bitarray.+?)(\d+\.\d+\.\d+)')
    for line in data.splitlines():
        if line == 'Reference':
            break
        line = ver_pat.sub(lambda m: m.group(1) + bitarray.__version__, line)
        fo.write(line + '\n')

    write_reference()
    url = "https://github.com/ilanschnell/bitarray/blob/master/CHANGELOG.md"
    fo.write('Finally the [change log](%s).\n' % url)


def issue_replace(match):
    url = "ilanschnell/bitarray#%s" % match.group(1)
    return "[%s](%s)" % (match.group(0), url)

def make_changelog():
    ver_pat = re.compile(r'(\d{4}-\d{2}-\d{2})\s+(\d+\.\d+\.\d+)')
    issue_pat = re.compile(r'#(\d+)')

    fo = open('CHANGELOG.md', 'w')
    fo.write("Change log\n"
             "==========\n\n")

    for line in open('CHANGE_LOG'):
        m = ver_pat.match(line)
        if m:
            fo.write(m.expand(r'*\2* (\1):\n'))
        elif line.startswith('-----'):
            fo.write('\n')
        else:
            line = issue_pat.sub(issue_replace, line)
            fo.write(line)

    fo.close()


def main():
    if len(sys.argv) > 1:
        sys.exit("no arguments expected")

    with open('README.md', 'r') as fi:
        data = fi.read()

    global fo
    with StringIO() as fo:
        write_all(data)
        new_data = fo.getvalue()
        fo.close()

    if new_data == data:
        print("already up-to-date")
    else:
        with open('README.md', 'w') as f:
            f.write(new_data)

    make_changelog()
    doctest.testfile('README.md')
    doctest.testfile('examples/represent.md')


if __name__ == '__main__':
    main()
