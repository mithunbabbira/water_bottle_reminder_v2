#include "../bottle/AlertPresence.h"
#include <cassert>
#include <iostream>
int main() {
  AlertPresence p;
  assert(!p.allowed(0) && !p.shouldProbe(0));
  p.update(1,true); assert(p.shouldProbe(1) && !p.allowed(1));
  auto token=p.epoch();p.started(1);
  p.complete(token,true,1000);assert(p.allowed(1000));
  assert(!p.shouldProbe(14999) && p.shouldProbe(15001));
  p.started(15001);p.complete(token,false,16000);assert(!p.allowed(16000));
  assert(!p.shouldProbe(16000));assert(p.shouldProbe(30001));
  p.started(30001);p.complete(token,true,31000);assert(p.allowed(31000));
  p.update(31001,false);assert(!p.allowed(31001));
  p.update(31002,true);assert(p.shouldProbe(31002));
  p.complete(token,true,31003);assert(!p.allowed(31003)); // stale episode
  token=p.epoch();p.started(31004);p.invalidate();
  p.complete(token,true,31005);assert(!p.allowed(31005)); // selection/clear invalidation
  token=p.epoch();p.started(32000);p.complete(token,true,33000);
  assert(p.allowed(62999));p.update(63000,true);assert(!p.allowed(63000));
  // A rejected driver start is a real negative attempt: revoke and back off.
  p.started(64000);p.complete(p.epoch(),true,64001);assert(p.allowed(64001));
  p.started(79000);p.complete(p.epoch(),false,79000);
  assert(!p.allowed(79000) && !p.shouldProbe(79001) && p.shouldProbe(94000));
  AlertPresence wrap;wrap.update(0xfffffff0,true);auto t=wrap.epoch();wrap.started(0xfffffff0);
  wrap.complete(t,true,0xfffffff1);assert(wrap.allowed(uint32_t(0xfffffff1u+29999u)));
  wrap.update(uint32_t(0xfffffff1u+30000u),true);assert(!wrap.allowed(uint32_t(0xfffffff1u+30000u)));
  std::cout << "Alert presence policy: boot, due/fresh, retry, failure, new episode, replace/clear, TTL and wrap passed\n";
}
