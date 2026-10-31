/* PC-only full-resolution textured rasterizer for offline sprite/map baking. */
#include <stdint.h>
#include <math.h>
#include <float.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static float srgb[256];
static int initialized;
static float clamp01(float x){return x<0?0:x>1?1:x;}
static float edge(float ax,float ay,float bx,float by,float x,float y){return (bx-ax)*(y-ay)-(by-ay)*(x-ax);}
static int wrap(int value,int extent){value%=extent;return value<0?value+extent:value;}
/* vertex = native-pixel x,y, depth, u,v, world normal x,y,z. colour is
 * linear premultiplied RGBA. Pixel samples are centred at (p+.5)/scale. */
API void bake_clear(int width,int height,float *colour,float *depth){
    int i;
    for(i=0;i<width*height;++i){colour[4*i]=colour[4*i+1]=colour[4*i+2]=colour[4*i+3]=0;depth[i]=FLT_MAX;}
    if(!initialized){for(i=0;i<256;++i){float x=i/255.f;srgb[i]=x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f);}initialized=1;}
}
API void bake_draw(int width,int height,int scale,float *colour,float *depth,
                   const float *vertices,const uint32_t *faces,int face_count,
                   const uint8_t *texture,int tw,int th,float cutoff,
                   const float *factor,int repeat,int unlit){
    int fi;
    for(fi=0;fi<face_count;++fi){
        const float *a=vertices+faces[fi*3]*8,*b=vertices+faces[fi*3+1]*8,*c=vertices+faces[fi*3+2]*8;
        float area=edge(a[0],a[1],b[0],b[1],c[0],c[1]),inv;
        int x0,x1,y0,y1,x,y;
        if(fabsf(area)<1e-10f)continue;
        x0=(int)floorf(fminf(a[0],fminf(b[0],c[0]))*scale);x1=(int)ceilf(fmaxf(a[0],fmaxf(b[0],c[0]))*scale);
        y0=(int)floorf(fminf(a[1],fminf(b[1],c[1]))*scale);y1=(int)ceilf(fmaxf(a[1],fmaxf(b[1],c[1]))*scale);
        if(x1<0||y1<0||x0>=width||y0>=height)continue;
        if(x0<0)x0=0;if(y0<0)y0=0;if(x1>=width)x1=width-1;if(y1>=height)y1=height-1;
        inv=1/area;
        for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x){
            float px=(x+.5f)/scale,py=(y+.5f)/scale;
            float wa=edge(b[0],b[1],c[0],c[1],px,py)*inv,wb=edge(c[0],c[1],a[0],a[1],px,py)*inv,wc=1-wa-wb;
            float d,u,v,fu,fv,rgba[4]={0,0,0,0},n[3],length,light;int xx,yy,k,s,t,index=y*width+x;
            if(wa<-.000001f||wb<-.000001f||wc<-.000001f)continue;
            d=a[2]*wa+b[2]*wb+c[2]*wc;if(d>=depth[index])continue;
            u=(a[3]*wa+b[3]*wb+c[3]*wc)*tw-.5f;v=(a[4]*wa+b[4]*wb+c[4]*wc)*th-.5f;
            xx=(int)floorf(u);yy=(int)floorf(v);fu=u-xx;fv=v-yy;
            for(t=0;t<2;++t)for(s=0;s<2;++s){
                int tx=xx+s,ty=yy+t;float w=(s?fu:1-fu)*(t?fv:1-fv);const uint8_t *tex;
                if(repeat){tx=wrap(tx,tw);ty=wrap(ty,th);}else{if(tx<0)tx=0;if(ty<0)ty=0;if(tx>=tw)tx=tw-1;if(ty>=th)ty=th-1;}
                tex=texture+(ty*tw+tx)*4;
                for(k=0;k<3;++k)rgba[k]+=srgb[tex[k]]*w;
                rgba[3]+=tex[3]/255.f*w;
            }
            rgba[3]*=factor[3];if(rgba[3]<cutoff||rgba[3]<=.001f)continue;
            /* Alpha-tested surfaces cover the supersample, then resolve to
             * antialiased coverage. BLEND is used only by separate FX baking. */
            if(cutoff>0)rgba[3]=1;
            for(k=0;k<3;++k)n[k]=a[5+k]*wa+b[5+k]*wb+c[5+k]*wc;
            length=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(length>1e-10f)for(k=0;k<3;++k)n[k]/=length;
            /* Soft hemisphere + fixed broad daylight. No low-poly normals,
             * source textures stay in linear light through resolve. */
            light=unlit?1.f:(.56f+.12f*clamp01(n[2]*.5f+.5f)+.45f*fmaxf(0,-.45f*n[0]-.35f*n[1]+.82f*n[2]));
            for(k=0;k<3;++k)colour[index*4+k]=rgba[k]*factor[k]*light*rgba[3];
            colour[index*4+3]=rgba[3];depth[index]=d;
        }
    }
}
