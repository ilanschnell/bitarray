import os
import re
import doctest

import bitarray


fo = None


sig_pat = re.compile(r'(\w+\([^()]*\))( -> (.+))?')
def write_doc(name):
    doc = eval('bitarray.%s.__doc__' % name)
    lines = doc.splitlines()
    m = sig_pat.match(lines[0])
    if m is None:
        raise Exception("signature line invalid: %r" % lines[0])
    s = '``%s``' %  m.group(1)
    if m.group(3):
        s += ' -> %s' % m.group(3)
    fo.write(s + '\n')
    assert lines[1] == ''
    for line in lines[2:]:
        fo.write('   %s\n' % line)
    fo.write('\n\n')


def write_reference():
    fo.write("""\
Reference
---------

**The bitarray class:**

""")
    write_doc('bitarray')

    fo.write("""\
**A bitarray object supports the following methods:**

""")
    for method in sorted(dir(bitarray.bitarray)):
        if method.startswith('_'):
            continue
        write_doc('bitarray.%s' % method)

    fo.write("""\
**Functions defined in the module:**

""")
    write_doc('test')
    write_doc('bits2bytes')



ver_pat = re.compile(r'(bitarray.+?)(\d+\.\d+\.\d+)')
def write_all(data):
    for line in data.splitlines():
        if line == 'Reference':
            break
        line = ver_pat.sub(lambda m: m.group(1) + bitarray.__version__, line)
        fo.write(line + '\n')

    write_reference()


def main():
    global fo

    data = open('README.rst').read()
    fo = open('README.rst', 'w')
    write_all(data)
    fo.close()

    doctest.testfile('README.rst')
    os.system('rst2html.py README.rst >README.html')


if __name__ == '__main__':
    main()
