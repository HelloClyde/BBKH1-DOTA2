/* Sparse offline natural-tree layers. The ordinary uncut frame takes no tile
 * I/O, no layer traversal and retains the original map pixels byte for byte. */
#include "tree_visual.h"
#include "cache_memory.h"
#include "map_codec.h"
#include "map_sidecar_format.h"
#include "trees.h"
#include <stddef.h>
#ifdef DOTA_RESOURCE_PACK
#include "resource_pack.h"
#endif
#ifdef BAKED_IO_HOST
#include <stdio.h>
typedef FILE TreeFile;
static TreeFile *topen(const char *p){return fopen(p,"rb");}
static int tclose(TreeFile *p){return fclose(p);}
static int tsize(TreeFile *p){long n;if(fseek(p,0,SEEK_END))return -1;n=ftell(p);return n<0||n>0x7fffffffL?-1:(int)n;}
static int tseek(TreeFile *p,unsigned n){return fseek(p,(long)n,SEEK_SET)==0;}
static unsigned tread(TreeFile *p,void *d,unsigned n){return (unsigned)fread(d,1,n,p);}
#else
#include "h1_filesystem.h"
typedef h1_file TreeFile;
static TreeFile *topen(const char *p){return h1_fopen(p,"rb");}
static int tclose(TreeFile *p){return h1_fclose(p);}
static int tsize(TreeFile *p){return h1_fseek(p,0,2);}
static int tseek(TreeFile *p,unsigned n){return h1_fseek(p,(int)n,0)==(int)n;}
static unsigned tread(TreeFile *p,void *d,unsigned n){return h1_fread(d,1,n,p);}
#endif
#define TV_MAGIC 0x31525448u
#define TV_TILE_MAGIC 0x31545048u
#define TV_PIXELS 16384u
#define TV_COLS 30u
#define TV_ROWS 22u
#define TV_TILES (TV_COLS*TV_ROWS)
#define TV_TABLES (8452u+16904u+TV_TILES*24u)
#define TV_DATA (128u+TV_TABLES)
#define TV_SLOTS 15u
#define TV_MASK_WORDS (TV_PIXELS/32u)
#define TV_PATCH_BYTES (TV_PIXELS*4u+TV_MASK_WORDS*4u)
enum {TV_OK,TV_MISSING,TV_HEADER,TV_IO,TV_MEMORY,TV_CRC,TV_FORMAT,TV_GEOMETRY};
typedef struct {uint32_t tile,revision,age;uint16_t *color,*depth;uint32_t *mask;} TreePatch;
static TreeFile *tree_file;
#ifdef DOTA_RESOURCE_PACK
static ResourceFile *tree_resource;
#endif
static unsigned char *tables,*scratch;
static uint16_t *patch_memory;
static TreePatch patches[TV_SLOTS];
static uint32_t affected[(TV_TILES+31u)/32u];
static uint32_t file_bytes,max_decoded,max_stored,scratch_extra,clock_stamp,resource_epoch,serial;
static const DotaGame *state_owner;
static unsigned state_revision,state_epoch,state_count,state_serial;
static TreeVisualStats tree_stats;
static void clear(void *p,unsigned n){unsigned char *d=p;while(n--)*d++=0;}
static uint16_t get16(const unsigned char *p){return (uint16_t)(p[0]|((unsigned)p[1]<<8));}
static void put16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static const unsigned char *directory(unsigned i){return tables+8452u+16904u+i*24u;}
static int fail(unsigned e){if(!tree_stats.error){tree_stats.error=e;++resource_epoch;}return 0;}
static int read_at(unsigned at,void *destination,unsigned n){
 unsigned char *p=destination;
 if(at>file_bytes||n>file_bytes-at)return fail(TV_FORMAT);
#ifdef DOTA_RESOURCE_PACK
 if(tree_resource){++tree_stats.read_requests;if(!resource_read_at(tree_resource,at,destination,n))return fail(TV_IO);tree_stats.read_bytes+=n;return 1;}
#endif
 if(!tree_file)return fail(TV_FORMAT);
 if(!tseek(tree_file,at))return fail(TV_IO);
 while(n){unsigned want=n>65536u?65536u:n,got;++tree_stats.read_requests;got=tread(tree_file,p,want);tree_stats.read_bytes+=got;
  if(got!=want)return fail(TV_IO);
  p+=got;n-=got;}
 return 1;
}
unsigned tree_visual_dynamic_bytes(void){return tree_stats.heap_bytes;}
int tree_visual_ready(void){return tree_stats.ready&&!tree_stats.error;}
int tree_visual_error(void){return (int)tree_stats.error;}
const TreeVisualStats *tree_visual_stats(void){return &tree_stats;}
void tree_visual_close(void){
 dota_trees_detach();if(tree_file){(void)tclose(tree_file);tree_file=0;++tree_stats.closes;}
#ifdef DOTA_RESOURCE_PACK
 if(tree_resource){(void)resource_close(tree_resource);tree_resource=0;++tree_stats.closes;}
#endif
 cache_memory_free(tables);cache_memory_free(scratch);cache_memory_free(patch_memory);
 tables=scratch=0;patch_memory=0;tree_stats.ready=0;tree_stats.heap_bytes=0;
 tree_stats.metadata_bytes=tree_stats.cache_bytes=tree_stats.scratch_bytes=0;tree_stats.released=1;
 clear(patches,sizeof(patches));clear(affected,sizeof(affected));state_owner=0;++resource_epoch;
}
int tree_visual_open(const char *path){
 static const unsigned char scene_sha[32]={0xc3,0x12,0x84,0x05,0x84,0x57,0x1c,0x09,0x58,0xb8,0x40,0x9d,0xa7,0x29,0xea,0x94,0x5e,0xcb,0xf5,0x57,0xa5,0x5a,0xb6,0x03,0x0f,0xf0,0xd2,0xd9,0xcb,0xef,0x78,0xdd};
 unsigned char header[128];unsigned i,actual;uint32_t h[32],saved_crc;
 tree_visual_close();++tree_stats.open_attempts;tree_stats.error=0;tree_stats.released=0;
 tree_file=path&&*path?topen(path):0;
#ifdef DOTA_RESOURCE_PACK
 if(!path||!*path){tree_resource=resource_open(RESOURCE_TREE);if(!tree_resource)return fail(TV_MISSING);actual=resource_size(tree_resource);}
 else
#endif
 {if(!tree_file)return fail(TV_MISSING);actual=(unsigned)tsize(tree_file);}
 file_bytes=actual;
 if(actual<128u||actual>0x7fffffffu||!read_at(0,header,128u))goto bad;
 for(i=0;i<32u;++i)h[i]=mapc_get32(header+4u*i);
 saved_crc=h[31];mapc_put32(header+124,0);
 if(mapc_crc32(header,128u)!=saved_crc){fail(TV_CRC);goto bad;}
 if(h[0]!=TV_MAGIC||(h[1]!=1u&&h[1]!=2u)||h[2]!=128u||h[3]!=actual||h[4]!=3840u||h[5]!=2816u||h[6]!=128u||h[7]!=TV_COLS||h[8]!=TV_ROWS||h[9]!=(uint32_t)-512||h[10]!=(uint32_t)-512||h[11]!=DOTA_STATIC_TREE_COUNT||h[12]!=128u||h[13]!=8580u||h[14]!=25484u||h[15]!=TV_DATA||h[16]!=24u||h[20]!=1u||h[21]>128u||h[30]){fail(TV_HEADER);goto bad;}
 for(i=0;i<32u;++i)if(header[88u+i]!=scene_sha[i]){fail(TV_HEADER);goto bad;}
 max_decoded=h[17];max_stored=h[18];
 if(!max_decoded||!max_stored||max_decoded>16u*1024u*1024u||max_stored>16u*1024u*1024u){fail(TV_HEADER);goto bad;}
 tables=cache_memory_alloc(TV_TABLES);if(!tables){fail(TV_MEMORY);goto bad;}
 tree_stats.metadata_bytes=TV_TABLES;tree_stats.heap_bytes=TV_TABLES;
 if(!read_at(128u,tables,TV_TABLES))goto bad;
 if(mapc_crc32(tables,TV_TABLES)!=h[19]){fail(TV_CRC);goto bad;}
 for(i=0;i<DOTA_STATIC_TREE_COUNT;++i){const unsigned char *xy=tables+i*4u,*b=tables+8452u+i*8u;
  if(get16(xy)>=4096u||get16(xy+2)>=4096u||get16(b)>get16(b+4)||get16(b+2)>get16(b+6)||get16(b+4)>3840u||get16(b+6)>2816u){fail(TV_FORMAT);goto bad;}}
 scratch_extra=0;
 for(i=0;i<TV_TILES;++i){const unsigned char *e=directory(i);unsigned at=mapc_get32(e),n=mapc_get32(e+4),raw=mapc_get32(e+8),codec=mapc_get32(e+16),extra=0;
  if(!n){if(at||raw){fail(TV_FORMAT);goto bad;}continue;}
  if(at<TV_DATA||at>actual||n>actual-at||n>max_stored||raw<16u+TV_PIXELS*4u||raw>max_decoded||(raw-16u-TV_PIXELS*4u)%(TV_PIXELS*7u)||codec>2u||(codec==0u&&n!=raw)||(codec==2u&&(h[1]!=2u||n<8u+TV_PIXELS*4u))){fail(TV_FORMAT);goto bad;}
  if(codec==1u)extra=n<32768u?32768u:n;
  if(codec==2u)extra=TV_PIXELS*5u;
  if(extra>scratch_extra)scratch_extra=extra;
 }
 if(!dota_trees_attach((const uint16_t *)(const void *)tables,DOTA_STATIC_TREE_COUNT)){fail(TV_GEOMETRY);goto bad;}
 tree_stats.ready=1;return 1;
bad:{unsigned e=tree_stats.error?tree_stats.error:TV_HEADER;tree_visual_close();tree_stats.error=e;return 0;}
}
int tree_visual_open_default(void){
#ifdef DOTA_RESOURCE_PACK
 return tree_visual_open(0);
#else
 return tree_visual_open("B:\\DOTA2\\TREE.H1T")||tree_visual_open("A:\\DOTA2\\TREE.H1T");
#endif
}
unsigned tree_visual_revision(const DotaGame *g){
 unsigned r=g?dota_trees_revision(g):0,c=g?g->trees.cut_count:0;
 if(state_owner!=g||state_revision!=r||state_epoch!=resource_epoch||state_count!=c){
  state_owner=g;state_revision=r;state_epoch=resource_epoch;state_count=c;state_serial=++serial;}
 return state_serial;
}
static void affected_tiles(const DotaGame *g){
 unsigned word;clear(affected,sizeof(affected));
 for(word=0;word<DOTA_STATIC_TREE_WORDS;++word){uint32_t bits=g->trees.cut[word];
  while(bits){unsigned bit=(unsigned)__builtin_ctz(bits),id=word*32u+bit,x,y,x1,y1;const unsigned char *b;
   bits&=bits-1u;if(id>=DOTA_STATIC_TREE_COUNT)continue;b=tables+8452u+id*8u;
   x=get16(b);y=get16(b+2);x1=get16(b+4);y1=get16(b+6);if(x>=x1||y>=y1)continue;
   for(y/=128u;y<=(y1-1u)/128u;++y)for(x=get16(b)/128u;x<=(x1-1u)/128u;++x){unsigned tile=y*TV_COLS+x;affected[tile>>5]|=1u<<(tile&31u);}
  }
 }
}
static int prepare(const DotaGame *g,int query){
 unsigned i,bytes;static unsigned affected_serial;
 if(!g||(!g->trees.cut_count&&!query)||!tree_visual_ready())return 0;
 if(!patch_memory){
  bytes=TV_SLOTS*TV_PATCH_BYTES;patch_memory=cache_memory_alloc(bytes);
  if(!patch_memory)return fail(TV_MEMORY);
  tree_stats.cache_bytes=bytes;tree_stats.heap_bytes+=bytes;
   scratch=cache_memory_alloc(max_decoded+scratch_extra);if(!scratch)return fail(TV_MEMORY);
   tree_stats.scratch_bytes=max_decoded+scratch_extra;tree_stats.heap_bytes+=tree_stats.scratch_bytes;
  for(i=0;i<TV_SLOTS;++i){unsigned char *base=(unsigned char *)patch_memory+i*TV_PATCH_BYTES;
   patches[i].color=(uint16_t *)(void *)base;patches[i].depth=patches[i].color+TV_PIXELS;
   patches[i].mask=(uint32_t *)(void *)(base+TV_PIXELS*4u);patches[i].tile=0xffffffffu;}
 }
 i=tree_visual_revision(g);if(affected_serial!=i){affected_tiles(g);affected_serial=i;}
 return 1;
}
static void inverse_plane(unsigned char *p,unsigned char *temporary){
 unsigned i,previous=0;
 for(i=0;i<TV_PIXELS;++i){unsigned z=p[i]|((unsigned)p[TV_PIXELS+i]<<8),d=((z>>1)^(0u-(z&1u)))&65535u;
  previous=(previous+d)&65535u;put16(temporary+i*2u,previous);}
 for(i=0;i<TV_PIXELS*2u;++i)p[i]=temporary[i];
}
static unsigned mix565(unsigned foreground,unsigned background,unsigned a){
 unsigned b=255u-a;
 return (((((foreground>>11)*a+(background>>11)*b+127u)/255u)&31u)<<11)|
        ((((((foreground>>5)&63u)*a+((background>>5)&63u)*b+127u)/255u)&63u)<<5)|
        (((foreground&31u)*a+(background&31u)*b+127u)/255u);
}
/* A v2 tile contains one cropped frame per owner, in stable source order.
 * Stream frames through a bounded 80 KiB buffer. The resource reader keeps its
 * 16 KiB decode blocks across these sequential reads, so no full sparse tile
 * or second canonical tile is resident. Sort individual pixels, not whole
 * sprites: crossing branches and equal-depth alpha samples must stay exact. */
static int load_sparse(unsigned offset,unsigned bytes,unsigned raw){
 unsigned char header[16],*temporary=scratch+max_decoded;
 unsigned frames,layers,at=offset,end=offset+bytes,j,k,previous=0;
 if(bytes<8u+TV_PIXELS*4u||!read_at(at,header,8u))return 0;
 frames=mapc_get32(header);layers=mapc_get32(header+4);at+=8u;
 if(!frames||frames>DOTA_STATIC_TREE_COUNT*2u+1u||!layers||layers>128u||raw!=16u+TV_PIXELS*4u+layers*TV_PIXELS*7u)return fail(TV_FORMAT);
 mapc_put32(scratch,TV_TILE_MAGIC);mapc_put32(scratch+4,layers);
 mapc_put32(scratch+8,TV_PIXELS);mapc_put32(scratch+12,0);
 if(!read_at(at,scratch+16u,TV_PIXELS*4u))return 0;
 at+=TV_PIXELS*4u;
 for(k=0;k<layers;++k){unsigned i;unsigned char *p=scratch+16u+TV_PIXELS*4u+k*TV_PIXELS*7u;
  clear(p,TV_PIXELS*7u);
  for(i=0;i<TV_PIXELS;++i){put16(p+i*2u,65535u);put16(p+TV_PIXELS*4u+i*2u,65535u);}
 }
 for(j=0;j<frames;++j){unsigned token,id,key,x,y,width,height,count,size,yy,xx;
  const unsigned char *colors,*depths,*alpha;
  if(at>end||end-at<16u||!read_at(at,header,16u))return fail(TV_FORMAT);
  at+=16u;token=get16(header);id=token&32767u;
  x=get16(header+2);y=get16(header+4);width=get16(header+6);height=get16(header+8);
  if(token==65535u||(id>=DOTA_STATIC_TREE_COUNT&&token!=32767u)||!width||!height||x>=128u||y>=128u||width>128u-x||height>128u-y||get16(header+10)||get16(header+12)||get16(header+14))return fail(TV_FORMAT);
  key=token==32767u?0x7fffffffu:id*2u+((token>>15)&1u);
  if(j&&key<=previous)return fail(TV_FORMAT);
  previous=key;count=width*height;size=count*5u;
  if(size>scratch_extra||at>end||size>end-at)return fail(TV_FORMAT);
  if(!read_at(at,temporary,size))return 0;
  at+=size;colors=temporary;depths=colors+count*2u;alpha=depths+count*2u;
  for(yy=0;yy<height;++yy)for(xx=0;xx<width;++xx){unsigned source=yy*width+xx,pixel=(y+yy)*128u+x+xx,a=alpha[source],z,insert,used;
   unsigned char *p;
   if(!a)continue;
   z=get16(depths+source*2u);if(z==65535u)return fail(TV_FORMAT);
   for(insert=0;insert<layers;++insert){p=scratch+16u+TV_PIXELS*4u+insert*TV_PIXELS*7u;
    if(!p[TV_PIXELS*6u+pixel]||get16(p+TV_PIXELS*4u+pixel*2u)>z)break;
   }
   if(insert>=layers)return fail(TV_FORMAT);
   for(used=insert;used<layers;++used){p=scratch+16u+TV_PIXELS*4u+used*TV_PIXELS*7u;if(!p[TV_PIXELS*6u+pixel])break;}
   if(used>=layers)return fail(TV_FORMAT);
   for(k=used;k>insert;--k){unsigned char *to=scratch+16u+TV_PIXELS*4u+k*TV_PIXELS*7u,*from=to-TV_PIXELS*7u;unsigned plane;
    for(plane=0;plane<3u;++plane)put16(to+plane*TV_PIXELS*2u+pixel*2u,get16(from+plane*TV_PIXELS*2u+pixel*2u));
    to[TV_PIXELS*6u+pixel]=from[TV_PIXELS*6u+pixel];
   }
   p=scratch+16u+TV_PIXELS*4u+insert*TV_PIXELS*7u;
   put16(p+pixel*2u,token);put16(p+TV_PIXELS*2u+pixel*2u,get16(colors+source*2u));put16(p+TV_PIXELS*4u+pixel*2u,z);p[TV_PIXELS*6u+pixel]=(unsigned char)a;
  }
 }
 if(at!=end)return fail(TV_FORMAT);
 return 1;
}
static int load_tile(unsigned tile){
 const unsigned char *e=directory(tile);unsigned i,k,n=mapc_get32(e+4),raw=mapc_get32(e+8),codec=mapc_get32(e+16),layers;unsigned char *encoded=scratch+max_decoded;
 if(!n)return 0;
  if(codec==2u){if(!load_sparse(mapc_get32(e),n,raw))return 0;}
  else if(codec==1u){
   if(!read_at(mapc_get32(e),encoded,n))return 0;
   if(map_decode(encoded,n,scratch,raw)!=raw){fail(TV_FORMAT);return 0;}
   layers=mapc_get32(scratch+4);
   if(mapc_get32(scratch)!=TV_TILE_MAGIC||mapc_get32(scratch+8)!=TV_PIXELS||mapc_get32(scratch+12)||layers>128u||raw!=16u+TV_PIXELS*4u+layers*TV_PIXELS*7u){fail(TV_FORMAT);return 0;}
   inverse_plane(scratch+16u,encoded);inverse_plane(scratch+16u+TV_PIXELS*2u,encoded);
   for(k=0;k<layers;++k)for(i=0;i<3u;++i)inverse_plane(scratch+16u+TV_PIXELS*4u+k*TV_PIXELS*7u+i*TV_PIXELS*2u,encoded);
  }else if(!read_at(mapc_get32(e),scratch,raw))return 0;
  layers=mapc_get32(scratch+4);
  if(mapc_get32(scratch)!=TV_TILE_MAGIC||mapc_get32(scratch+8)!=TV_PIXELS||mapc_get32(scratch+12)||layers>128u||raw!=16u+TV_PIXELS*4u+layers*TV_PIXELS*7u){fail(TV_FORMAT);return 0;}
  if(mapc_crc32(scratch,raw)!=mapc_get32(e+12)){fail(TV_CRC);return 0;}
  /* Validate every token before consulting state; a cut in an earlier layer
   * must never hide a malformed token later in the file. */
  for(k=0;k<layers;++k){const unsigned char *p=scratch+16u+TV_PIXELS*4u+k*TV_PIXELS*7u;
   for(i=0;i<TV_PIXELS;++i){unsigned token=get16(p+i*2u),id=token&32767u;
    if(token!=65535u&&id>=DOTA_STATIC_TREE_COUNT&&token!=32767u){fail(TV_FORMAT);return 0;}}}
 return (int)layers;
}
int tree_visual_pick(const DotaGame *g,int gx,int gy){
 int x=gx+512,y=gy+512,layers;unsigned at,k,base;
 if((unsigned)x>=3840u||(unsigned)y>=2816u||!prepare(g,1))return -1;
 layers=load_tile((unsigned)(y/128)*TV_COLS+(unsigned)(x/128));if(!layers)return -1;
 at=(unsigned)(y&127)*128u+(unsigned)(x&127);base=get16(scratch+16u+TV_PIXELS*2u+at*2u);
 for(k=0;k<(unsigned)layers;++k){const unsigned char *p=scratch+16u+TV_PIXELS*4u+k*TV_PIXELS*7u;unsigned token=get16(p+at*2u),id=token&32767u,z;
  if(token==65535u||!p[TV_PIXELS*6u+at])continue;
  z=get16(p+TV_PIXELS*4u+at*2u);if(z>base)continue;
  if(token==32767u)return -1;
  if(token&32768u){if(dota_trees_is_cut(g,id))return -1;continue;}
  if(!dota_trees_is_cut(g,id))return (int)id;
 }
 return -1;
}
static TreePatch *tile_patch(const DotaGame *g,unsigned tile){
 TreePatch *entry=&patches[0];unsigned i,k,layers,rev=tree_visual_revision(g);int loaded;
 for(i=0;i<TV_SLOTS;++i){TreePatch *p=&patches[i];
  if(p->tile==tile&&p->revision==rev){++tree_stats.tile_hits;p->age=++clock_stamp;return p;}
  if(p->age<entry->age)entry=p;
 }
 ++tree_stats.tile_misses;entry->tile=0xffffffffu;clear(entry->mask,TV_MASK_WORDS*4u);
 loaded=load_tile(tile);if(tree_visual_error())return 0;layers=(unsigned)loaded;
 if(layers){
  for(i=0;i<TV_PIXELS;++i){unsigned color=get16(scratch+16u+i*2u),depth=get16(scratch+16u+TV_PIXELS*2u+i*2u),front=65536u,stump=65536u;int changed=0;
   for(k=0;k<layers;++k){const unsigned char *p=scratch+16u+TV_PIXELS*4u+k*TV_PIXELS*7u;unsigned token=get16(p+i*2u),id=token&32767u,z;int cut;
    if(token==65535u||!p[TV_PIXELS*6u+i])continue;
    z=get16(p+TV_PIXELS*4u+i*2u);cut=id<DOTA_STATIC_TREE_COUNT&&dota_trees_is_cut(g,id);
    if(token&32768u){if(cut&&z<stump)stump=z;continue;}
    if(z<front){front=z;changed=cut;}}
   /* An unchanged foreground tree, wall or constant prop retains its exact
    * original panorama RGB. Only a removed foreground or new stump is patched. */
   if(stump<front&&stump<=depth)changed=1;
   if(!changed)continue;
   for(k=layers;k>0;--k){const unsigned char *p=scratch+16u+TV_PIXELS*4u+(k-1u)*TV_PIXELS*7u;unsigned token=get16(p+i*2u),id=token&32767u,a=p[TV_PIXELS*6u+i],z;int cut,active;
    if(token==65535u||!a)continue;
    cut=id<DOTA_STATIC_TREE_COUNT&&dota_trees_is_cut(g,id);active=id==32767u?1:((token&32768u)?cut:!cut);if(!active)continue;
    z=get16(p+TV_PIXELS*4u+i*2u);if(z>depth)continue;
    color=a==255u?get16(p+TV_PIXELS*2u+i*2u):mix565(get16(p+TV_PIXELS*2u+i*2u),color,a);if(z<depth)depth=z;
   }
   entry->color[i]=(uint16_t)color;entry->depth[i]=(uint16_t)depth;entry->mask[i>>5]|=1u<<(i&31u);
  }
  ++tree_stats.decoded_tiles;
 }
 entry->tile=tile;entry->revision=rev;entry->age=++clock_stamp;return entry;
}
void tree_visual_apply(uint16_t *pixels,uint16_t *depths,const DotaGame *g,int gx,int gy,int width,int height){
 int x,y;
 if(!pixels||!depths||width<=0||height<=0||!prepare(g,0))return;
 for(y=0;y<height;){int py=gy+y+512,rows=128-(py&127);if(rows>height-y)rows=height-y;
  for(x=0;x<width;){int px=gx+x+512,columns=128-(px&127);unsigned tile;TreePatch *p;
   if(columns>width-x)columns=width-x;
   if((unsigned)px<3840u&&(unsigned)py<2816u){tile=(unsigned)(py/128)*TV_COLS+(unsigned)(px/128);
    if((affected[tile>>5]>>(tile&31u))&1u){int yy,xx;p=tile_patch(g,tile);if(!p)return;
     for(yy=0;yy<rows;++yy)for(xx=0;xx<columns;++xx){unsigned at=(unsigned)((py+yy)&127)*128u+(unsigned)((px+xx)&127),to=(unsigned)(y+yy)*(unsigned)width+(unsigned)(x+xx);
      if((p->mask[at>>5]>>(at&31u))&1u){pixels[to]=p->color[at];depths[to]=p->depth[at];++tree_stats.patched_pixels;}}
    }
   }
   x+=columns;
  }
  y+=rows;
 }
}
uint16_t tree_visual_depth_at(const DotaGame *g,int gx,int gy,uint16_t original){
 int x=gx+512,y=gy+512;unsigned tile,at;TreePatch *p;
 if(!g||!g->trees.cut_count||!tree_visual_ready())return original;
 ++tree_stats.depth_queries;if((unsigned)x>=3840u||(unsigned)y>=2816u||!prepare(g,0))return original;
 tile=(unsigned)(y/128)*TV_COLS+(unsigned)(x/128);if(!((affected[tile>>5]>>(tile&31u))&1u))return original;
 p=tile_patch(g,tile);if(!p)return original;at=(unsigned)(y&127)*128u+(unsigned)(x&127);return ((p->mask[at>>5]>>(at&31u))&1u)?p->depth[at]:original;
}
