#!/usr/bin/env python3
"""Exercise Golden Gate's donor routing against the actual Compat relay pool."""
from pathlib import Path
import re
import subprocess
import tempfile
from compat_test_support import ROOT, compile_fixture

source = (ROOT / 'WhateverXe/kern_xe.cpp').read_text()
methods = '\n'.join(
    re.search(rf'(?:mach_vm_address_t|bool) XE::{name}\(.*?\n\}}', source, re.S)[0]
    for name in ('intelBranchTarget', 'routeIntelShort')
)
fixture = Path(__file__).with_name('intel_branch_routing_fixture.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='intel-branch-routing-') as directory:
    exe = compile_fixture(directory, fixture.replace('// PRODUCTION_ROUTING', methods),
                          release=(27, 2, 0))
    subprocess.run([str(exe)], check=True)
