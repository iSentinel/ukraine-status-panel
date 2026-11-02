#include "alarm_audio_policy.h"
#include <cassert>
#include <climits>
#include <iostream>
using namespace usp_alarm_audio;
int main(){
 State s{}; assert(!due(s,0));
 armed(&s,true,true,100); assert(due(s,100)); played(&s,100); assert(!due(s,6099)); assert(due(s,6100));
 armed(&s,false,true,6200); assert(!due(s,999999));
 armed(&s,true,false,7000); assert(!due(s,7000));
 armed(&s,true,true,8000); assert(due(s,8000)); played(&s,UINT32_MAX-2000); assert(!due(s,1000)); assert(due(s,3999)); armed(&s,false,true,4000);
 for(unsigned i=0;i<10000;i++){armed(&s,true,true,i*7000u);assert(due(s,i*7000u));played(&s,i*7000u);armed(&s,false,true,i*7000u+1);assert(!due(s,i*7000u+7000));}
 std::cout<<"ALARM_AUDIO_POLICY_NATIVE=PASS repeat6000 dismiss disable wrap stress10000\n";
}
