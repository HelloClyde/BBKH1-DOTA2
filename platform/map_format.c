/* Tiny format/CRC helpers, no OS functions or additional libraries. */
#include "map_sidecar_format.h"
#include "map_crc_table.h"
uint32_t mapc_crc32_update(uint32_t state,const unsigned char *p,unsigned n){
    while(n--)state=mapc_crc_table[(state^*p++)&255u]^(state>>8);
    return state;
}
uint32_t mapc_crc32(const unsigned char *p,unsigned n){return mapc_crc32_update(0xffffffffu,p,n)^0xffffffffu;}
uint32_t mapc_header_crc32(const unsigned char *p){
    unsigned i;uint32_t state=0xffffffffu;
    for(i=0;i<MAPC_HEADER_BYTES;++i){unsigned byte=i>=52u&&i<56u?0u:p[i];state=mapc_crc_table[(state^byte)&255u]^(state>>8);}
    return state^0xffffffffu;
}
