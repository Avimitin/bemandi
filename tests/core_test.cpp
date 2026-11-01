#include "core.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

static void require(bool value,const char* text) { if(!value) { fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
int main() {
    using namespace bemandi;
    Cadence c;
    require(c.due(1000,120000,120),"first frame");
    require(!c.due(1999,120000,120),"not early");
    require(c.due(2000,120000,120),"next deadline");
    require(c.due(200000,120000,120),"resume after a pause");
    require(!c.due(200001,120000,120),"no catch-up burst");
    c.reset(); require(c.due(1,120000,120),"reset cadence");
    require(to_100ns(20000000000000LL,10000000)==20000000000000LL,"long-uptime timestamp does not overflow");
    require(to_100ns(31,3)==103333333,"timestamp remainder");
    std::atomic<SlotState> slot{SlotState::free};
    std::atomic<int> claimed{0};
    std::thread a([&]{if(claim(slot)) ++claimed;});
    std::thread b([&]{if(claim(slot)) ++claimed;});
    a.join(); b.join();
    require(claimed==1,"only one producer can claim a slot");
    slot.store(SlotState::inflight);
    require(!claim(slot),"cannot overwrite NDI-owned storage");
    slot.store(SlotState::free);
    require(claim(slot),"completed slot reusable");
    puts("core tests passed");
}
