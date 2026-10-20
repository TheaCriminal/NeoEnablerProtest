from pathlib import Path
import json
import re
import subprocess
import sys

nm, directory = sys.argv[1:]
p = Path(directory)
code = (p / 'step-shim.bin').read_bytes()
symbols = {}
for line in subprocess.check_output([nm, str(p / 'step-shim.out')], text=True).splitlines():
    match = re.match(r'([0-9a-f]+)\s+\w\s+(\w+)$', line)
    if match:
        symbols[match[2]] = int(match[1], 16)
names = ('original', 'wait_cycles', 'enabled', 'active', 'gate', 'seq', 'response_seq', 'command',
         'phase', 'category', 'result', 'title', 'blocked', 'refused', 'expired', 'dirty', 'magic')
assert symbols['_start'] == 0 and len(code) <= 1024
assert all(name in symbols for name in names)
assert code[symbols['magic']:symbols['magic'] + 8] == b'NEOMS001'
assert symbols['original'] % 8 == symbols['wait_cycles'] % 8 == 0
assert all(symbols[name] % 4 == 0 for name in names[2:])
header = ['#ifndef STEP_SHIM_DATA_H', '#define STEP_SHIM_DATA_H']
header += ['static const unsigned char step_code[] = {' + ','.join(hex(x) for x in code) + '};']
header += [f'#define MS_{name.upper()} {symbols[name]}' for name in names]
header += ['#endif']
(p / 'step_shim_data.h').write_text('\n'.join(header) + '\n')
(p / 'step-symbols.json').write_text(json.dumps({name: symbols[name] for name in names}, indent=2) + '\n')
print(f'Step shim: {len(code)} bytes')
