#!/usr/bin/env python
import sys
import os
import re
import doctest
import shutil
import subprocess

subprocess.call([sys.executable, 'setup.py', 'build_ext', '--inplace'])
sys.path.insert(0, os.getcwd())
import bitarray

assert bitarray.test().wasSuccessful()

print 'Backup README'
shutil.copyfile('README', 'README.bak')

print 'Writing new README'
fo = open('README', 'w')

# Copy the everything before 'Reference' while substituting the version number
pat = re.compile(r'(bitarray.+?)(\d+\.\d+\.\d+)')
for line in open('README.bak'):
    if line == 'Reference\n':
        break
    fo.write(pat.sub(lambda m: m.group(1) + bitarray.__version__, line))

def writedoc(name):
    print name,
    doc = eval('bitarray.%s.__doc__' % name)
    lines = doc.splitlines()
    fo.write('``%s``\n' % lines[0])
    assert lines[1] == ''
    for line in lines[2:]:
        fo.write('   %s\n' % line)
    fo.write('\n\n')

fo.write("""\
Reference
---------

**The bitarray class:**

""")

writedoc('bitarray')

fo.write("""\
**A bitarray object supports the following methods:**

""")

for method in sorted(dir(bitarray.bitarray)):
    if method.startswith('_'):
        continue
    writedoc('bitarray.%s' % method)

fo.write("""\
**Functions defined in the module:**

""")

writedoc('test')

writedoc('bits2bytes')

fo.close()
print

print 'doctest'
doctest.testfile('README')

# --------- html

print 'Writing html'
os.system('rst2html.py README >README.html')
