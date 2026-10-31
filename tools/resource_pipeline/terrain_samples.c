/* PC-only offline terrain attribute sampler. Do NOT build into the H1 BDA.
 *
 * terrain_samples INPUT.f32 OUTPUT.f32 originX originY step width height
 * Input record: 43 little-endian IEEE binary32 floats:
 *   material_id, vertex0[14], vertex1[14], vertex2[14]
 * Vertex: x,y,z,u,v,w1,w2,w3,s1,s2,s3,tR,tG,tB.
 * material_id must be a nonnegative integer represented exactly by the input.
 * Output: row-major height*width records, each 13 little-endian binary32 floats:
 *   matid,z,u,v,w1,w2,w3,s1,s2,s3,tR,tG,tB.
 * Sample XY: origin + (index + .5)*step. Geometry/attributes use double
 * barycentric arithmetic (no perspective). Highest double z wins; exact ties
 * retain the first record. Unknown: matid=-1, z=-FLT_MAX, other fields zero.
 * Coverage u,v >= -.001 and u+v <= 1.001; bbox includes this tolerance.
 * Degenerate XY area |den| < 1e-12 is skipped. UV/weights/tints are not clamped.
 * Memory bounded to 4,194,304 samples (240 MiB including double depth).
 *
 * gcc -std=c99 -O2 -ffp-contract=off -Wall -Wextra -Werror \
 *     tools/terrain_samples.c -o terrain_samples.exe
 */
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHANNELS 13u
#define INPUT_FLOATS 43u
#define COVERAGE_EPS .001
#define MAX_DIMENSION 65536ul
#define MAX_SAMPLES ((size_t)4194304u)

static float decode_f32(const unsigned char *p)
{
    uint32_t bits=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    float value;memcpy(&value,&bits,4);return value;
}

static void encode_f32(unsigned char *p,float value)
{
    uint32_t bits;memcpy(&bits,&value,4);
    p[0]=(unsigned char)bits;p[1]=(unsigned char)(bits>>8);
    p[2]=(unsigned char)(bits>>16);p[3]=(unsigned char)(bits>>24);
}

static int parse_double(const char *text,double *value)
{
    char *end;errno=0;*value=strtod(text,&end);
    return !errno && *text && !*end && isfinite(*value);
}

static int parse_dimension(const char *text,unsigned *value)
{
    char *end;unsigned long n;errno=0;n=strtoul(text,&end,10);
    if(errno || !*text || *end || *text=='-' || n<1 || n>MAX_DIMENSION)return 0;
    *value=(unsigned)n;return 1;
}

static int pixel_bounds(double low,double high,double origin,double step,unsigned count,
                        unsigned *first,unsigned *last)
{
    /* Two barycentric weights can each be -EPS, expanding an XY range by at
       most 2*EPS*range. Include that band before clipping pixel centres. */
    double padding=2.0*COVERAGE_EPS*(high-low);
    double a=ceil((low-padding-origin)/step-.5);
    double b=floor((high+padding-origin)/step-.5);
    if(b<0 || a>(double)(count-1) || a>b)return 0;
    if(a<0)a=0;
    if(b>(double)(count-1))b=(double)(count-1);
    *first=(unsigned)a;*last=(unsigned)b;return 1;
}

static int raster(float *samples,double *depth,unsigned width,unsigned height,
                  double origin_x,double origin_y,double step,const float record[43])
{
    double v[3][14],min_x,max_x,min_y,max_y,den;
    unsigned x0,x1,y0,y1,x,y;
    int vertex,channel;
    for(vertex=0;vertex<3;++vertex)for(channel=0;channel<14;++channel)
        v[vertex][channel]=(double)record[1+vertex*14+channel];
    den=(v[1][1]-v[2][1])*(v[0][0]-v[2][0])+
        (v[2][0]-v[1][0])*(v[0][1]-v[2][1]);
    if(!isfinite(den))return -1;
    if(fabs(den)<1e-12)return 0;
    min_x=max_x=v[0][0];min_y=max_y=v[0][1];
    for(vertex=1;vertex<3;++vertex){
        if(v[vertex][0]<min_x)min_x=v[vertex][0];
        if(v[vertex][0]>max_x)max_x=v[vertex][0];
        if(v[vertex][1]<min_y)min_y=v[vertex][1];
        if(v[vertex][1]>max_y)max_y=v[vertex][1];
    }
    if(!pixel_bounds(min_x,max_x,origin_x,step,width,&x0,&x1) ||
       !pixel_bounds(min_y,max_y,origin_y,step,height,&y0,&y1))return 1;
    for(y=y0;y<=y1;++y){
        double py=origin_y+((double)y+.5)*step;
        for(x=x0;x<=x1;++x){
            double px=origin_x+((double)x+.5)*step;
            double u=((v[1][1]-v[2][1])*(px-v[2][0])+
                      (v[2][0]-v[1][0])*(py-v[2][1]))/den;
            double w=((v[2][1]-v[0][1])*(px-v[2][0])+
                      (v[0][0]-v[2][0])*(py-v[2][1]))/den;
            double z;
            size_t index;
            float output[13];
            if(u< -COVERAGE_EPS || w< -COVERAGE_EPS || u+w>1.0+COVERAGE_EPS)continue;
            /* Affine barycentrics preserve a constant attribute exactly. */
            z=v[2][2]+u*(v[0][2]-v[2][2])+w*(v[1][2]-v[2][2]);
            if(!isfinite(z) || z>FLT_MAX || z< -FLT_MAX)return -1;
            index=(size_t)y*width+x;
            if(z<=depth[index])continue;
            output[0]=record[0];output[1]=(float)z;
            for(channel=3;channel<14;++channel){
                double value=v[2][channel]+u*(v[0][channel]-v[2][channel])+w*(v[1][channel]-v[2][channel]);
                if(!isfinite(value) || value>FLT_MAX || value< -FLT_MAX)return -1;
                output[channel-1]=(float)value;
            }
            memcpy(samples+index*CHANNELS,output,sizeof(output));depth[index]=z;
        }
    }
    return 1;
}

int main(int argc,char **argv)
{
    double origin_x,origin_y,step,*depth;
    unsigned width,height;
    float *samples;
    size_t count,i,covered=0;
    unsigned long long triangles=0,degenerate=0;
    unsigned char encoded[INPUT_FLOATS*4];
    FILE *input,*output;
    int status=0;
    if(argc!=8){fprintf(stderr,"Usage: terrain_samples INPUT OUTPUT originX originY step width height\n");return 2;}
    if(sizeof(float)!=4 || FLT_RADIX!=2 || FLT_MANT_DIG!=24 || FLT_MAX_EXP!=128){fprintf(stderr,"IEEE binary32 float required\n");return 2;}
    if(strcmp(argv[1],argv[2])==0){fprintf(stderr,"Input and output must differ\n");return 2;}
    if(!parse_double(argv[3],&origin_x) || !parse_double(argv[4],&origin_y) ||
       !parse_double(argv[5],&step) || step<=0 || !parse_dimension(argv[6],&width) || !parse_dimension(argv[7],&height)){
        fprintf(stderr,"Invalid origin, positive step or dimensions (1..%lu)\n",MAX_DIMENSION);return 2;
    }
    if(!isfinite(origin_x+((double)width-.5)*step) || !isfinite(origin_y+((double)height-.5)*step)){
        fprintf(stderr,"Pixel centres overflow double\n");return 2;
    }
    if((size_t)width>SIZE_MAX/(size_t)height){fprintf(stderr,"Sample count overflow\n");return 2;}
    count=(size_t)width*height;
    if(count>MAX_SAMPLES || count>SIZE_MAX/(CHANNELS*sizeof(float)) || count>SIZE_MAX/sizeof(double)){
        fprintf(stderr,"Sample grid exceeds bounded memory budget (%zu samples)\n",MAX_SAMPLES);return 2;
    }
    samples=(float *)malloc(count*CHANNELS*sizeof(float));depth=(double *)malloc(count*sizeof(double));
    if(!samples || !depth){fprintf(stderr,"Cannot allocate sample grid\n");free(samples);free(depth);return 2;}
    memset(samples,0,count*CHANNELS*sizeof(float));
    for(i=0;i<count;++i){samples[i*CHANNELS]=-1;samples[i*CHANNELS+1]=-FLT_MAX;depth[i]=-DBL_MAX;}
    input=fopen(argv[1],"rb");
    if(!input){perror("Cannot open terrain input");free(samples);free(depth);return 2;}
    for(;;){
        size_t got=fread(encoded,1,sizeof(encoded),input);
        float record[43];int channel,result;
        if(!got){if(ferror(input)){perror("Terrain input read failed");status=3;}break;}
        if(got!=sizeof(encoded)){fprintf(stderr,"Truncated terrain record after %llu records (%zu/172 bytes)\n",triangles,got);status=3;break;}
        for(channel=0;channel<43;++channel){
            record[channel]=decode_f32(encoded+channel*4);
            if(!isfinite(record[channel])){fprintf(stderr,"Non-finite value in record %llu channel %d\n",triangles,channel);status=3;break;}
        }
        if(status)break;
        if(record[0]<0 || floor((double)record[0])!=(double)record[0]){
            fprintf(stderr,"Material id must be a nonnegative exact integer (record %llu)\n",triangles);status=3;break;
        }
        result=raster(samples,depth,width,height,origin_x,origin_y,step,record);
        if(result<0){fprintf(stderr,"Non-finite/overflowed interpolation in record %llu\n",triangles);status=3;break;}
        ++triangles;if(!result)++degenerate;
    }
    if(fclose(input)!=0 && !status){perror("Terrain input close failed");status=3;}
    if(status){free(samples);free(depth);return status;}
    output=fopen(argv[2],"wb");
    if(!output){perror("Cannot open terrain output");free(samples);free(depth);return 4;}
    for(i=0;i<count;++i){
        unsigned char pixel[CHANNELS*4];unsigned channel;
        if(samples[i*CHANNELS]>=0)++covered;
        for(channel=0;channel<CHANNELS;++channel)encode_f32(pixel+channel*4,samples[i*CHANNELS+channel]);
        if(fwrite(pixel,1,sizeof(pixel),output)!=sizeof(pixel)){perror("Terrain output write failed");status=4;break;}
    }
    if(fclose(output)!=0 && !status){perror("Terrain output close failed");status=4;}
    free(samples);free(depth);
    if(status)return status;
    printf("{\"records\":%llu,\"degenerate\":%llu,\"width\":%u,\"height\":%u,\"covered_samples\":%zu,\"floats_per_pixel\":13,\"coverage_epsilon\":0.001}\n",triangles,degenerate,width,height,covered);
    return 0;
}
