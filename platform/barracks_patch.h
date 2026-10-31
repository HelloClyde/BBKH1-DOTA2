/* Pure bounded decoder for trusted compiled same-source sparse map patches. */
#ifndef H1_DOTA_BARRACKS_PATCH_DECODER_H
#define H1_DOTA_BARRACKS_PATCH_DECODER_H
typedef struct {const unsigned char *at,*end;unsigned left,zero,used,limit;} BarracksPatchReader;
static int barracks_patch_byte(BarracksPatchReader *r,unsigned *value){
    if(r->used>=r->limit)return 0;
    if(!r->left){
        unsigned control;if(r->at>=r->end)return 0;
        control=*r->at++;r->left=(control&127u)+1u;r->zero=control&128u;
        if(r->left>r->limit-r->used || (!r->zero && r->left>(unsigned)(r->end-r->at)))return 0;
    }
    *value=r->zero?0u:*r->at++;--r->left;++r->used;return 1;
}
static int barracks_patch_word(BarracksPatchReader *r,unsigned *value){
    unsigned a,b;if(!barracks_patch_byte(r,&a)||!barracks_patch_byte(r,&b))return 0;
    *value=a|(b<<8);return 1;
}
static int barracks_patch_pair(unsigned short *pixels,unsigned short *zbuf,
    unsigned width,unsigned height,unsigned stride,unsigned top,int ox,int oy,unsigned pair,unsigned state){
    const DotaBarracksPatch *p;BarracksPatchReader r;unsigned row,drawn=0;
    unsigned short colours[256];
    if(pair>=6u || state>3u || !width || !height || stride<width || !pixels || !zbuf)return -1;
    if(state==3u)return 0; /* No writes, decoding or cache allocation while alive. */
    p=&dota_barracks_patches[pair];
    if(p->x+p->w<=ox || p->x>=ox+(int)width || p->y+p->h<=oy || p->y>=oy+(int)height)return 0;
    if(p->w<=0 || p->w>256 || p->h<=0 || p->offset>sizeof(dota_barracks_patch_bytes) ||
       p->bytes>sizeof(dota_barracks_patch_bytes)-p->offset)return -1;
    r.at=dota_barracks_patch_bytes+p->offset;r.end=r.at+p->bytes;r.left=r.zero=r.used=0;r.limit=p->raw_bytes;
    for(row=0;row<p->runs;++row){
        unsigned start,count,condition,i,previous=0;int x,y,active;
        if(!barracks_patch_word(&r,&start)||!barracks_patch_word(&r,&count)||!barracks_patch_word(&r,&condition))return -1;
        if(start>=(unsigned)p->w*p->h || !count || count>256u || count>(unsigned)p->w-start%(unsigned)p->w || !condition || condition>7u)return -1;
        x=p->x+(int)(start%(unsigned)p->w)-ox;y=p->y+(int)(start/(unsigned)p->w)-oy;
        active=(condition&(1u<<state)) && y>=0 && y<(int)height;
        for(i=0;i<count;++i){unsigned value;if(!barracks_patch_word(&r,&value))return -1;colours[i]=(unsigned short)value;}
        for(i=0;i<count;++i){
            unsigned delta;int sx=x+(int)i;if(!barracks_patch_word(&r,&delta))return -1;
            previous=(previous+delta)&65535u;
            if(active && sx>=0 && sx<(int)width){
                pixels[(y+(int)top)*(int)stride+sx]=colours[i];zbuf[y*(int)width+sx]=(unsigned short)previous;++drawn;
            }
        }
    }
    if(r.at!=r.end || r.left || r.used!=r.limit)return -1;
    return (int)drawn;
}
#endif
