#include "kern_xelp_os.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define OSDynamicCast(T, value) dynamic_cast<T *>(value)
#define OSSafeReleaseNULL(p) do { if(p) { (p)->release(); p=nullptr; } } while(0)

struct OSObject {
    explicit OSObject(const char *name="OSObject",bool heap=false):name(name),heap(heap){}
    virtual ~OSObject()=default;
    const char *name;
    bool heap;
    unsigned retained=1;
    OSObject *metaCast(const char *kind) { return !strcmp(kind,name) ? this : nullptr; }
    void retain() { ++retained; }
    void release() { assert(retained);if(--retained==0 && heap)delete this; }
};
struct OSNumber:OSObject {
    explicit OSNumber(uint64_t address):address(address){}
    uint64_t address;
    uint64_t unsigned64BitValue() const { return address; }
};
struct IORegistryPlane {};
static IORegistryPlane acpi;
struct OSIterator:OSObject {
    explicit OSIterator(const std::vector<OSObject *> &nodes):OSObject("OSIterator",true),nodes(nodes){}
    const std::vector<OSObject *> &nodes;
    size_t next {};
    OSObject *getNextObject() { return next<nodes.size() ? nodes[next++] : nullptr; }
};
struct IORegistryEntry:OSObject {
    explicit IORegistryEntry(const char *name):OSObject(name){}
    static bool planeAvailable;
    static const IORegistryPlane *getPlane(const char *key) {
        assert(!strcmp(key,"IOACPIPlane"));return planeAvailable ? &acpi : nullptr;
    }
    bool onPlane=true,allowWrite=true,allowIterator=true;
    IORegistryEntry *provider {};
    OSObject *address {},*association {};
    unsigned iterations {},writes {};
    std::vector<OSObject *> children;
    IORegistryEntry *getProvider() const { return provider; }
    bool inPlane(const IORegistryPlane *plane) const { return plane==&acpi && onPlane; }
    OSIterator *getChildIterator(const IORegistryPlane *plane) {
        assert(inPlane(plane));++iterations;return allowIterator ? new OSIterator(children) : nullptr;
    }
    OSObject *getProperty(const char *key) const {
        if(!strcmp(key,"_ADR"))return address;
        assert(!strcmp(key,"acpi-device"));return association;
    }
    OSObject *copyProperty(const char *key) const {
        auto value=getProperty(key);
        if(value)value->retain();
        return value;
    }
    bool setProperty(const char *key,OSObject *value) {
        assert(!strcmp(key,"acpi-device") && !association && value);
        ++writes;
        if(!allowWrite)return false;
        value->retain();association=value;
        return true;
    }
};
bool IORegistryEntry::planeAvailable=true;
struct IOPCIDevice:IORegistryEntry {
    IOPCIDevice():IORegistryEntry("IOPCIDevice"){}
    unsigned device=2,function=0;
    unsigned getDeviceNumber() const { return device; }
    unsigned getFunctionNumber() const { return function; }
};

// ASSOCIATION

struct Root {
    unsigned publications {};
    bool ready {};
    void setProperty(const char *key,bool value) {
        assert(!strcmp(key,"WEXFramebufferACPIDeviceReady"));
        ready=value;++publications;
    }
};
static const char *version="Darwin Kernel Version 27.2.0: fixture/RELEASE_X86_64";
static bool startFramebuffer(IOPCIDevice *pci,Root *root) {
    const auto trace=[](const char *value) { assert(!strcmp(value,"PCI to ACPI association")); };
    // START_GATE
    auto acpiDevice=pci->getProperty("acpi-device");
    return acpiDevice && acpiDevice->metaCast("IOACPIPlatformDevice");
}

int main() {
    IORegistryEntry bridge("A2XACPIPCI"),pc00("IOACPIPlatformDevice");
    bridge.provider=&pc00;
    IORegistryEntry gpu("IOACPIPlatformDevice"),other("IOACPIPlatformDevice");
    OSNumber gpuAddress(0x20000),otherAddress(0x20001),wideAddress(0x100020000);
    gpu.address=&gpuAddress;other.address=&otherAddress;
    IORegistryEntry unrelated("IOService"),wide("IOACPIPlatformDevice");
    unrelated.address=&gpuAddress;wide.address=&wideAddress;
    pc00.children={&other,&unrelated,&wide,&gpu};
    IOPCIDevice pci;pci.provider=&bridge;Root root;
    assert(startFramebuffer(&pci,&root) && root.ready && root.publications==1);
    assert(pci.association==&gpu && gpu.retained==2 && gpuAddress.retained==1);
    assert(pci.writes==1 && pc00.iterations==1);
    assert(startFramebuffer(&pci,&root) && root.ready);
    assert(pci.writes==1 && pc00.iterations==1 && gpu.retained==2);
    IOPCIDevice nonzeroFunction;nonzeroFunction.provider=&bridge;nonzeroFunction.function=1;
    assert(startFramebuffer(&nonzeroFunction,&root) && nonzeroFunction.association==&other);

    for(unsigned scenario=0;scenario<8;++scenario) {
        IOPCIDevice invalid;invalid.provider=&bridge;
        Root rejected;
        if(scenario==0)invalid.association=&gpuAddress;
        if(scenario==1)pc00.children.push_back(&gpu);
        if(scenario==2)invalid.device=3;
        if(scenario==3)invalid.provider=nullptr;
        if(scenario==4)pc00.onPlane=false;
        if(scenario==5)IORegistryEntry::planeAvailable=false;
        if(scenario==6)pc00.allowIterator=false;
        if(scenario==7)invalid.allowWrite=false;
        assert(!startFramebuffer(&invalid,&rejected) && !rejected.ready && rejected.publications==1);
        assert(invalid.association==(scenario==0 ? &gpuAddress : nullptr));
        assert(invalid.writes==(scenario==7 ? 1U : 0U));
        assert(gpu.retained==2 && gpuAddress.retained==1);
        if(scenario==1)pc00.children.pop_back();
        pc00.onPlane=true;IORegistryEntry::planeAvailable=true;pc00.allowIterator=true;
    }
    version="Darwin Kernel Version 25.6.0: fixture/RELEASE_X86_64";
    IOPCIDevice tahoe;tahoe.association=&gpu;
    Root legacy;
    assert(startFramebuffer(&tahoe,&legacy) && !legacy.publications && !tahoe.writes);
    puts("Native framebuffer sees the correctly typed ACPI peer, with unique bridge-local _ADR");
}
