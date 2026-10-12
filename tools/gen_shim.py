"""Writes build/shim_data.h: shim bytes from build/shim.bin and the offsets of its data fields from build/shim.out."""
import re
import subprocess
import sys
from pathlib import Path

build = Path(__file__).resolve().parent.parent / 'build'
code = (build / 'shim.bin').read_bytes()
syms = {}
for line in subprocess.check_output([sys.argv[1] if len(sys.argv) > 1 else 'nm', str(build / 'shim.out')], text=True).splitlines():
    m = re.match(r'([0-9a-f]+)\s+\w\s+(\w+)$', line)
    if m:
        syms[m.group(2)] = int(m.group(1), 16)
fields = ('prev', 'printf_ptr', 'attr_off', 'four_args', 'magic')
assert syms.get('_start') == 0, syms
assert all(f in syms for f in fields), syms
assert code[syms['magic']:syms['magic'] + 8] == b'NEOENBL2'
out = ['static const unsigned char shim_code[%d] = {%s};' % (len(code), ','.join(hex(b) for b in code))]
out += ['#define SHIM_%s %d' % (f.upper(), syms[f]) for f in fields]
(build / 'shim_data.h').write_text('\n'.join(out) + '\n')
print('shim_data.h: %d bytes, %s' % (len(code), ', '.join('%s=%d' % (f, syms[f]) for f in fields)))
