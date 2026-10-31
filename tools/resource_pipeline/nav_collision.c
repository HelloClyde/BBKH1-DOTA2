/* PC-only original Source2 physics-to-navigation edge rasterizer.
 * Inputs: world XYZ float32 triangles,256^2 float32 ground heights,
 * 256^2 uint8 walk cells. Output: right then down256^2 blocked bytes.
 * A swept hull is represented by three transverse rays at three body heights.
 * Original GNV occupancy remains authoritative; physics only removes links. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#define N 256
#define CELL 16.0f
#define RADIUS 6.0f
#define STEP 32.0f /* source worldspawn _dotatilegrid_stepheight128 /4 */
static unsigned char walk[N*N],right_edge[N*N],down_edge[N*N];
static float height[N*N];
static FILE *proof;
static unsigned long current_triangle;
static int clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static void cross(const float a[3],const float b[3],float r[3]){r[0]=a[1]*b[2]-a[2]*b[1];r[1]=a[2]*b[0]-a[0]*b[2];r[2]=a[0]*b[1]-a[1]*b[0];}
static float dot(const float a[3],const float b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static int hit(const float t[9],const float a[3],const float b[3],float *fraction){
    float e1[3],e2[3],d[3],p[3],s[3],q[3],det,u,v,w;int i;
    for(i=0;i<3;++i){e1[i]=t[3+i]-t[i];e2[i]=t[6+i]-t[i];d[i]=b[i]-a[i];s[i]=a[i]-t[i];}
    cross(d,e2,p);det=dot(e1,p);if(fabsf(det)<1e-6f)return 0;
    u=dot(s,p)/det;if(u<-.0001f || u>1.0001f)return 0;
    cross(s,e1,q);v=dot(d,q)/det;if(v<-.0001f || u+v>1.0001f)return 0;
    w=dot(e2,q)/det;*fraction=w;return w>=-.0001f && w<=1.0001f;
}
static int blocked(const float t[9],int x,int y,int down){
    int i,j,a=y*N+x,b=down?a+N:a+1;float ta[3],tb[3],top=t[2],bottom=t[2],ha=height[a],hb=height[b];
    static const float levels[3]={8.0f,20.0f,32.0f};
    if(!walk[a] || !walk[b])return 1;
    for(i=1;i<3;++i){if(t[i*3+2]>top)top=t[i*3+2];if(t[i*3+2]<bottom)bottom=t[i*3+2];}
    /* A riser terminating at a reachable floor is a legal step; a fence
     * extending above both feet is not. Never infer a ramp from height alone. */
    if(fabsf(ha-hb)<=STEP+.01f && top<=fmaxf(ha,hb)+.5f && bottom<=fminf(ha,hb)+.5f)return 0;
    for(i=-1;i<=1;++i)for(j=0;j<3;++j){
        ta[0]=(x+.5f)*CELL+(down?i*RADIUS:0);ta[1]=(y+.5f)*CELL+(down?0:i*RADIUS);ta[2]=ha+levels[j];
        tb[0]=ta[0]+(down?0:CELL);tb[1]=ta[1]+(down?CELL:0);tb[2]=hb+levels[j];
        {float fraction;if(hit(t,ta,tb,&fraction)){
            fprintf(proof,"%lu,%d,%d,%d,%d,%g,%g,%g,%g,%g,%g\n",current_triangle,x,y,down,i,levels[j],ta[0]+fraction*(tb[0]-ta[0]),ta[1]+fraction*(tb[1]-ta[1]),ta[2]+fraction*(tb[2]-ta[2]),ha,hb);return 1;
        }}
    }
    return 0;
}
int main(int argc,char **argv){
    FILE *f,*out;float t[9];unsigned long count=0,walls=0,added_right=0,added_down=0;int x,y,k;
    if(argc!=6){fprintf(stderr,"nav_collision triangles heights walk output evidence.csv\n");return 2;}
    proof=fopen(argv[5],"w");if(!proof)return 3;
    fprintf(proof,"triangle,cell_x,cell_y,down,transverse_step,body_z,hit_x,hit_y,hit_z,ground_a,ground_b\n");
    f=fopen(argv[2],"rb");if(!f || fread(height,sizeof(float),N*N,f)!=N*N)return 3;fclose(f);
    f=fopen(argv[3],"rb");if(!f || fread(walk,1,N*N,f)!=N*N)return 3;fclose(f);
    for(y=0;y<N;++y)for(x=0;x<N;++x){int a=y*N+x;right_edge[a]=x==N-1 || !walk[a] || !walk[a+1];down_edge[a]=y==N-1 || !walk[a] || !walk[a+N];}
    f=fopen(argv[1],"rb");if(!f)return 3;
    while(fread(t,sizeof(float),9,f)==9){
        float e1[3],e2[3],n[3],length,minx=t[0],maxx=t[0],miny=t[1],maxy=t[1],minz=t[2],maxz=t[2];int x0,x1,y0,y1;
        current_triangle=count++;for(k=0;k<3;++k){e1[k]=t[3+k]-t[k];e2[k]=t[6+k]-t[k];}cross(e1,e2,n);length=sqrtf(dot(n,n));
        if(length<1e-8f || fabsf(n[2])>length*.70f)continue;
        for(k=1;k<3;++k){float a=t[k*3],b=t[k*3+1],c=t[k*3+2];if(a<minx)minx=a;if(a>maxx)maxx=a;if(b<miny)miny=b;if(b>maxy)maxy=b;if(c<minz)minz=c;if(c>maxz)maxz=c;}
        if(maxz-minz<4.0f || maxx<0 || minx>4096 || maxy<0 || miny>4096)continue;
        ++walls;x0=clamp((int)floorf((minx-RADIUS-8)/CELL)-1,0,N-1);x1=clamp((int)floorf((maxx+RADIUS-8)/CELL)+1,0,N-1);
        y0=clamp((int)floorf((miny-RADIUS-8)/CELL)-1,0,N-1);y1=clamp((int)floorf((maxy+RADIUS-8)/CELL)+1,0,N-1);
        for(y=y0;y<=y1;++y)for(x=x0;x<=x1;++x){int a=y*N+x;
            if(x<N-1 && !right_edge[a] && blocked(t,x,y,0)){right_edge[a]=1;++added_right;}
            if(y<N-1 && !down_edge[a] && blocked(t,x,y,1)){down_edge[a]=1;++added_down;}
        }
    }
    if(ferror(f)){fclose(f);return 3;}fclose(f);out=fopen(argv[4],"wb");if(!out)return 3;
    if(fwrite(right_edge,1,N*N,out)!=N*N || fwrite(down_edge,1,N*N,out)!=N*N || fclose(out))return 3;
    if(fclose(proof))return 3;
    printf("{\"source_triangles\":%lu,\"wall_triangles\":%lu,\"physics_right_edges_added\":%lu,\"physics_down_edges_added\":%lu}\n",count,walls,added_right,added_down);return 0;
}
