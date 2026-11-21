#include "kern_xelp_call_target.hpp"
#include "kern_xelp_os.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

using mach_vm_address_t = uintptr_t;
#define arrsize(a) (sizeof(a)/sizeof(a[0]))
#define FunctionCast(fn,address) reinterpret_cast<decltype(&fn)>(address)

static const char *version="Darwin Kernel Version 27.2.0: fixture/RELEASE_X86_64";
static const bool display13TraceEnabled=true;
struct IOLock { bool held {}; };
static IOLock lock;
static bool failLock;
static IOLock *IOLockAlloc() { return failLock ? nullptr : &lock; }
static void IOLockLock(IOLock *mutex) { assert(mutex==&lock && !mutex->held);mutex->held=true; }
static void IOLockUnlock(IOLock *mutex) { assert(mutex==&lock && mutex->held);mutex->held=false; }
struct IORegistryEntry {
    unsigned repeat {};
    static IORegistryEntry *getRegistryRoot() { static IORegistryEntry root;return &root; }
    void setProperty(const char *key,bool value) {
        assert(!lock.held && !strcmp(key,"WEXFramebufferDebugSysctlsReused") && value);
        ++repeat;
    }
};

static int slot=-1;
static unsigned adds,removes;
static void nativeAdd() { assert(lock.held && slot==-1);slot=153;++adds; }
static void nativeRemove() { assert(lock.held && slot==153);slot=-1;++removes; }
struct KernelPatcher {
    struct RouteRequest {
        const char *symbol;
        void (*replacement)();
        uintptr_t &original;
    };
    bool failRoute {},wrongSymbol {};
    unsigned routes {};
    void (*added)() {};
    void (*removed)() {};
    template<class T> T solveSymbol(size_t,const char *s,uintptr_t base,size_t) const {
        if(!strcmp(s,"__Z14registerSysctlv"))return base+0x62b04;
        if(!strcmp(s,"__Z16unregisterSysctlv"))return base+0x62dcd;
        assert(!strcmp(s,"_sysctl__debug_intelfb"));
        return base+0x134788+wrongSymbol;
    }
    void clearError() {}
};

struct XE {
    static XE *callbackXE;
    IOLock *framebufferDebugSysctlLock {};
    bool framebufferDebugSysctlsRegistered {};
    uintptr_t orgFramebufferRegisterDebugSysctls {},orgFramebufferUnregisterDebugSysctls {};
    bool processFramebufferDebugSysctls(KernelPatcher &,size_t,uintptr_t,size_t);
    static void wrapFramebufferRegisterDebugSysctls();
    static void wrapFramebufferUnregisterDebugSysctls();
    bool routeIntelShort(KernelPatcher &p,size_t,KernelPatcher::RouteRequest *r,size_t count,uintptr_t,size_t) {
        assert(count==2 && !strcmp(r[0].symbol,"__Z14registerSysctlv") &&
               !strcmp(r[1].symbol,"__Z16unregisterSysctlv"));
        ++p.routes;
        if(p.failRoute)return false;
        p.added=r[0].replacement;
        p.removed=r[1].replacement;
        r[0].original=reinterpret_cast<uintptr_t>(nativeAdd);
        r[1].original=reinterpret_cast<uintptr_t>(nativeRemove);
        return true;
    }
};
XE *XE::callbackXE;
// PRODUCTION_METHODS

int main(int argc,char **argv) {
    std::vector<uint8_t> image(0x1347d8);
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        assert(file);
        image.assign(std::istreambuf_iterator<char>(file),{});
        assert(image.size()>=0x1347d8);
    } else {
        // Independently disassembled TGL donor entry points, call and OID.
        const uint8_t add[]{0x55,0x48,0x89,0xe5,0x48,0xff,0x05,0x81,0x08,0x08,0x00};
        const uint8_t remove[]{0x55,0x48,0x89,0xe5,0x48,0xff,0x05,0xc0,0x05,0x08,0x00};
        const uint8_t startCall[]{0xe8,0x1a,0x38,0x02,0x00};
        const uint8_t use[]{0x48,0x8d,0x3d,0x45,0x1a,0x0d,0x00,0xe8};
        const uint8_t release[]{0x48,0x8d,0x3d,0x25,0x19,0x0d,0x00,0x5d,0xe9};
        const uint8_t autoNode[]{0xff,0xff,0xff,0xff,0x01,0x00,0xc0,0xc8};
        memcpy(image.data()+0x62b04,add,sizeof(add));
        memcpy(image.data()+0x62dcd,remove,sizeof(remove));
        memcpy(image.data()+0x3f2e5,startCall,sizeof(startCall));
        memcpy(image.data()+0x62d3c,use,sizeof(use));
        memcpy(image.data()+0x62e5c,release,sizeof(release));
        memcpy(image.data()+0x134798,autoNode,sizeof(autoNode));
    }
    const auto base=reinterpret_cast<uintptr_t>(image.data());
    XE x;
    XE::callbackXE=&x;
    KernelPatcher p;
    assert(x.processFramebufferDebugSysctls(p,1,base,image.size()) && p.routes==1);
    p.removed();
    p.added();p.added();
    assert(adds==1 && !removes && slot==153 && IORegistryEntry::getRegistryRoot()->repeat==1);
    p.removed();p.removed();
    assert(adds==1 && removes==1 && slot==-1);
    p.added();p.removed();
    assert(adds==2 && removes==2 && slot==-1);

    for(size_t offset:{0x3f2e5,0x62b04,0x62dcd,0x62d3c,0x62e5c,0x134798}) {
        image[offset]^=1;
        KernelPatcher mutated;
        assert(!x.processFramebufferDebugSysctls(mutated,1,base,image.size()) && !mutated.routes);
        image[offset]^=1;
    }
    KernelPatcher bad;
    assert(!x.processFramebufferDebugSysctls(bad,1,base,0x1347d7) && !bad.routes);
    bad.wrongSymbol=true;
    assert(!x.processFramebufferDebugSysctls(bad,1,base,image.size()) && !bad.routes);
    bad.wrongSymbol=false;failLock=true;
    assert(!x.processFramebufferDebugSysctls(bad,1,base,image.size()) && !bad.routes);
    failLock=false;bad.failRoute=true;
    assert(!x.processFramebufferDebugSysctls(bad,1,base,image.size()) && bad.routes==1);
    version="Darwin Kernel Version 25.6.0: fixture/RELEASE_X86_64";
    assert(x.processFramebufferDebugSysctls(bad,1,base,image.size()) && bad.routes==1);
    puts("Framebuffer debug sysctls: registration, duplicate startup, removal and safe restart");
}
