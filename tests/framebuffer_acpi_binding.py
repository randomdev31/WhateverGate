#!/usr/bin/env python3
"""Exercise ACPI association before Golden Gate's native framebuffer start."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
method = re.search(r'static bool bindFramebufferACPIDevice\(.*?\n\}', source, re.S)[0]
native = source.index('bool XE::wrapDisplay13FramebufferStart(')
gate = re.search(r'    if\(XELP::kernelOS\(version\)==XELP::OS::GoldenGate\).*?\n    }',
                 source[native:source.index('const auto started=FunctionCast', native)], re.S)[0]
if len(sys.argv) > 1:
    donor = Path(sys.argv[1]).read_bytes()
    assert donor[0x3f34e:0x3f35e] == bytes.fromhex('488b07488d3529900600ff90b8020000')
    assert donor[0xa8381:0xa838d] == b'acpi-device\0'

fixture = Path(__file__).with_name('framebuffer_acpi_binding_fixture.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='framebuffer-acpi-') as folder:
    directory = Path(folder)
    cpp = directory / 'test.cpp'
    cpp.write_text(fixture.replace('// ASSOCIATION', method).replace('// START_GATE', gate))
    exe = directory / 'test'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-fsanitize=address,undefined',
                    '-I'+str(root / 'WhateverXe'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
