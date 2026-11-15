#include "../../bottle/WifiDiscovery.h"
#include <cassert>
#include <iostream>

void reset() { WiFi = FakeWifi(); NativeArduino::now() = 0; NativeArduino::delays() = 0; }
int main() {
  reset(); WifiDiscovery scan;
  assert(scan.start(41,true,7)); assert(scan.busy());
  assert(!scan.start(42,true,8)); assert(WiFi.scans == 1);
  scan.tick(); assert(scan.busy() && !scan.takeFinished());
  WiFi.found = {{"Office with spaces",-80,1},{"Free Wi-Fi",-60,0},{"Office with spaces",-30,1},{"",-10,1}};
  WiFi.result = 4; scan.tick();
  assert(!scan.busy() && scan.takeFinished() && !scan.takeFinished());
  assert(scan.id()==41 && scan.fromBle() && scan.session()==7);
  assert(scan.count()==2 && scan.hiddenCount()==1);
  assert(scan.network(0).ssid=="Office with spaces" && scan.network(0).rssi==-30 && scan.network(0).secured);
  assert(!scan.network(1).secured && WiFi.modeValue==WIFI_OFF);
  assert(!WiFi.persistentValue && !WiFi.autoReconnect && NativeArduino::delays()==0);

  reset(); WifiDiscovery capped;
  for(int i=0;i<24;++i) WiFi.found.push_back({String(std::to_string(i)),-100+i,1});
  capped.start(1,false,0); WiFi.result=24; capped.tick();
  assert(capped.count()==15 && capped.truncated());
  assert(capped.network(0).rssi==-77 && capped.network(14).rssi==-91);

  reset(); WifiDiscovery empty;
  empty.start(1,false,0); WiFi.result=0; empty.tick();
  assert(empty.count()==0 && !empty.error()[0] && empty.takeFinished());

  reset(); WifiDiscovery timeout;
  timeout.start(2,false,0); NativeArduino::now()=14999; timeout.tick(); assert(timeout.busy());
  NativeArduino::now()=15000; timeout.tick();
  assert(!timeout.busy() && timeout.error()[0] && WiFi.stops==1 && WiFi.modeValue==WIFI_OFF);

  reset(); WifiDiscovery failed; WiFi.beginResult=WIFI_SCAN_FAILED;
  assert(failed.start(3,false,0)); assert(failed.takeFinished() && failed.error()[0] && !failed.busy());

  reset(); NativeArduino::now()=0xfffffff0u; WifiDiscovery wrap;
  wrap.start(4,false,0); NativeArduino::now()+=15000; wrap.tick();
  assert(!wrap.busy() && wrap.error()[0]);
  std::cout << "6 Wi-Fi scan test groups passed\n";
}
