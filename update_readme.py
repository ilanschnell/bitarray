import sys
assert sys.version_info[0] == 3, "This program requires Python 3"

import re
import doctest
from io import StringIO

import bitarray
import bitarray.util


BASE_URL = "https://github.com/ilanschnell/bitarray"


sig_pat = re.compile(r'(\w+\([^()]*\))( -> (.+))?')
def write_doc(fo, name):
    doc = eval('bitarray.%s.__doc__' % name)
    assert doc, name
    lines = doc.splitlines()
    m = sig_pat.match(lines[0])
    if m is None:
        raise Exception("signature line invalid: %r" % lines[0])
    s = '``%s``' %  m.group(1)
    if m.group(3):
        s += ' -> %s' % m.group(3)
    fo.write('%s\n' % s)
    assert lines[1] == ''
    for line in lines[2:]:
        out = line.rstrip()
        fo.write("   %s\n" % out.replace('`', '``') if out else "\n")
    fo.write('\n\n')


def write_reference(fo):
    fo.write("""\
Reference
=========

bitarray version: %s -- `change log <%s>`__

In the following, ``item`` is usually a single bit - an integer 0 or 1.


The bitarray object:
--------------------

""" % (bitarray.__version__, "%s/blob/master/changelog.rst" % BASE_URL))
    write_doc(fo, 'bitarray')

    fo.write("**A bitarray object supports the following methods:**\n\n")
    for method in sorted(dir(bitarray.bitarray)):
        if method.startswith('_'):
            continue
        write_doc(fo, 'bitarray.%s' % method)

    fo.write("Other objects:\n"
             "--------------\n\n")
    write_doc(fo, 'frozenbitarray')
    write_doc(fo, 'decodetree')

    fo.write("Functions defined in the `bitarray` module:\n"
             "-------------------------------------------\n\n")
    for func in sorted(['test', 'bits2bytes', 'get_default_endian']):
        write_doc(fo, func)

    fo.write("Functions defined in `bitarray.util` module:\n"
             "--------------------------------------------\n\n")
    for func in bitarray.util.__all__:
        write_doc(fo, 'util.%s' % func)


def write_readme():
    ver_pat = re.compile(r'(bitarray.+?)(\d+\.\d+\.\d+)')

    with open('README.rst', 'r') as fi:
        data = fi.read()

    with StringIO() as fo:
        for line in data.splitlines():
            if line == 'Reference':
                break
            line = ver_pat.sub(lambda m: m.group(1) + bitarray.__version__,
                               line)
            fo.write("%s\n" % line.rstrip())

        write_reference(fo)
        new_data = fo.getvalue()

    if new_data == data:
        print("already up-to-date")
    else:
        with open('README.rst', 'w') as f:
            f.write(new_data)


def write_changelog():
    ver_pat = re.compile(r'(\d{4}-\d{2}-\d{2})\s+(\d+\.\d+\.\d+)')
    issue_pat = re.compile(r'#(\d+)')
    link_pat = re.compile(r'\[(.+)\]\((.+)\)')

    def issue_replace(match):
        url = "%s/issues/%s" % (BASE_URL, match.group(1))
        return "`%s <%s>`__" % (match.group(0), url)

    with open('changelog.rst', 'w') as fo:
        fo.write("Change log\n"
                 "==========\n\n")

        for line in open('CHANGE_LOG'):
            match = ver_pat.match(line)
            if match:
                fo.write(match.expand(r'**\2** (\1):\n'))
            elif line.startswith('-----'):
                pass #fo.write('\n')
            else:
                out = line.replace('`', '``')
                out = issue_pat.sub(issue_replace, out)
                out = link_pat.sub(
                    lambda m: "`%s <%s>`__" % (m.group(1), m.group(2)), out)
                fo.write(out)


def main():
    if len(sys.argv) > 1:
        sys.exit("no arguments expected")

    write_readme()
    write_changelog()
    doctest.testfile('README.rst')
    doctest.testfile('examples/represent.rst')


if __name__ == '__main__':
    main()
