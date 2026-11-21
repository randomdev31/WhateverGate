#include "AppleIntelTGLCompat/AppleIntelTGLCompat.cpp"
#include <algorithm>
#include <cassert>
#include <cstring>

#if defined(__x86_64__) && defined(__APPLE__)
using mach_vm_address_t=uintptr_t;
static uint8_t donorEntry[16];

struct KernelPatcher {
    struct RouteRequest {
        const char *symbol;
        uintptr_t from {},to;
        uintptr_t *org;
        RouteRequest(const char *symbol,uintptr_t to,uintptr_t &org)
            :symbol(symbol),to(to),org(&org){}
    };
    unsigned attempts {};
    uintptr_t routedTo {};
    bool falseSuccess {},missingSymbol {},missingTrampoline {};

    template<class T> T solveSymbol(size_t,const char *,uintptr_t address,size_t) {
        return missingSymbol ? 0 : address;
    }
    void clearError(){}
    bool routeMultipleShort(size_t,RouteRequest *request,size_t count,uintptr_t address,size_t) {
        ++attempts;
        assert(count==1 && address==reinterpret_cast<uintptr_t>(donorEntry));
        if(falseSuccess)return true; // Lilu treats nonzero errno as a trampoline.
        const auto distance=int64_t(request->to)-int64_t(address+5);
        assert(distance>=INT32_MIN && distance<=INT32_MAX);
        const auto encoded=static_cast<int32_t>(distance);
        donorEntry[0]=0xE9;
        memcpy(donorEntry+1,&encoded,sizeof(encoded));
        if(request->org && !missingTrampoline)*request->org=0xffffff8000400000ULL;
        routedTo=request->to;
        return true;
    }
};

static struct {
    size_t (*nofault)(uintptr_t,uintptr_t,size_t) {};
} tglVM;

struct XE {
    uintptr_t branchRelayImage {},branchRelayReserve {};
    size_t branchRelayImageSize {};
    uint32_t branchRelaysUsed {},branchRangeFailures {},branchRouteFailures {};
    uintptr_t intelBranchTarget(uintptr_t,uintptr_t,int32_t &);
    bool routeIntelShort(KernelPatcher &,size_t,KernelPatcher::RouteRequest *,size_t,uintptr_t,size_t);
};

// PRODUCTION_ROUTING

static uint64_t receive(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    return a+2*b+3*c+4*d+5*e+6*f;
}

int main() {
    const auto donor=reinterpret_cast<uintptr_t>(donorEntry);
    const auto stubs=reinterpret_cast<uintptr_t>(wexBranchRelays);
    const auto slots=reinterpret_cast<uintptr_t>(branchRelayTargets);
    const auto handler=reinterpret_cast<uintptr_t>(receive);
    XE x;
    x.branchRelayImage=std::min(stubs,slots);
    x.branchRelayImageSize=std::max(stubs,slots)+sizeof(branchRelayTargets)-x.branchRelayImage;
    x.branchRelayReserve=reinterpret_cast<uintptr_t>(xelpReserveBranchRelay);
    tglVM.nofault=[](uintptr_t from,uintptr_t to,size_t length) {
        memcpy(reinterpret_cast<void *>(to),reinterpret_cast<const void *>(from),length);
        return length;
    };

    int32_t relative=0;
    const auto next=donor+5;
    assert(XELPCall::relativeDisplacement(next,next+INT32_MAX,relative) && relative==INT32_MAX);
    assert(XELPCall::relativeDisplacement(next,next-0x80000000ULL,relative) && relative==INT32_MIN);
    assert(!XELPCall::relativeDisplacement(next,next+0x80000000ULL,relative));
    assert(!XELPCall::relativeDisplacement(next,next-0x80000001ULL,relative));
    assert(!xelpReserveBranchRelay(XELPCall::BranchRelayABI+1,handler));
    const auto local=xelpReserveBranchRelay(XELPCall::BranchRelayABI,handler);
    assert(local==stubs);
    assert(reinterpret_cast<decltype(&receive)>(local)(1,2,3,4,5,6)==91);

    constexpr uintptr_t farOffset=0x90000000;
    for(uintptr_t target:{next+64,next+farOffset,next-farOffset}) {
        uintptr_t original=0;
        KernelPatcher p;
        KernelPatcher::RouteRequest route("donor",target,original);
        donorEntry[0]=0x55;
        uint8_t call[5] {0xE8};
        const auto dest=x.intelBranchTarget(next,target,relative);
        assert(dest);
        memcpy(call+1,&relative,4);
        assert(XELPCall::targets(call,sizeof(call),donor,dest));
        assert(x.routeIntelShort(p,1,&route,1,donor,sizeof(donorEntry)));
        assert(p.attempts==1 && original && p.routedTo==dest);
        assert(XELPCall::relativeTarget(donorEntry+1,next)==dest);
        if(target!=next+64) {
            assert(p.routedTo>=stubs && p.routedTo<stubs+sizeof(branchRelayTargets));
            assert(branchRelayTargets[(p.routedTo-stubs)/8]==target);
        } else assert(p.routedTo==target);
    }

    constexpr uint8_t entry=0x55;
    const auto far=next+farOffset;
    for(unsigned failure=0;failure<4;++failure) {
        XE candidate=x;
        KernelPatcher patcher;
        uintptr_t original=0;
        KernelPatcher::RouteRequest route("donor",far,original);
        donorEntry[0]=entry;
        if(failure==0)candidate.branchRelayReserve=0;
        if(failure==1)patcher.falseSuccess=true;
        if(failure==2)patcher.missingTrampoline=true;
        if(failure==3)patcher.missingSymbol=true;
        assert(!candidate.routeIntelShort(patcher,1,&route,1,donor,sizeof(donorEntry)));
        assert(patcher.attempts==(failure==1 || failure==2 ? 1U : 0U));
        if(failure!=2)assert(donorEntry[0]==entry);
    }
    // Fill the actual assembly pool while verifying each dispatch target,
    // then prove exhaustion and repeated lookup leave existing slots valid.
    for(size_t i=3;i<branchRelayCount;++i) {
        const auto target=far+i;
        const auto stub=xelpReserveBranchRelay(XELPCall::BranchRelayABI,target);
        assert(stub==stubs+8*i);
        assert(XELPCall::importedTarget(stub,target,[&](uintptr_t from,void *out,size_t size) {
            return tglVM.nofault(from,reinterpret_cast<uintptr_t>(out),size)==size;
        })==target);
    }
    assert(!xelpReserveBranchRelay(XELPCall::BranchRelayABI,far+branchRelayCount));
    assert(xelpReserveBranchRelay(XELPCall::BranchRelayABI,handler)==local);
    assert(reinterpret_cast<decltype(&receive)>(local)(1,2,3,4,5,6)==91);
    puts("Golden Gate direct/relayed branches, live x86-64 dispatch and rejection of false route success");
}
#else
int main() { return 77; }
#endif
