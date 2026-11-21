"""Compile complete Compat with only kernel/IOKit includes supplied by mocks.

Export asm names and all production function bodies are retained. KMOD's loader
registration is a no-op on the host; actual bundle/link checks remain separate.
"""
from pathlib import Path
import subprocess
import re

ROOT = Path(__file__).resolve().parents[1]


def compile_fixture(directory, fixture, release=(25, 6, 9)):
    directory = Path(directory)
    for name in ['IOKit/IOBufferMemoryDescriptor.h', 'IOKit/IOLib.h',
                 'IOKit/pci/IOPCIDevice.h', 'libkern/c++/OSData.h',
                 'kern/clock.h', 'mach/kmod.h', 'IOKit/IORegistryEntry.h']:
        path = directory/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "compat_kernel_fixture.hpp"\n')
    cpp, exe = directory/'test.cpp', directory/'test'
    # Execute the accepted pluginStart body and the real Compat consumer.
    # Lilu's shouldLoad policy and OSKext linking remain external boundaries.
    start = (ROOT/'WhateverXe/kern_start.cpp').read_text()
    activation = re.search(r'\[\]\(\) \{.*?\n\t\}', start, re.S)[0]
    major, minor, patch = release
    cpp.write_text('#include "compat_kernel_fixture.hpp"\n'
                   '#include "kern_graphics_activation.hpp"\n'
                   '#include "kern_xelp_os.hpp"\n'
                   'enum KernelVersion : unsigned { Tahoe = 25 };\n'
                   f'KernelVersion getKernelVersion(){{return static_cast<KernelVersion>({major});}}\n'
                   f'extern "C" const char version[]="Darwin Kernel Version {major}.{minor}.{patch}: fixture/RELEASE_X86_64";\n'
                   'static unsigned hostStarts=0;\n'
                   'struct {void init(){++hostStarts;}} host;\n'
                   'void activateFixturePlugin(){('+activation+')();}\n'+fixture)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I'+str(directory), '-I'+str(ROOT/'tests'), '-I'+str(ROOT),
                    '-I'+str(ROOT/'AppleIntelTGLCompat'), '-I'+str(ROOT/'WhateverXe'),
                    str(cpp), '-o', str(exe)], check=True)
    return exe
