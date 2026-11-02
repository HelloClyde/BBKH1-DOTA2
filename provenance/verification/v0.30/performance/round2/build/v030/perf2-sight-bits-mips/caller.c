#include "sight.h"
unsigned test_all_current_bits(const DotaGame*g,int team){
 const uint32_t*p=dota_sight_current_bits(g,team);unsigned c;
 if(!p)return 0;
 for(c=0;c<65536u;++c){unsigned present=(p[c>>5]>>(c&31u))&1u;
  if(present!=(unsigned)(dota_sight_fog_state(g,team,(int)(c%256u)*16+8,(int)(c/256u)*16+8)==2))return 0x80000000u|c;
 }
 return 65536u;
}
