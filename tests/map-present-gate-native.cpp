#include "usp_map_present_gate.h"
#include <cassert>
#include <cstdint>
#include <iostream>
struct Legacy {
  bool pending{},drawing{};
  void arm(){pending=true;drawing=false;}
  bool consume(bool active,int code){
    if(!pending||!active)return false;
    if(code==1)drawing=true;
    if(code!=2||!drawing)return false;
    pending=false;return true;
  }
};
static void walk(unsigned depth,unsigned max,Legacy a,usp_map_present_gate::Gate b,
                 uint64_t &states,uint64_t &presents){
  if(depth==max)return;
  for(unsigned action=0;action<7;++action){
    auto x=a;auto y=b;bool rx=false,ry=false;
    if(action==6){x.arm();y.arm();}
    else {const bool active=action>=3;const int code=int(action%3);
      rx=x.consume(active,code);ry=y.consume(active,code==1,code==2);}
    assert(rx==ry);assert(x.pending==y.pending());assert(x.drawing==y.drawing());
    ++states;if(rx)++presents;walk(depth+1,max,x,y,states,presents);
  }
}
int main(){uint64_t states=0,presents=0;Legacy a;usp_map_present_gate::Gate b;
  walk(0,7,a,b,states,presents);assert(states==960799&&presents==18201);
  std::cout<<"MAP_PRESENT_GATE_NATIVE=PASS states="<<states<<" presents="<<presents<<"\n";}
