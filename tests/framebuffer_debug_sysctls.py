#!/usr/bin/env python3
"""Verify native intelfb debug node registration across framebuffer starts."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
names = ('processFramebufferDebugSysctls', 'wrapFramebufferRegisterDebugSysctls',
         'wrapFramebufferUnregisterDebugSysctls')
methods = '\n'.join(re.search(rf'(?:bool|void) XE::{name}\(.*?\n\}}', source, re.S)[0]
                    for name in names)
fixture = Path(__file__).with_name('framebuffer_debug_sysctls_fixture.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='framebuffer-debug-sysctls-') as tmp:
    folder = Path(tmp)
    cpp = folder / 'test.cpp'
    cpp.write_text(fixture.replace('// PRODUCTION_METHODS', methods))
    exe = folder / 'test'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-fsanitize=address,undefined',
                    '-I' + str(root / 'WhateverXe'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe), *sys.argv[1:]], check=True)
