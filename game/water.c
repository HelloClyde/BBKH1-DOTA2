#include "water.h"
#include "water_world_data.h"
static unsigned boundary(unsigned *position){
 unsigned value=0,shift=0,part;
 do{part=water_code[(*position)++];value|=(part&127u)<<shift;shift+=7;}while(part&128u);
 return value;
}
int dota_water_world(int x,int y){
 unsigned gx,gy,position,rows,i,j,count=0,ends[32];
 if((unsigned)x>4095u||(unsigned)y>4095u)return 0;
 gx=((unsigned)x*(DOTA_WATER_GRID_SIZE-1)+2047u)/4095u;
 gy=((unsigned)y*(DOTA_WATER_GRID_SIZE-1)+2047u)/4095u;
 position=water_block[gy/DOTA_WATER_BLOCK_ROWS];rows=gy%DOTA_WATER_BLOCK_ROWS+1;
 for(i=0;i<rows;i++){
  unsigned tag=water_code[position++];
  if(!tag)continue;
  count=tag&63u;
  if(tag&128u){unsigned edge=0;for(j=0;j<count;j++){edge+=boundary(&position);ends[j]=edge;}}
  else for(j=0;j<count;j++){unsigned delta=boundary(&position);int change=(int)(delta>>1);if(delta&1u)change=-change-1;ends[j]=(unsigned)((int)ends[j]+change);}
 }
 for(i=0;i<count;i+=2){if(gx<ends[i])return 0;if(gx<ends[i+1])return 1;}
 return 0;
}
