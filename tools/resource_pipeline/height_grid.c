/* PC-only offline height rasterizer. Never compile this file into the H1 BDA.
 *
 * Usage: height_grid INPUT.f32 OUTPUT.f32 GRID_SIZE
 * Input: tightly packed little-endian IEEE float32 triangles, N * 3 * XYZ.
 * Output: GRID_SIZE squared little-endian float32 values, row-major y/x.
 * World x/y cover 0..4095 inclusive; unknown samples are -FLT_MAX.
 *
 * Match prepare_3d_assets.py.heights: float32 source differences/denominator
 * and bbox scaling, double sample coordinates/barycentrics, u/v >= -.001,
 * u+v <= 1.001, maximum z. Compile without fast-math/FMA contraction:
 * gcc -std=c99 -O2 -ffp-contract=off tools/height_grid.c -o height_grid.exe
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GRID_SIZE 8192u

static float decode_f32_le(const unsigned char *bytes)
{
    uint32_t bits=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|
                  ((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
    float value;
    memcpy(&value,&bits,4);
    return value;
}

static int write_f32_le(FILE *file,float value)
{
    uint32_t bits;
    unsigned char bytes[4];
    memcpy(&bits,&value,4);
    bytes[0]=(unsigned char)bits;bytes[1]=(unsigned char)(bits>>8);
    bytes[2]=(unsigned char)(bits>>16);bytes[3]=(unsigned char)(bits>>24);
    return fwrite(bytes,1,4,file)==4;
}

static unsigned bbox_index(float coordinate,float scale,unsigned size,int upper)
{
    /* NumPy multiplies the float32 bbox array by a float32-cast scale. Clamp
       before conversion so extreme but finite source coordinates cannot cause
       undefined float-to-integer overflow. */
    float scaled=coordinate*scale;
    if(scaled<=0.0f)return 0;
    if(scaled>=(float)(size-1))return size-1;
    return (unsigned)(upper?ceilf(scaled):floorf(scaled));
}

static int raster_triangle(float *grid,unsigned size,double scale,const float t[9])
{
    float min_x=t[0],max_x=t[0],min_y=t[1],max_y=t[1];
    float by_cy=t[4]-t[7],ax_cx=t[0]-t[6];
    float cx_bx=t[6]-t[3],ay_cy=t[1]-t[7];
    float cy_ay=t[7]-t[1],ax_cx_v=t[0]-t[6];
    volatile float den_left=by_cy*ax_cx;
    volatile float den_right=cx_bx*ay_cy;
    float den=den_left+den_right;
    unsigned x0,x1,y0,y1,x,y;
    int i;
    if(!isfinite(den))return -1;
    if(fabsf(den)<1e-8f)return 0;
    for(i=1;i<3;++i){
        float px=t[i*3],py=t[i*3+1];
        if(px<min_x)min_x=px;
        if(px>max_x)max_x=px;
        if(py<min_y)min_y=py;
        if(py>max_y)max_y=py;
    }
    x0=bbox_index(min_x,(float)scale,size,0);
    x1=bbox_index(max_x,(float)scale,size,1);
    y0=bbox_index(min_y,(float)scale,size,0);
    y1=bbox_index(max_y,(float)scale,size,1);
    for(y=y0;y<=y1;++y){
        double sy=(double)y/scale;
        for(x=x0;x<=x1;++x){
            double sx=(double)x/scale;
            double u=((double)by_cy*(sx-t[6])+(double)cx_bx*(sy-t[7]))/(double)den;
            double v=((double)cy_ay*(sx-t[6])+(double)ax_cx_v*(sy-t[7]))/(double)den;
            double z;
            size_t index;
            if(u<-.001 || v<-.001 || u+v>1.001)continue;
            z=u*t[2]+v*t[5]+(1.0-u-v)*t[8];
            if(!isfinite(z) || z>FLT_MAX || z< -FLT_MAX)return -1;
            index=(size_t)y*size+x;
            if(z>(double)grid[index])grid[index]=(float)z;
        }
    }
    return 1;
}

int main(int argc,char **argv)
{
    unsigned long parsed;
    unsigned size;
    size_t count,i,bytes;
    unsigned long long triangles=0,degenerate=0;
    unsigned char encoded[36];
    float *grid;
    double scale;
    FILE *input,*output;
    char *end;
    int status=0;
    if(argc!=4){fprintf(stderr,"Usage: height_grid INPUT.f32 OUTPUT.f32 GRID_SIZE (2..%u)\n",MAX_GRID_SIZE);return 2;}
    if(sizeof(float)!=4 || FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128){
        fprintf(stderr,"IEEE binary32 float is required\n");return 2;
    }
    if(strcmp(argv[1],argv[2])==0){fprintf(stderr,"Input and output must differ\n");return 2;}
    errno=0;end=NULL;parsed=strtoul(argv[3],&end,10);
    if(errno || !end || !*argv[3] || *end || argv[3][0]=='-' || parsed<2 || parsed>MAX_GRID_SIZE){
        fprintf(stderr,"Invalid grid size; expected 2..%u\n",MAX_GRID_SIZE);return 2;
    }
    size=(unsigned)parsed;
    if((size_t)size>SIZE_MAX/(size_t)size){fprintf(stderr,"Grid count overflow\n");return 2;}
    count=(size_t)size*size;
    if(count>SIZE_MAX/sizeof(float)){fprintf(stderr,"Grid allocation overflow\n");return 2;}
    bytes=count*sizeof(float);
    grid=(float *)malloc(bytes);
    if(!grid){fprintf(stderr,"Cannot allocate %zu grid bytes\n",bytes);return 2;}
    for(i=0;i<count;++i)grid[i]=-FLT_MAX;
    input=fopen(argv[1],"rb");
    if(!input){perror("Cannot open triangle input");free(grid);return 2;}
    scale=(double)(size-1)/4095.0;
    for(;;){
        size_t got=fread(encoded,1,sizeof(encoded),input);
        float triangle[9];
        int result,j;
        if(!got){if(ferror(input)){perror("Triangle input read failed");status=3;}break;}
        if(got!=sizeof(encoded)){fprintf(stderr,"Truncated triangle after %llu complete triangles (%zu/36 bytes)\n",triangles,got);status=3;break;}
        for(j=0;j<9;++j){
            triangle[j]=decode_f32_le(encoded+j*4);
            if(!isfinite(triangle[j])){fprintf(stderr,"Non-finite source coordinate in triangle %llu\n",triangles);status=3;break;}
        }
        if(status)break;
        result=raster_triangle(grid,size,scale,triangle);
        if(result<0){fprintf(stderr,"Non-finite/overflowed raster arithmetic in triangle %llu\n",triangles);status=3;break;}
        ++triangles;
        if(!result)++degenerate;
    }
    if(fclose(input)!=0 && !status){perror("Triangle input close failed");status=3;}
    /* A malformed/truncated stream never creates or modifies the output. */
    if(status){free(grid);return status;}
    output=fopen(argv[2],"wb");
    if(!output){perror("Cannot open grid output");free(grid);return 4;}
    for(i=0;i<count;++i)if(!write_f32_le(output,grid[i])){perror("Grid output write failed");status=4;break;}
    if(fclose(output)!=0 && !status){perror("Grid output close failed");status=4;}
    free(grid);
    if(status)return status;
    fprintf(stdout,"{\"triangles\":%llu,\"degenerate\":%llu,\"grid_size\":%u,\"output_bytes\":%zu}\n",triangles,degenerate,size,bytes);
    return 0;
}
