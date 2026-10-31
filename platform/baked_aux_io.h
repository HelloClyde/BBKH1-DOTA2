/* Independent optional file. No main metadata pages or cursor are reused. */
#ifndef H1_OPTIONAL_AUX_IO_H
#define H1_OPTIONAL_AUX_IO_H
static BakedFile *aux_file;
static uint32_t aux_bytes,aux_position;
static int aux_position_known,aux_last_error;
static BakedIOAuxStats aux_stats;
static int aux_fail(int code){aux_last_error=code;++aux_stats.errors;return 0;}
static void aux_reset_stats(void){unsigned i;unsigned char*p=(unsigned char*)&aux_stats;for(i=0;i<sizeof(aux_stats);i++)p[i]=0;}
void baked_io_aux_close(void){
    BakedFile*old=aux_file;aux_file=0;aux_bytes=0;aux_position=0;aux_position_known=0;
    if(old){++aux_stats.closes;if(file_close(old)!=0)(void)aux_fail(BAKED_IO_CLOSE_FAILED);}
}
int baked_io_aux_open(const char*path){
    int length;baked_io_aux_close();aux_last_error=BAKED_IO_OK;++aux_stats.open_attempts;
    if(!path || !*path || !files_available()){++aux_stats.open_failures;aux_last_error=BAKED_IO_OPEN_FAILED;return 0;}
    aux_file=file_open(path);
    if(!aux_file){++aux_stats.open_failures;aux_last_error=BAKED_IO_OPEN_FAILED;return 0;}
    ++aux_stats.seeks;++io_stats.seeks;length=file_seek(aux_file,0,2);
    if(length<0){baked_io_aux_close();++aux_stats.open_failures;return aux_fail(BAKED_IO_SIZE_FAILED);}
    aux_bytes=(uint32_t)length;aux_position=aux_bytes;aux_position_known=1;return 1;
}
uint32_t baked_io_aux_size(void){return aux_bytes;}
int baked_io_aux_is_open(void){return aux_file!=0;}
int baked_io_aux_error(void){return aux_last_error;}
const BakedIOAuxStats*baked_io_aux_stats(void){return &aux_stats;}
int baked_io_aux_read_at(uint32_t offset,void*dst,uint32_t bytes){
    unsigned char*out=(unsigned char*)dst;++aux_stats.read_requests;++io_stats.read_requests;
    if(!aux_file)return aux_fail(BAKED_IO_NOT_OPEN);
    if(offset>aux_bytes || bytes>aux_bytes-offset || (bytes&&!dst))return aux_fail(BAKED_IO_RANGE);
    if(!bytes){aux_last_error=BAKED_IO_OK;return 1;}
    if(!aux_position_known || aux_position!=offset){
        ++aux_stats.seeks;++io_stats.seeks;
        if(file_seek(aux_file,(int)offset,0)!=(int)offset){aux_position_known=0;return aux_fail(BAKED_IO_SEEK_FAILED);}
        aux_position=offset;aux_position_known=1;
    }
    while(bytes){uint32_t want=bytes>IO_READ_CHUNK?IO_READ_CHUNK:bytes,got;
        ++aux_stats.raw_reads;++io_stats.raw_reads;got=file_read(out,want,aux_file);
        if(!got || got>want){aux_position_known=0;return aux_fail(BAKED_IO_READ_FAILED);}
        aux_stats.bytes_read+=got;io_stats.bytes_read+=got;aux_position+=got;out+=got;bytes-=got;
    }
    aux_last_error=BAKED_IO_OK;return 1;
}
#endif
