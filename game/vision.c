#include "vision.h"
#include "roshan_data.h"
#include "../private/generated/dota_roshan_vision.h"

int dota_roshan_region_at(int x,int y){
    int side;if((unsigned)x>=4096u || (unsigned)y>=4096u)return 0;
    x*=256;y*=256;
    for(side=0;side<2;++side){
        /* Reject outside the exact Source polygon bounds before its signed64
         * convex tests. Boundaries still use the original edge inequalities. */
        static const int bounds_q8[2][4]={
            {295427,357585,350461,408808},
            {670471,733162,667253,724179}
        };
        int i,first=dota_roshan_polygon_offsets[side],end=dota_roshan_polygon_offsets[side+1],inside=1;
        if(x<bounds_q8[side][0] || x>bounds_q8[side][1] ||
           y<bounds_q8[side][2] || y>bounds_q8[side][3])continue;
        for(i=first;i<end;++i){
            int next=i+1<end?i+1:first;
            long long dx=dota_roshan_polygon_q8[next][0]-dota_roshan_polygon_q8[i][0];
            long long dy=dota_roshan_polygon_q8[next][1]-dota_roshan_polygon_q8[i][1];
            long long px=x-dota_roshan_polygon_q8[i][0],py=y-dota_roshan_polygon_q8[i][1];
            if(dx*py-dy*px<0){inside=0;break;}
        }
        if(inside)return side+1;
    }
    return 0;
}
int dota_pit_same_region(int x1,int y1,int x2,int y2){
    return dota_roshan_region_at(x1,y1)==dota_roshan_region_at(x2,y2);
}

int dota_roshan_region_pixel(int global_px,int global_py){
    int side;if((unsigned)global_px>=32768u)return 0;
    for(side=0;side<2;++side){
        const DotaPitPixelRows *r=&dota_pit_pixel_rows[side];unsigned a,b,row;
        if(global_py<r->y || global_py>=r->y+r->rows)continue;
        row=(unsigned)(global_py-r->y);
        a=dota_pit_pixel_row_offsets[r->row_base+row];b=dota_pit_pixel_row_offsets[r->row_base+row+1];
        for(;a<b;++a){const unsigned short *span=dota_pit_pixel_runs[r->run_base+a];
            if(global_px<span[0])break;
            if(global_px<span[1])return side+1;
        }
    }
    return 0;
}

static void mask_run(uint16_t *row,int first,int end,uint16_t hidden){
    int x;for(x=first;x<end;++x)row[x]=hidden;
}
void dota_roshan_mask_pixels(uint16_t *pixels,int stride,int width,int height,
                            int global_x,int global_y,int region,uint16_t hidden){
    int side,y;
    if(!pixels || width<=0 || height<=0 || stride<width)return;
    if(region==0){
        /* Outside both pits only their actual visible spans need hiding.
         * Most viewports do not intersect a pit row at all. */
        for(side=0;side<2;++side){
            const DotaPitPixelRows *r=&dota_pit_pixel_rows[side];
            int first=r->y-global_y,end=first+r->rows;
            if(first<0)first=0;
            if(end>height)end=height;
            for(y=first;y<end;++y){
                unsigned row=(unsigned)(global_y+y-r->y),a=dota_pit_pixel_row_offsets[r->row_base+row],b=dota_pit_pixel_row_offsets[r->row_base+row+1];
                for(;a<b;++a){const unsigned short *span=dota_pit_pixel_runs[r->run_base+a];
                    int left=(int)span[0]-global_x,right=(int)span[1]-global_x;
                    if(left<0)left=0;
                    if(right>width)right=width;
                    mask_run(pixels+y*stride,left,right,hidden);
                }
            }
        }
        return;
    }
    /* Inside a pit, retain every original run including holes and disjoint
     * runs. The generated two pit row ranges do not overlap. */
    for(y=0;y<height;++y){
        int next=0;uint16_t *dst=pixels+y*stride;
        if(region==1 || region==2){
            const DotaPitPixelRows *r=&dota_pit_pixel_rows[region-1];
            int py=global_y+y;
            if(py>=r->y && py<r->y+r->rows){
                unsigned row=(unsigned)(py-r->y),a=dota_pit_pixel_row_offsets[r->row_base+row],b=dota_pit_pixel_row_offsets[r->row_base+row+1];
                for(;a<b;++a){const unsigned short *span=dota_pit_pixel_runs[r->run_base+a];
                    int left=(int)span[0]-global_x,right=(int)span[1]-global_x;
                    if(left<0)left=0;
                    if(right>width)right=width;
                    if(right<=next || left>=width)continue;
                    mask_run(dst,next,left,hidden);next=right;
                }
            }
        }
        mask_run(dst,next,width,hidden);
    }
}
