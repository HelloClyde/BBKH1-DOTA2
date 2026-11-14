#include "render.h"
#include "vision.h"
#include "objectives.h"
#include "courier.h"
#include "items.h"
#include "item_world.h"
#include "item_assets.h"
#include "shop_catalog.h"
#include "neutral.h"
#include "sight.h"
#include "items_special.h"
#include "extra_heroes.h"
#include "extra_ability.h"
#include "progression.h"
#include "ability_icon_state.h"
#include "runes.h"
#include "match_time.h"
#include "items_actions.h"
#include "items_params.h"
#include "vision_assets.h"
#ifdef DOTA_BAKED
#include "baked_scene.h"
typedef BSCamera DotaCamera;
#define s3d_depth_buffer bs_depth_buffer
#define s3d_unit_screen_bounds bs_unit_screen_bounds
#else
#include "assets3d.h"
typedef S3DCamera DotaCamera;
#endif
#include "../private/generated/dota_art.h"
#include "../private/generated/dota_shard_art.h"
#include "../private/generated/dota_font.h"
#include "../private/generated/dota_overview.h"
#include "../private/generated/dota_map_fixtures.h"
#include "../private/generated/dota_rune_art.h"

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define INK RGB(225,235,228)
#define DIM RGB(142,165,157)
#define GOLD RGB(224,188,109)
#define GREEN RGB(101,199,131)
#define RED RGB(226,104,92)
#define PANEL RGB(16,29,30)
#define EDGE RGB(56,78,74)

uint16_t dota_pixels[480*272];
static int clip_top=0,clip_bottom=271;
#include "ability_icon_overlay.h"
static DotaStatusEffect hud_status_effects[DOTA_STATUS_MAX];
static int hud_status_count;
static int hud_status_info_visible;
static const char *hero_names[DOTA_HERO_TYPES]={"斧王","莉娜","主宰","水晶室女","斯拉克","瘟疫法师","帕吉","狙击手","斯温","潮汐猎人"};
static const char *hero_roles[DOTA_HERO_TYPES]={"坚韧前排 / 近战","火焰法师 / 远程","剑术核心 / 近战","冰霜辅助 / 远程","偷取属性 / 近战","死亡脉冲 / 远程","肉钩先手 / 近战","远距狙击 / 远程","巨剑核心 / 近战","潮汐控制 / 近战"};
static const char *skill_names[DOTA_HERO_TYPES][4]={
    {"狂战士之吼","战斗饥渴","反击螺旋","淘汰之刃"},
    {"龙破斩","光击阵","炽魂","神灭斩"},
    {"剑刃风暴","治疗守卫","剑舞","无敌斩"},
    {"冰霜新星","冰封禁制","奥术光环","极寒领域"},
    {"黑暗契约","突袭","海浪短刀","暗影之舞"},
    {"死亡脉冲","幽魂护罩","竭心光环","死神镰刀"},
    {"肉钩","腐烂","肉盾","肢解"},
    {"榴霰弹","爆头","瞄准","暗杀"},
    {"风暴之拳","巨力挥舞","战吼","神之力量"},
    {"巨浪","海妖外壳","锚击","毁灭"}
};
extern const DotaArt *item_assets_hero_art(int hero);
extern const DotaArt *item_assets_ability_art(int hero,int skill);
extern const DotaArt *item_assets_item_art(int item);
extern void item_assets_text(int item,int field,char *dst,int cap);
static const DotaArt *hero_art(int hero){return (unsigned)hero<4u?dota_art_hero(hero):item_assets_hero_art(hero);}
static const DotaArt *ability_art(int hero,int skill){const DotaArt *shard=skill==4?dota_shard_ability_art(hero):0;return shard?shard:(unsigned)hero<4u&&(unsigned)skill<4u?dota_art_ability(hero,skill):item_assets_ability_art(hero,skill);}
static const char *event_names[29]={"","法力不足","技能冷却中","技能尚未学习","没有有效目标","金币不足","背包已满","请靠近对应商店","购买成功","没有治疗药膏","","此店不售该物品","魔方已授予魔晶","魔方金币奖励","","目标超出施法距离","击败肉山，获得不朽守护","不朽守护触发","肉山抵挡了技能","祭坛经验已领取","采集到治疗莲花","侦察眼已占领","敌方侦察眼已关闭","暂时无法使用","没有治疗莲花","已下单，信使自动送货","信使已送达","暂存空间已满","物品尚未送达"};
static void bar(int x,int y,int w,int h,int value,int maximum,uint16_t color);

static int absi(int v){return v<0?-v:v;}
static int clampi(int v,int low,int high){return v<low?low:(v>high?high:v);}
static void pixel(int x,int y,uint16_t c){if((unsigned)x<480u && y>=clip_top && y<=clip_bottom)dota_pixels[y*480+x]=c;}
static void box(int x,int y,int w,int h,uint16_t c){
    int xx,yy,x1=x+w,y1=y+h;
    if(x<0)x=0;
    if(y<clip_top)y=clip_top;
    if(x1>480)x1=480;
    if(y1>clip_bottom+1)y1=clip_bottom+1;
    for(yy=y;yy<y1;++yy){
        uint16_t *row=dota_pixels+yy*480;
        for(xx=x;xx+7<x1;xx+=8){row[xx]=c;row[xx+1]=c;row[xx+2]=c;row[xx+3]=c;row[xx+4]=c;row[xx+5]=c;row[xx+6]=c;row[xx+7]=c;}
        for(;xx<x1;++xx)row[xx]=c;
    }
}
static void frame(int x,int y,int w,int h,uint16_t c){box(x,y,w,1,c);box(x,y+h-1,w,1,c);box(x,y,1,h,c);box(x+w-1,y,1,h,c);}
static int outcode(int x,int y){return (x<0?1:x>479?2:0)|(y<clip_top?4:y>clip_bottom?8:0);}
static void line(int x,int y,int xx,int yy,uint16_t c){
    int dx,dy,sx,sy,err,e,a=outcode(x,y),b=outcode(xx,yy);
    /* Clip the segment before rasterizing roads that extend beyond the camera. */
    while(a|b){
        int outside,nx,ny;
        if(a&b)return;
        outside=a?a:b;
        if(outside&8){ny=clip_bottom;nx=x+(xx-x)*(ny-y)/(yy-y);}
        else if(outside&4){ny=clip_top;nx=x+(xx-x)*(ny-y)/(yy-y);}
        else if(outside&2){nx=479;ny=y+(yy-y)*(nx-x)/(xx-x);}
        else{nx=0;ny=y+(yy-y)*(nx-x)/(xx-x);}
        if(outside==a){x=nx;y=ny;a=outcode(x,y);}else{xx=nx;yy=ny;b=outcode(xx,yy);}
    }
    dx=absi(xx-x);dy=-absi(yy-y);sx=x<xx?1:-1;sy=y<yy?1:-1;err=dx+dy;
    for(;;){pixel(x,y,c);if(x==xx && y==yy)break;e=err*2;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
static void circle(int cx,int cy,int r,uint16_t c){
    int x=r,y=0,err=1-r;if(r<1)return;if(r>180)r=180;
    x=r;err=1-r;
    while(x>=y){
        pixel(cx+x,cy+y,c);pixel(cx+y,cy+x,c);pixel(cx-y,cy+x,c);pixel(cx-x,cy+y,c);
        pixel(cx-x,cy-y,c);pixel(cx-y,cy-x,c);pixel(cx+y,cy-x,c);pixel(cx+x,cy-y,c);
        ++y;if(err<0)err+=2*y+1;else{--x;err+=2*(y-x)+1;}
    }
}
static uint16_t blend(uint16_t foreground,uint16_t background,unsigned alpha){
    unsigned inv=255-alpha;
    unsigned r=(((foreground>>11)&31)*alpha+((background>>11)&31)*inv+127)/255;
    unsigned g=(((foreground>>5)&63)*alpha+((background>>5)&63)*inv+127)/255;
    unsigned b=((foreground&31)*alpha+(background&31)*inv+127)/255;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static void art(int x,int y,const DotaArt *a){
    int xx,yy;if(!a)return;
    for(yy=0;yy<a->h;++yy)if(y+yy>=clip_top && y+yy<=clip_bottom)
        for(xx=0;xx<a->w;++xx)if((unsigned)(x+xx)<480u){
            unsigned alpha=a->alpha[yy*a->w+xx];int p=(y+yy)*480+x+xx;
            if(alpha==255)dota_pixels[p]=a->pixels[yy*a->w+xx];
            else if(alpha)dota_pixels[p]=blend(a->pixels[yy*a->w+xx],dota_pixels[p],alpha);
        }
}
static void art_small(int x,int y,const DotaArt *a){
    int xx,yy;if(!a)return;
    for(yy=0;yy<20;++yy)for(xx=0;xx<20;++xx){
        unsigned q=(unsigned)(yy*a->h/20)*a->w+(unsigned)(xx*a->w/20),alpha=a->alpha[q];
        if((unsigned)(x+xx)>=480u || y+yy<clip_top || y+yy>clip_bottom)continue;
        if(alpha){int p=(y+yy)*480+x+xx;dota_pixels[p]=alpha==255?a->pixels[q]:blend(a->pixels[q],dota_pixels[p],alpha);}
    }
}
static void ability_icon(int x,int y,const DotaArt *a){
    int xx,yy;if(!a)return;
    for(yy=0;yy<24;++yy)for(xx=0;xx<24;++xx){
        unsigned q=(unsigned)(yy*a->h/24)*a->w+(unsigned)(xx*a->w/24),alpha=a->alpha[q];
        if((unsigned)(x+xx)>=480u||y+yy<clip_top||y+yy>clip_bottom)continue;
        if(alpha){int p=(y+yy)*480+x+xx;dota_pixels[p]=alpha==255?a->pixels[q]:blend(a->pixels[q],dota_pixels[p],alpha);}
    }
}
static unsigned utf8(const char **text){
    const unsigned char *p=(const unsigned char *)*text;unsigned cp;
    if(*p<128){cp=*p;*text+=1;return cp;}
    if((*p&224)==192 && p[1]){cp=((p[0]&31)<<6)|(p[1]&63);*text+=2;return cp;}
    if((*p&240)==224 && p[1] && p[2]){cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);*text+=3;return cp;}
    *text+=1;return '?';
}
static int text_width(const char *s){int w=0;while(*s){unsigned cp=utf8(&s);w+=cp<128?7:12;}return w;}
static void text(int x,int y,const char *s,uint16_t color){
    int origin=x;
    while(*s){unsigned cp=utf8(&s);const DotaGlyph *g;int xx,yy;
        if(cp=='\n'){y+=16;x=origin;continue;}
        g=dota_font_find(cp);if(!g)g=item_assets_glyph(cp);
        if(g)for(yy=0;yy<14;++yy)for(xx=0;xx<g->width;++xx)if(g->rows[yy]&(1u<<xx))pixel(x+xx,y+yy,color);
        x+=cp<128?7:12;
    }
}
static void center(int x,int y,int width,const char *s,uint16_t c){text(x+(width-text_width(s))/2,y,s,c);}
static void text_fit(int x,int y,int width,const char *s,uint16_t c){
    char out[192];int n=0,w=0;const char *p=s;
    while(*p){const char *before=p;unsigned cp=utf8(&p);int step=cp<128?7:12,len=(int)(p-before);if(w+step>width || n+len>188)break;while(before<p)out[n++]=*before++;w+=step;}
    out[n]=0;text(x,y,out,c);
}
/* Source tooltips contain markup. Keep their text and paragraphs, wrapping at
 * UTF-8 codepoint boundaries; scrolling changes visible lines only. */
static void tooltip(int x,int y,int width,int lines,const char *s,int scroll){
    char row[192];int n=0,w=0,line_no=0,markup=0;const char *p=s;
    while(*p || n){const char *before=p;unsigned cp=*p?utf8(&p):'\n';int step=cp<128?7:12,len=(int)(p-before);
        if(cp=='<'){markup=1;continue;}if(markup){if(cp=='>')markup=0;continue;}
        if(cp=='\r')continue;
        if(cp=='\n' || w+step>width || n+len>188){row[n]=0;if(line_no>=scroll && line_no<scroll+lines)text(x,y+16*(line_no-scroll),row,INK);++line_no;n=w=0;if(line_no>=scroll+lines)return;if(cp=='\n')continue;}
        while(before<p)row[n++]=*before++;
        w+=step;
    }
}
static int append_text(char *dst,int used,int cap,const char *s){while(*s && used<cap-1)dst[used++]=*s++;dst[used]=0;return used;}
static int append_value(char *dst,int used,int cap,int value,int fractional){
    char reverse[16],out[24];int n=0,k=0,rem=0;if(value<0){out[k++]='-';value=-value;}
    if(fractional){rem=value%100;value/=100;}
    do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value && n<14);
    while(n)out[k++]=reverse[--n];
    if(rem){out[k++]='.';out[k++]=(char)('0'+rem/10);if(rem%10)out[k++]=(char)('0'+rem%10);}
    out[k]=0;return append_text(dst,used,cap,out);
}
static void item_tooltip(int item,const DotaItemRecord *r,char *out,int cap){
    static const char *labels[ITEM_STAT_COUNT]={"生命","法力","攻击","护甲","力量","敏捷","智力","攻速","移动速度","移速%","生命恢复/秒","法力恢复/秒","魔抗%","闪避%","吸血%","技能增强%","冷却缩减%","施法距离","攻击距离","状态抗性%","法术吸血%","治疗增强%","减甲","范围加成"};
    int i,n=0;
    for(i=0;i<ITEM_STAT_COUNT;++i)if(r->stats[i]){int fractional=i==ITEM_STAT_ARMOR || (i>=ITEM_STAT_STRENGTH && i<=ITEM_STAT_INTELLECT) || (i>=ITEM_STAT_MOVE_PERCENT && i<=ITEM_STAT_COOLDOWN_REDUCTION) || (i>=ITEM_STAT_STATUS_RESIST && i<=ITEM_STAT_ARMOR_REDUCTION);
        n=append_text(out,n,cap,labels[i]);n=append_text(out,n,cap," +");n=append_value(out,n,cap,r->stats[i],fractional);n=append_text(out,n,cap,"  ");}
    if(n)n=append_text(out,n,cap,"\n");
    item_assets_text(item,1,out+n,cap-n);
}
static void number(int x,int y,int value,uint16_t c){
    char out[16],reverse[14];int n=0,i=0;if(value<0){out[i++]='-';value=-value;}
    do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value && n<13);
    while(n)out[i++]=reverse[--n];
    out[i]=0;text(x,y,out,c);
}
static int number_width(int value){int n=1;while(value>=10){value/=10;++n;}return n*7;}
static void kda(int x,int y,int kills,int deaths,int assists,uint16_t color){
    number(x,y,kills,color);x+=number_width(kills);text(x,y,"/",DIM);x+=7;
    number(x,y,deaths,color);x+=number_width(deaths);text(x,y,"/",DIM);x+=7;number(x,y,assists,color);
}
const DotaStatusEffect *dota_status_snapshot(int *count){if(count)*count=hud_status_count;return hud_status_effects;}
int dota_status_info_visible(void){return hud_status_info_visible;}
static void tiny_digit(int x,int y,int digit,uint16_t color){
    static const unsigned char glyphs[11][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{0,0,10,21,21,10,0}};
    int row,col;for(row=0;row<7;++row)for(col=0;col<5;++col)if(glyphs[digit][row]&(16u>>col))pixel(x+col,y+row,color);
}
static void tiny_number(int x,int y,int value,uint16_t color){
    int digits[3],n=0,i;value=clampi(value,0,999);
    do{digits[n++]=value%10;value/=10;}while(value && n<3);
    box(x-1,y-1,n*6+1,9,RGB(7,14,15));
    for(i=0;i<n;++i)tiny_digit(x+i*6,y,digits[n-i-1],color);
}
static void tiny_char(int x,int y,char code,uint16_t color){
    static const char keys[]="QWERZXCVBNUITY+-/.%A";
    static const unsigned char rows[][7]={
        {14,17,17,17,21,18,13},{17,17,17,21,21,21,10},{31,16,16,30,16,16,31},{30,17,17,30,20,18,17},
        {31,1,2,4,8,16,31},{17,17,10,4,10,17,17},{14,17,16,16,16,17,14},{17,17,17,17,17,10,4},
        {30,17,17,30,17,17,30},{17,25,21,19,17,17,17},{17,17,17,17,17,17,14},{14,4,4,4,4,4,14},
        {31,4,4,4,4,4,4},{17,17,10,4,4,4,4},{0,4,4,31,4,4,0},{0,0,0,31,0,0,0},
        {1,1,2,4,8,16,16},{0,0,0,0,0,12,12},{25,26,2,4,8,11,19},{14,17,17,31,17,17,17}};
    int i,r,c;if(code>='0'&&code<='9'){tiny_digit(x,y,code-'0',color);return;}
    for(i=0;keys[i]&&keys[i]!=code;++i){}if(!keys[i])return;
    for(r=0;r<7;++r)for(c=0;c<5;++c)if(rows[i][r]&(16u>>c))pixel(x+c,y+r,color);
}
static int tiny_unsigned(char *out,unsigned value){char reversed[10];int n=0,i;do{reversed[n++]=(char)('0'+value%10u);value/=10u;}while(value&&n<10);for(i=0;i<n;++i)out[i]=reversed[n-i-1];return n;}
static void tiny_string(int x,int y,const char *value,uint16_t color){while(*value){tiny_char(x,y,*value++,color);x+=6;}}
static void hotkey_badge(int x,int y,char key,uint16_t color){box(x,y,7,9,RGB(9,16,17));tiny_char(x+1,y+1,key,color);}
static void shade_art(int x,int y,int w,int h){int xx,yy;for(yy=0;yy<h;++yy)for(xx=0;xx<w;++xx)if((unsigned)(x+xx)<480u&&y+yy>=clip_top&&y+yy<=clip_bottom){unsigned at=(unsigned)(y+yy)*480u+(unsigned)(x+xx);dota_pixels[at]=(uint16_t)((dota_pixels[at]>>1)&0x7befu);}}
static void resource_bar(int x,int y,int value,int maximum,int regen100,uint16_t fill){
    char ratio[24],regen[20];int n=0,r=0,i,start;unsigned magnitude=regen100<0?(unsigned)(-(long long)regen100):(unsigned)regen100;
    bar(x,y,DOTA_HUD_SKILLS_W-2,DOTA_HUD_RESOURCE_H,value,maximum,fill);
    n+=tiny_unsigned(ratio+n,(unsigned)(value<0?0:value));ratio[n++]='/';n+=tiny_unsigned(ratio+n,(unsigned)(maximum<0?0:maximum));ratio[n]=0;
    regen[r++]=regen100<0?'-':'+';magnitude=(magnitude+5u)/10u;r+=tiny_unsigned(regen+r,magnitude/10u);regen[r++]='.';regen[r++]=(char)('0'+magnitude%10u);regen[r]=0;
    start=x+DOTA_HUD_SKILLS_W-4-r*6;for(i=0;i<n&&x+2+i*6+5<start-3;++i)tiny_char(x+2+i*6,y+3,ratio[i],INK);
    tiny_string(start,y+3,regen,regen100<0?RED:INK);
}
static const DotaArt *status_art(int id){
    if(id>=DOTA_STATUS_RUNE && id<DOTA_STATUS_RUNE+DOTA_RUNE_COUNT)return dota_rune_icons[id-DOTA_STATUS_RUNE];
    if(id>=DOTA_STATUS_ROSTER){int code=id-DOTA_STATUS_ROSTER;return ability_art(code/8,code%8);}
    if(id>=DOTA_STATUS_ITEM_GENERIC)return item_assets_item_art(id-DOTA_STATUS_ITEM_GENERIC);
    switch(id){
        case DOTA_STATUS_PACT:return ability_art(DOTA_HERO_SLARK,0);
        case DOTA_STATUS_LEASH:return ability_art(DOTA_HERO_SLARK,1);
        case DOTA_STATUS_ESSENCE_GAIN:case DOTA_STATUS_ESSENCE_LOSS:case DOTA_STATUS_ESSENCE_PERMANENT:case DOTA_STATUS_SHIV_GAIN:case DOTA_STATUS_SHIV_LOSS:return ability_art(DOTA_HERO_SLARK,2);
        case DOTA_STATUS_SHADOW:case DOTA_STATUS_DEPTH_SHROUD:return ability_art(DOTA_HERO_SLARK,3);
        case DOTA_STATUS_GHOST:case DOTA_STATUS_GHOST_SLOW:return ability_art(DOTA_HERO_NECROPHOS,1);
        case DOTA_STATUS_HEARTSTOPPER:case DOTA_STATUS_SADIST:return ability_art(DOTA_HERO_NECROPHOS,2);
        case DOTA_STATUS_SCYTHE_REGEN:case DOTA_STATUS_REAPERS_SCYTHE:return ability_art(DOTA_HERO_NECROPHOS,3);
        case DOTA_STATUS_TAUNT:case DOTA_STATUS_CALL:return dota_art_ability(DOTA_HERO_AXE,0);
        case DOTA_STATUS_HUNGER:return dota_art_ability(DOTA_HERO_AXE,1);
        case DOTA_STATUS_SPIN:return dota_art_ability(DOTA_HERO_JUGGERNAUT,0);
        case DOTA_STATUS_FIERY:return dota_art_ability(DOTA_HERO_LINA,2);
        case DOTA_STATUS_OMNI:return dota_art_ability(DOTA_HERO_JUGGERNAUT,3);
        case DOTA_STATUS_FIELD:return dota_art_ability(DOTA_HERO_CRYSTAL_MAIDEN,3);
        case DOTA_STATUS_FLASK:return dota_art_item(DOTA_FLASK);
        case DOTA_STATUS_WARD:return dota_art_ability(DOTA_HERO_JUGGERNAUT,1);
        case DOTA_STATUS_ARCANE:return dota_art_ability(DOTA_HERO_CRYSTAL_MAIDEN,2);
        default:return 0;
    }
}
static void skill_level_marks(int x,int y,int rank,int maximum){
    int i;
    if(maximum<1)return;
    x+=(19-(maximum*5-1))/2;
    for(i=0;i<maximum;++i)box(x+i*5,y,4,2,i<rank?GOLD:RGB(60,70,58));
}
static const char *status_symbol(int id){
    switch(id){
        case DOTA_STATUS_STUN:return "晕";
        case DOTA_STATUS_SLOW:return "缓";
        case DOTA_STATUS_DOT:return "伤";
        case DOTA_STATUS_SHARD:return "晶";
        case DOTA_STATUS_FOUNTAIN:return "泉";
        case DOTA_STATUS_TORMENTOR_BURN:return "灼";
        case DOTA_STATUS_TORMENTOR_HEAL:return "愈";
        case DOTA_STATUS_AEGIS:return "盾";
        case DOTA_STATUS_ROSHAN_DISARM:return "缴";
        case DOTA_STATUS_ROSHAN_VULNERABLE:return "弱";
        default:return "+";
    }
}
static void status_bars(const DotaGame *g){
    int i,slots[2]={0,0};
    hud_status_count=dota_status_effects(g,g->player,hud_status_effects,DOTA_STATUS_MAX);
    for(i=0;i<hud_status_count;++i){
        const DotaStatusEffect *s=&hud_status_effects[i];int kind=s->harmful?1:0,slot=slots[kind]++;
        int base=kind?DOTA_HUD_DEBUFF_X:DOTA_HUD_BUFF_X,x=base+DOTA_HUD_STATUS_LABEL_W+slot*DOTA_HUD_STATUS_SLOT_W,y=DOTA_HUD_STATUS_Y;
        uint16_t color=kind?RED:GREEN;const DotaArt *icon=status_art(s->id);
        if(!slot){box(base,y,DOTA_HUD_STATUS_LABEL_W,DOTA_HUD_STATUS_H,PANEL);text(base+2,y+4,kind?"减益":"增益",color);}
        if(slot>=DOTA_HUD_STATUS_SLOTS)continue;
        box(x,y,DOTA_HUD_STATUS_ICON_W,DOTA_HUD_STATUS_H,kind?RGB(57,29,28):RGB(25,48,35));
        if(icon)art(x+1,y+1,icon);else text(x+5,y,status_symbol(s->id),color);
        if(s->ticks>0)tiny_number(x+2,y+14,(s->ticks+DOTA_HZ-1)/DOTA_HZ,INK);
        else{box(x+1,y+13,7,9,RGB(7,14,15));tiny_digit(x+2,y+14,10,INK);}
        if(s->stacks>0)tiny_number(x+15,y+2,s->stacks,GOLD);
        frame(x,y,DOTA_HUD_STATUS_ICON_W,DOTA_HUD_STATUS_H,color);
    }
}
static void status_detail(const DotaGame *g){
    const char *name=dota_status_name(g->last_event_value);const DotaStatusEffect *active=0;int i,x,width,pos;
    DotaStatusEffect permanent={DOTA_STATUS_SHARD,0,0,0};
    if(g->last_event_value>=DOTA_STATUS_ITEM_GENERIC&&g->last_event_value<DOTA_STATUS_ROSTER){static char item_status_name[96];item_assets_text(g->last_event_value-DOTA_STATUS_ITEM_GENERIC,0,item_status_name,sizeof(item_status_name));if(*item_status_name)name=item_status_name;}
    if(!name || !*name)return;
    for(i=0;i<hud_status_count;++i)if(hud_status_effects[i].id==g->last_event_value){active=&hud_status_effects[i];break;}
    if(!active && g->last_event_value==DOTA_STATUS_SHARD && dota_hero_has_shard(g,g->player))active=&permanent;
    width=DOTA_HUD_STATUS_INFO_W;x=DOTA_HUD_STATUS_INFO_X;hud_status_info_visible=1;
    box(x,154,width,20,PANEL);frame(x,154,width,20,EDGE);text(x+8,157,name,active?(active->harmful?RED:GREEN):DIM);
    pos=x+18+text_width(name);
    if(!active)text(pos,157,"已结束",DIM);
    else{
        if(active->ticks>0){int seconds=(active->ticks+DOTA_HZ-1)/DOTA_HZ;number(pos,157,seconds,INK);pos+=number_width(seconds)+3;text(pos,157,"秒",DIM);pos+=17;}
        else{text(pos,157,"持续生效",DIM);pos+=54;}
        if(active->stacks>0){number(pos,157,active->stacks,GOLD);text(pos+number_width(active->stacks)+3,157,"层",DIM);}
    }
}
static void button(int x,int y,int w,int h,const char *label,int selected){
    box(x,y,w,h,selected?RGB(65,61,42):RGB(25,43,42));frame(x,y,w,h,selected?GOLD:EDGE);
    if(selected)box(x,y,3,h,GOLD);
    center(x,y+(h-14)/2,w,label,selected?GOLD:INK);
}
static DotaCamera scene_camera(const DotaView *v){
#ifdef DOTA_BAKED
    DotaCamera camera;camera.x=v->camera_x;camera.y=v->camera_y;
    camera.z=bs_ground_height(camera.x,camera.y);return camera;
#else
    S3DCamera camera;const S3DAssets *assets=dota_scene_assets();
    camera.x=v->camera_x;camera.y=v->camera_y;
    camera.z=s3d_ground_height(assets,camera.x,camera.y);camera.zoom_q8=180;
    return camera;
#endif
}
int dota_screen_to_world(const DotaView *v,int x,int y,int *world_x,int *world_y){
    DotaCamera camera;
    if(!v || !world_x || !world_y)return 0;
    camera=scene_camera(v);
#ifdef DOTA_BAKED
    return bs_pick_ground(&camera,x,y,world_x,world_y);
#else
    return s3d_pick_ground(dota_scene_assets(),&camera,x,y,world_x,world_y);
#endif
}
int dota_hit_owned_ward(const DotaGame *g,int x,int y){
    int ward,b[4];uint16_t nearest;const uint16_t *depth=s3d_depth_buffer();
    if(!g || x<0 || x>=480 || y<24 || y>=DOTA_HUD_TOP)return -1;
    ward=dota_healing_ward(g,g->player);
    if(ward<0 || !s3d_unit_screen_bounds(ward,b,&nearest))return -1;
    if(x<b[0]-3 || x>b[2]+3 || y<b[1]-3 || y>b[3]+3)return -1;
    return (unsigned)depth[(y-24)*480+x]+24u<(unsigned)nearest?-1:ward;
}
int dota_hit_shop(int x,int y){
#ifdef DOTA_BAKED
    int i,b[4];uint16_t nearest;const uint16_t *depth=bs_depth_buffer();
    if(x<0 || x>=480 || y<24 || y>=DOTA_HUD_TOP)return -1;
    for(i=0;i<2;++i)if(bs_shop_screen_bounds(i,b,&nearest) && x>=b[0] && x<=b[2] && y>=b[1] && y<=b[3] &&
        (unsigned)depth[(y-24)*480+x]+24u>=(unsigned)nearest)return i;
#else
    (void)x;(void)y;
#endif
    return -1;
}
int dota_hit_unit(const DotaGame *g,const DotaView *v,int x,int y,int relation,int units_only){
    int i,best=-1,best_depth=65536,best_distance=0x7fffffff;
    const uint16_t *depth=s3d_depth_buffer();
    (void)v;
    if(!g || (v && v->scoreboard) || x<0 || x>=480 || y<24 || y>=DOTA_HUD_TOP)return -1;
    for(i=0;i<DOTA_MAX_UNITS;++i){
        const DotaUnit *u=&g->units[i];int bounds[4],distance,dx,dy;uint16_t nearest;
        int friendly=u->team==g->units[g->player].team;
        if(!u->active || u->hp<=0 || (!friendly && dota_unit_invulnerable(g,i)) ||
           (relation==DOTA_HIT_ENEMY && friendly) || (relation==DOTA_HIT_FRIENDLY && !friendly) || !dota_unit_visible(g,g->player,i) ||
           (units_only && (u->type==DOTA_TOWER || u->type==DOTA_BASE || u->type==DOTA_BUILDING)) || !s3d_unit_screen_bounds(i,bounds,&nearest))continue;
        if(x<bounds[0]-3 || x>bounds[2]+3 || y<bounds[1]-3 || y>bounds[3]+3)continue;
        /* A nearer terrain/prop surface can occlude this entire model bound. */
        if((unsigned)depth[(y-24)*480+x]+24u<(unsigned)nearest)continue;
        dx=x-(bounds[0]+bounds[2])/2;dy=y-(bounds[1]+bounds[3])/2;distance=dx*dx+dy*dy;
        if((int)nearest<best_depth || ((int)nearest==best_depth && distance<best_distance)){
            best=i;best_depth=nearest;best_distance=distance;
        }
    }
    return best;
}
int dota_hit_enemy(const DotaGame *g,const DotaView *v,int x,int y,int units_only){return dota_hit_unit(g,v,x,y,DOTA_HIT_ENEMY,units_only);}
static void bar(int x,int y,int w,int h,int value,int maximum,uint16_t color){
    int fill=maximum>0?clampi(value,0,maximum)*w/maximum:0;
    box(x,y,w,h,RGB(9,18,18));box(x,y,fill,h,color);
}
static void backdrop(void){
    int y;clip_top=0;clip_bottom=271;
    for(y=0;y<272;++y)box(0,y,480,1,RGB(10+y/24,22+y/16,25+y/22));
    line(0,236,220,16,RGB(31,48,46));line(185,272,457,0,RGB(28,45,43));
    line(265,272,480,57,RGB(28,45,43));
}
static void title(void){
    text(22,15,"DOTA2",GOLD);text(114,16,"离线复刻",INK);text(346,18,"H1 / 480 x 272",DIM);
    box(22,44,436,1,EDGE);
}
void dota_render_asset_error(int code){
#ifdef DOTA_RESOURCE_PACK
    backdrop();title();center(20,76,440,code==17?"Cannot load the required resource bundle":code==16?"Cannot load interactive tree data":code==15?"Cannot load terrain vision data":"Cannot load game resource data",GOLD);
    center(20,108,440,"B:\\DOTA2\\DOTA2.H1R",INK);
    center(20,142,440,"Copy the matching program and resource bundle.",DIM);
#else
    backdrop();title();center(20,76,440,code==16?"Cannot load the required interactive trees":code==15?"Cannot load the required terrain vision data":code==13?"Old resource pack. Update BOTH folders.":"Cannot load the animation/map pack",GOLD);
    center(20,108,440,code==16?"B:\\DOTA2\\TREE.H1T":code==15?"B:\\DOTA2\\VISION.H1V":"A:\\DOTA2\\SCENE.H1P",INK);
    center(20,142,440,"Copy BOTH folders from the install ZIP.",DIM);
#endif
    center(20,173,440,"Error code",DIM);number(284,173,code,GOLD);
    center(20,226,440,"ENTER or BACK to exit",INK);
}
/* Six small crops reuse the battlefield framebuffer while preparing the menu.
 * Original baked idle meshes are rendered here; no portrait is used as a body.
 * Current-pose framing includes bound weapons and prevents crop-edge clipping. */
#ifndef DOTA_BAKED
static uint16_t menu_models[DOTA_HERO_TYPES][72*44];
static unsigned menu_models_frame;
static int menu_models_hero,menu_models_valid;
static void prepare_menu_models(const DotaView *v){
    static const int sine[16]={0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};
    const S3DAssets *assets=dota_scene_assets();int hero;unsigned phase=v->frames/3u;
    /* Pose and yaw are unchanged between these three display frames. */
    if(menu_models_valid && menu_models_frame==phase && menu_models_hero==v->hero)return;
    for(hero=0;hero<DOTA_HERO_TYPES;++hero){
        const S3DMesh *mesh=assets && hero<4?assets->heroes[hero]:0;const int16_t *pose=0;
        S3DCamera camera={0,0,80,85};S3DInstance in={mesh,0,0,0,256,0,0,0};
        int i,x,y,min_x=32767,max_x=-32768,min_yz=65535,max_yz=-65536,zoom;
        in.yaw_16=hero==v->hero?(int)(v->frames/12u)&15:0;
        if(mesh && mesh->idle_count && mesh->idle_count<=mesh->frame_count)
            in.frame=(int)((phase+(unsigned)hero)%mesh->idle_count);
        if(mesh && mesh->frame_count && mesh->frame_xyz)
            pose=mesh->frame_xyz+(unsigned)in.frame*mesh->vertex_count*3u;
        if(mesh && mesh->vertices && mesh->vertex_count){
            int si=sine[in.yaw_16],co=sine[(in.yaw_16+4)&15];
            for(i=0;i<mesh->vertex_count;++i){
                const S3DVertex *p=&mesh->vertices[i];
                int px=pose?pose[i*3]:p->x,py=pose?pose[i*3+1]:p->y,pz=pose?pose[i*3+2]:p->z;
                int rx=(px*co-py*si)/256,ry=(px*si+py*co)/256,yz=ry-pz;
                if(rx<min_x)min_x=rx;
                if(rx>max_x)max_x=rx;
                if(yz<min_yz)min_yz=yz;
                if(yz>max_yz)max_yz=yz;
            }
            camera.x=(min_x+max_x)/2;camera.z=-(min_yz+max_yz)/2;
            zoom=68*256/(max_x-min_x+1);
            if(zoom>85)zoom=85;
            if(zoom>40*65536/(181*(max_yz-min_yz+1)))zoom=40*65536/(181*(max_yz-min_yz+1));
            camera.zoom_q8=(unsigned)(zoom>0?zoom:1);
        }
        s3d_begin(dota_pixels,&camera,S3D_TRANSPARENT);
        if(mesh)s3d_draw_instance(&in);
        for(y=0;y<44;++y)for(x=0;x<72;++x)menu_models[hero][y*72+x]=dota_pixels[(98+y)*480+204+x];
    }
    menu_models_frame=phase;menu_models_hero=v->hero;menu_models_valid=1;
}
static void menu_model(int x,int y,int hero){
    int xx,yy;
    for(yy=0;yy<44;++yy)for(xx=0;xx<72;++xx){uint16_t color=menu_models[hero][yy*72+xx];if(color!=S3D_TRANSPARENT)pixel(x+xx,y+yy,color);}
}
#else
static const DotaView *menu_view;
static void prepare_menu_models(const DotaView *v){menu_view=v;}
static void menu_model(int x,int y,int hero){
    bs_draw_menu_hero(dota_pixels,hero,hero==menu_view->hero?(int)(menu_view->frames/12u)&15:0,
                      menu_view->frames,x,y,72,44);
}
#endif
static void menu(const DotaGame *g,const DotaView *v){
    int i,hero=clampi(v->hero,0,DOTA_HERO_TYPES-1);const DotaHeroDef *h=&dota_hero_defs[hero];(void)g;
    prepare_menu_models(v);
    backdrop();title();text(24,51,"选择英雄",INK);text(238,51,"十名英雄 / 每局不重复",DIM);
    for(i=0;i<DOTA_HERO_TYPES;++i){int x=12+92*(i%5),y=70+74*(i/5),selected=i==hero;
        box(x,y,88,68,selected?RGB(41,54,43):RGB(17,34,34));frame(x,y,88,68,selected?GOLD:EDGE);
        if(selected)box(x,y,88,3,GOLD);
        menu_model(x+8,y+3,i);center(x,y+49,88,hero_names[i],selected?GOLD:INK);
    }
    text(24,216,hero_roles[hero],DIM);text(279,216,"生命",DIM);number(308,216,h->hp,INK);text(354,216,"法力",DIM);number(383,216,h->mana,INK);
    button(142,236,196,30,"开始对战  ENTER",1);
    button(12,236,120,30,"历史对局 R",0);button(360,236,102,30,"操作帮助 H",0);
}
static void minimap(const DotaGame *g,const DotaView *v){
    static unsigned char regions[DOTA_HUD_MAP_W*DOTA_HUD_MAP_W];
    static int regions_ready;
    int i,j,x=DOTA_HUD_MAP_X,y=DOTA_HUD_MAP_Y,w=DOTA_HUD_MAP_W;
    unsigned char columns[DOTA_HUD_MAP_W];
    const uint32_t *current=dota_sight_current_bits(g,g->units[g->player].team);
    int observer_region=dota_roshan_region_at(g->units[g->player].x,g->units[g->player].y);
    if(!regions_ready){
        for(j=0;j<w;++j)for(i=0;i<w;++i)regions[j*w+i]=(unsigned char)dota_roshan_region_at(i*DOTA_WORLD/w,j*DOTA_WORLD/w);
        regions_ready=1;
    }
    for(i=0;i<w;++i)columns[i]=(unsigned char)(i*DOTA_SIGHT_GRID_SIDE/w);
    for(j=0;j<w;++j){
      const uint32_t *row=current?current+(j*DOTA_SIGHT_GRID_SIDE/w)*(DOTA_SIGHT_GRID_SIDE/32):0;
      for(i=0;i<w;++i){
        int same_region=observer_region==regions[j*w+i];
        /* floor(floor(i*WORLD/w)/CELL) == floor(i*GRID/w). Borrow the
         * current mask once; no game tick runs during this draw. */
        unsigned cell=columns[i];
        int visible=row&&((row[cell>>5]>>(cell&31u))&1u);
        uint16_t color=dota_overview[j*w+i];
        dota_pixels[(y+j)*480+x+i]=same_region?(visible?color:vision_assets_fog_color(color,3)):RGB(8,8,8);
      }
    }
    for(i=0;i<DOTA_MAX_UNITS;++i){const DotaUnit *u=&g->units[i];int px,py;
        if(!u->active || u->hp<=0)continue;
        if(!dota_unit_visible(g,g->player,i))continue;
        px=clampi(x+u->x*w/DOTA_WORLD,x+1,x+w-2);py=clampi(y+u->y*w/DOTA_WORLD,y+1,y+w-2);
        if(u->type==DOTA_HERO)box(px-1,py-1,3,3,i==g->player?GOLD:u->team==0?GREEN:RED);
        else if(u->type==DOTA_TOWER || u->type==DOTA_BASE)box(px-1,py-1,3,3,u->team==0?RGB(66,137,92):RGB(158,68,61));
        else if(u->type==DOTA_NEUTRAL || u->type==DOTA_ANCIENT)box(px,py,2,2,RGB(207,170,78));
        else if(u->type==DOTA_ROSHAN){box(px-1,py-1,3,3,RGB(238,139,58));}
        else if(u->type==DOTA_SUMMON)box(px,py,2,2,u->team==0?GREEN:RED);
        else if(u->type==DOTA_CREEP)pixel(px,py,u->team==0?RGB(54,118,79):RGB(140,66,60));
    }
    for(i=0;i<2;++i){int sx=x+dota_secret_shops[i].x*w/DOTA_WORLD,sy=y+dota_secret_shops[i].y*w/DOTA_WORLD;
        if(!dota_pit_same_region(g->units[g->player].x,g->units[g->player].y,dota_secret_shops[i].x,dota_secret_shops[i].y))continue;
        sx=clampi(sx,x+2,x+w-3);sy=clampi(sy,y+2,y+w-3);
        box(sx-2,sy-2,5,5,PANEL);frame(sx-1,sy-1,3,3,GOLD);
    }
    {int location=clampi(g->tormentor.location,0,1);
        int sx=x+dota_tormentor_locations[location].x*w/DOTA_WORLD;
        int sy=y+dota_tormentor_locations[location].y*w/DOTA_WORLD;
        sx=clampi(sx,x+2,x+w-3);sy=clampi(sy,y+2,y+w-3);
        if(dota_pit_same_region(g->units[g->player].x,g->units[g->player].y,dota_tormentor_locations[location].x,dota_tormentor_locations[location].y)){
            int visible=dota_sight_position_visible(g,g->player,dota_tormentor_locations[location].x,dota_tormentor_locations[location].y);
            frame(sx-2,sy-2,5,5,visible&&dota_tormentor_index(g)>=0?RGB(100,220,240):DIM);
        }
    }
    for(i=0;i<DOTA_OBJECTIVES;++i){
        const DotaPoint *p=&dota_objective_positions[i];int px,py,visible=dota_sight_position_visible(g,g->player,p->x,p->y),owner=visible?dota_objective_owner(g,i):-1;uint16_t c;
        if(!dota_pit_same_region(g->units[g->player].x,g->units[g->player].y,p->x,p->y))continue;
        px=clampi(x+p->x*w/DOTA_WORLD,x+1,x+w-2);py=clampi(y+p->y*w/DOTA_WORLD,y+1,y+w-2);
        c=i<2?RGB(162,118,238):i<4?RGB(226,144,207):owner==0?GREEN:owner==1?RED:DIM;
        if(i<4)frame(px-1,py-1,3,3,c);else pixel(px,py,c);
    }
    for(i=0;i<DOTA_RUNE_SPOTS;++i)if(dota_rune_kind(g,i)>=0 && dota_sight_position_visible(g,g->player,dota_rune_positions[i].x,dota_rune_positions[i].y)){
        int px=clampi(x+dota_rune_positions[i].x*w/DOTA_WORLD,x+1,x+w-2),py=clampi(y+dota_rune_positions[i].y*w/DOTA_WORLD,y+1,y+w-2);
        box(px-1,py-1,3,3,GOLD);
    }
    {
        int k,min_x=DOTA_WORLD-1,min_y=DOTA_WORLD-1,max_x=0,max_y=0,left,right,top,bottom;
        for(k=0;k<4;++k){int wx,wy;
            if(dota_screen_to_world(v,(k&1)?479:0,(k&2)?DOTA_HUD_TOP-1:24,&wx,&wy)){
                if(wx<min_x)min_x=wx;
                if(wy<min_y)min_y=wy;
                if(wx>max_x)max_x=wx;
                if(wy>max_y)max_y=wy;
            }
        }
        left=clampi(min_x*w/DOTA_WORLD,0,w-1);right=clampi(max_x*w/DOTA_WORLD,0,w-1);
        top=clampi(min_y*w/DOTA_WORLD,0,w-1);bottom=clampi(max_y*w/DOTA_WORLD,0,w-1);
        frame(x+left,y+top,right-left+1,bottom-top+1,RGB(178,197,181));
    }
    frame(x,y,w,w,GOLD);
}
static void topbar(const DotaGame *g,const DotaView *v){
    const DotaUnit *p=&g->units[g->player];int seconds=dota_match_seconds(g),negative=seconds<0,at=0,night=dota_match_is_night(g);char clock[8];
    if(negative){clock[at++]='-';seconds=-seconds;}
    if(seconds>=600)clock[at++]=(char)('0'+(seconds/60)/10%10);
    clock[at++]=(char)('0'+seconds/60%10);clock[at++]=':';clock[at++]=(char)('0'+seconds%60/10);clock[at++]=(char)('0'+seconds%10);clock[at]=0;
    box(0,0,480,24,PANEL);box(0,23,480,1,EDGE);
    box(DOTA_HUD_KDA_X,0,DOTA_HUD_KDA_W,23,v->scoreboard?RGB(49,54,38):RGB(28,40,36));
    text(7,5,"KDA",GOLD);kda(36,5,p->kills,p->deaths,dota_hero_assists(g,g->player),INK);
    text(117,5,v->scoreboard?"-":"+",GOLD);frame(0,0,DOTA_HUD_KDA_W,24,EDGE);
    text(138,5,"天辉",GREEN);number(170,5,g->kills[0],INK);center(202,5,58,clock,negative?GOLD:INK);
    if(night){circle(192,11,5,RGB(166,192,227));circle(195,8,5,PANEL);}
    else {circle(192,11,3,GOLD);line(192,4,192,6,GOLD);line(192,16,192,18,GOLD);line(185,11,187,11,GOLD);line(197,11,199,11,GOLD);}
    number(269,5,g->kills[1],INK);text(297,5,"夜魇",RED);
    center(DOTA_HUD_PAUSE_X,5,DOTA_HUD_PAUSE_W,"暂停",INK);frame(DOTA_HUD_PAUSE_X,0,DOTA_HUD_PAUSE_W,24,EDGE);
}
static void hud(const DotaGame *g,const DotaView *v){
    int i,hero=g->selected_hero,str=0,agi=0,intel=0,points=dota_progression_points(g,g->player),talent_points=dota_progression_talent_points(g,g->player),skill_count=extra_available(g,g->player)?5:4;const DotaUnit *p=&g->units[g->player];
    int damage=p->damage,armor=dota_effective_armor(g,g->player),next=dota_next_level_xp(p->level);
    if(dota_omnislash_phase(g,g->player)==DOTA_OMNISLASH_ACTIVE)damage+=20+5*g->omnislash[g->player].phase_ticks;
    dota_rules_attributes(g,g->player,&str,&agi,&intel);
    box(0,DOTA_HUD_TOP,480,72,PANEL);box(0,DOTA_HUD_TOP,480,1,GOLD);
    for(i=0;i<4;++i){int x=i==0?DOTA_HUD_ATTR_X:i==1?DOTA_HUD_SKILLS_X:i==2?DOTA_HUD_ITEMS_X:DOTA_HUD_GOLD_X;box(x,201,1,71,EDGE);}
    text_fit(76,202,83,hero_names[hero],p->hp>0?INK:RED);
    box(DOTA_HUD_PROGRESS_X,DOTA_HUD_PROGRESS_Y,DOTA_HUD_PROGRESS_W,DOTA_HUD_PROGRESS_H,points?RGB(65,58,30):talent_points?RGB(29,47,62):PANEL);
    frame(DOTA_HUD_PROGRESS_X,DOTA_HUD_PROGRESS_Y,DOTA_HUD_PROGRESS_W,DOTA_HUD_PROGRESS_H,points?GOLD:talent_points?RGB(129,176,234):EDGE);hotkey_badge(162,203,'U',points?GOLD:talent_points?RGB(129,176,234):DIM);if(points)tiny_number(172,206,points,GOLD);else tiny_char(173,206,'+',talent_points?RGB(129,176,234):DIM);
    art_small(76,218,hero_art(hero));text(100,216,"LV",DIM);number(117,216,p->level,GOLD);
    if(next>0){char xp[8];int n=0;bar(139,218,41,3,p->xp,next,RGB(174,129,59));n=tiny_unsigned(xp,(unsigned)clampi((int)((long long)p->xp*100/next),0,100));xp[n++]='%';xp[n]=0;tiny_string(145,224,xp,DIM);}else text(145,217,"满级",GOLD);
    text(100,230,"攻",DIM);number(112,230,damage,INK);text(145,230,"甲",DIM);number(157,230,armor,INK);
    if(p->hp<=0){text(76,244,"复活",DIM);number(102,244,(p->respawn_ticks+DOTA_HZ-1)/DOTA_HZ,RED);}else{text(76,244,"移",DIM);number(89,244,dota_move_speed_source(g,g->player),INK);}
    text(120,244,"攻速",DIM);number(146,244,dota_attack_speed_percent(g,g->player),INK);
    text(76,258,"力",RED);number(88,258,str,INK);text(112,258,"敏",GREEN);number(124,258,agi,INK);text(148,258,"智",RGB(129,176,234));number(160,258,intel,INK);
    for(i=0;i<skill_count;++i){int count=skill_count,x=dota_hud_skill_x(i,count),w=dota_hud_skill_width(i,count),ix=x+(count==4?6:(w-24)/2),ward=i==1?dota_healing_ward(g,g->player):-1;
        DotaAbilityIconState state=ability_icon_state(g,g->player,i);
        int rank=state.rank,maximum=i==4?1:dota_progression_skill_max(hero,i),up=i<4&&dota_progression_can_learn(g,g->player,i);
        int omni=i==3?dota_omnislash_phase(g,g->player):0,passive=state.passive;
        const DotaArt *icon=ability_art(hero,i);
        box(x+1,201,w-2,37,RGB(29,44,42));frame(x+1,201,w-2,37,omni?RGB(120,205,240):v->aim_skill==i||up?GOLD:state.missing_mana?RGB(77,145,235):ward>=0||state.toggled?GREEN:EDGE);
        ability_icon(ix,204,icon);ability_icon_overlay(ix,204,24,&state);
        if(dota_skill_autocast_enabled(g,g->player,i)){frame(ix-1,203,26,26,GREEN);tiny_char(x+w-7,203,'A',GREEN);}
        if(!passive&&rank&&state.cooldown>0){int seconds=(state.cooldown+DOTA_HZ-1)/DOTA_HZ,w=number_width(seconds);
            number(ix+1+(24-w)/2,215,seconds,RGB(0,0,0));number(ix+(24-w)/2,214,seconds,INK);}
        if(state.charges>=0)tiny_number(x+w-12,204,state.charges,state.charges?INK:RED);
        if(!passive)hotkey_badge(x+(count==4?DOTA_HUD_SKILL_KEY_X_OFFSET:2),DOTA_HUD_SKILL_KEY_Y,"QWERT"[i],rank?GOLD:DIM);
        skill_level_marks(x+(count==4?6:(w-19)/2),DOTA_HUD_SKILL_LEVEL_Y+2,rank,maximum);if(up)tiny_char(x+w-8,DOTA_HUD_SKILL_LEVEL_Y,'+',GOLD);
        if(!passive&&rank){
            if(!state.cooldown&&!state.missing_mana)box(x+w-7,DOTA_HUD_SKILL_READY_Y,DOTA_HUD_SKILL_READY_W,DOTA_HUD_SKILL_READY_H,GREEN);
            if(omni)bar(x+2,237,w-4,2,omni==1?6-g->omnislash[g->player].phase_ticks:g->omnislash[g->player].remaining_ticks,omni==1?6:60+5*(dota_omnislash_rank(p)-1),RGB(120,205,240));
            else if(ward>=0)bar(x+2,237,w-4,2,g->summon_life[ward-DOTA_SUMMON_FIRST],dota_healing_ward_lifetime(g,g->player),GREEN);
        }
        if(passive&&(hero==DOTA_HERO_LINA||hero==DOTA_HERO_AXE)&&g->passive_stacks[g->player])tiny_number(x+w-11,203,g->passive_stacks[g->player],p->skill_ticks[2]?GOLD:DIM);
        if(hero==DOTA_HERO_SLARK&&i==3&&rank){
            uint16_t color=dota_extra_shadow_dance_passive_active(g,g->player)?GREEN:DIM;
            line(ix+3,225,ix+6,223,color);line(ix+6,223,ix+9,225,color);line(ix+3,225,ix+6,227,color);line(ix+6,227,ix+9,225,color);pixel(ix+6,225,color);
        }
    }
    resource_bar(DOTA_HUD_SKILLS_X+1,DOTA_HUD_HEALTH_Y,p->hp,p->max_hp,dota_rules_effective_health_regen100(g,g->player),RGB(31,102,55));
    resource_bar(DOTA_HUD_SKILLS_X+1,DOTA_HUD_MANA_Y,dota_items_available_mana(g,g->player),p->max_mana,dota_rules_mana_regen100(g,g->player),RGB(30,70,124));
    {int pending=0;for(i=0;i<9;++i){int x=DOTA_HUD_ITEMS_X+i%3*DOTA_HUD_ITEM_W,y=DOTA_HUD_ITEMS_Y+i/3*DOTA_HUD_ITEM_H,item=dota_inventory_item(g,g->player,i),ghost=-1,lock=0,cd=0,charges=0;
        uint16_t edge=i>=6?RGB(72,72,65):EDGE;
        if(v->inventory_mode&&i==v->inventory_source)edge=GOLD;else if(v->inventory_mode&&i==v->inventory_hover)edge=RGB(110,193,239);
        box(x+1,y+1,26,22,i>=6?RGB(20,27,27):RGB(24,39,38));frame(x+1,y+1,26,22,edge);
        if(item<0)ghost=dota_courier_pending_item(g,g->player,pending++);
        if(item>=0&&item<DOTA_ITEM_TYPES){art_small(x+4,y+2,item_assets_item_art(item));lock=dota_inventory_inactive_ticks(g,g->player,i);if(i>=6||lock)shade_art(x+4,y+2,20,20);cd=dota_inventory_cooldown(g,g->player,i);charges=dota_inventory_charges(g,g->player,i);}
        else if(ghost>=0&&ghost<DOTA_ITEM_TYPES){art_small(x+4,y+2,item_assets_item_art(ghost));shade_art(x+4,y+2,20,20);frame(x+1,y+1,26,22,RGB(90,160,215));}
        if(ghost>=0){uint16_t courier_color=RGB(90,160,215);line(x+13,y+7,x+13,y+16,courier_color);line(x+9,y+12,x+13,y+16,courier_color);line(x+17,y+12,x+13,y+16,courier_color);}
        if(i<6&&item>=0&&dota_items_is_active(item))hotkey_badge(x+2,y+2,"ZXCVBN"[i],lock?DIM:INK);
        if(lock>0)tiny_number(x+10,y+9,(lock+DOTA_HZ-1)/DOTA_HZ,GOLD);else if(cd>0)tiny_number(x+9,y+9,(cd+DOTA_HZ-1)/DOTA_HZ,INK);else if(charges>0){if(item==DOTA_ITEM_WARD_DISPENSER){
            /* Public charge queries return the combined total. Backpack has
             * its own instance; legacy slot6 is neutral, not backpack slot0. */
            int observer=i<6?dota_items_ward_available(g,g->player,i,0):g->items.backpack[g->player][i-6].charges&255;
            int sentry=i<6?dota_items_ward_available(g,g->player,i,1):g->items.backpack[g->player][i-6].charges>>8;
            tiny_number(x+4,y+13,observer,i<6?GOLD:DIM);tiny_number(x+16,y+13,sentry,i<6?RGB(117,166,235):DIM);
        }else tiny_number(x+16,y+13,charges,INK);}
    }}
    text(409,201,"金币",DIM);number(437,201,p->gold,GOLD);
    {int x=DOTA_HUD_TP_X,y=DOTA_HUD_TP_Y,item=dota_inventory_item(g,g->player,DOTA_INVENTORY_TP_SLOT);
        int cd=dota_inventory_cooldown(g,g->player,DOTA_INVENTORY_TP_SLOT),charges=dota_inventory_charges(g,g->player,DOTA_INVENTORY_TP_SLOT);
        box(x,y,DOTA_HUD_TP_W,DOTA_HUD_TP_H,RGB(24,39,38));
        art_small(x+4,y+1,item_assets_item_art(DOTA_ITEM_TPSCROLL));
        if(item<0)shade_art(x+4,y+1,20,20);
        frame(x,y,DOTA_HUD_TP_W,DOTA_HUD_TP_H,v->aim_item==DOTA_ITEMS_TP_SLOT?GOLD:EDGE);
        hotkey_badge(x+1,y+1,'G',item<0?DIM:INK);
        if(cd>0)tiny_number(x+8,y+8,(cd+DOTA_HZ-1)/DOTA_HZ,INK);
        else if(charges>0)tiny_number(x+17,y+12,charges,GOLD);
        text(x+32,y+4,"TP",DIM);
    }
    button(DOTA_HUD_SHOP_X,DOTA_HUD_SHOP_Y,DOTA_HUD_SHOP_W,DOTA_HUD_SHOP_H,"商店 M",1);
}
static void scoreboard(const DotaGame *g){
    int i;
    box(DOTA_HUD_SCORE_X,DOTA_HUD_SCORE_Y,DOTA_HUD_SCORE_W,DOTA_HUD_SCORE_H,PANEL);
    frame(DOTA_HUD_SCORE_X,DOTA_HUD_SCORE_Y,DOTA_HUD_SCORE_W,DOTA_HUD_SCORE_H,GOLD);
    text(15,32,"全员战绩",GOLD);text(154,32,"天辉",GREEN);text(210,32,"夜魇",RED);
    button(DOTA_HUD_SCORE_CLOSE_X,DOTA_HUD_SCORE_CLOSE_Y,DOTA_HUD_SCORE_CLOSE_W,DOTA_HUD_SCORE_CLOSE_H,"X",0);
    text(44,53,"英雄",DIM);text(DOTA_HUD_SCORE_LEVEL_X-10,53,"等级",DIM);
    text(DOTA_HUD_SCORE_KILLS_X-10,53,"击杀",DIM);text(DOTA_HUD_SCORE_DEATHS_X-10,53,"死亡",DIM);text(DOTA_HUD_SCORE_ASSISTS_X-10,53,"助攻",DIM);
    for(i=0;i<DOTA_HERO_COUNT;++i){
        const DotaUnit *u=&g->units[i];int y=DOTA_HUD_SCORE_ROWS_Y+i*DOTA_HUD_SCORE_ROW_H;uint16_t team=u->team==DOTA_RADIANT?GREEN:RED;
        box(9,y-1,462,18,i==g->player?RGB(46,52,36):i%2?RGB(24,39,38):RGB(19,33,33));
        box(10,y,3,15,team);number(18,y,i+1,team);
        text(44,y,(unsigned)u->hero_id<DOTA_HERO_TYPES?hero_names[u->hero_id]:"--",u->hp>0?INK:DIM);
        if(i==g->player)text(126,y,"你",GOLD);
        number(DOTA_HUD_SCORE_LEVEL_X,y,u->level,INK);number(DOTA_HUD_SCORE_KILLS_X,y,u->kills,INK);
        number(DOTA_HUD_SCORE_DEATHS_X,y,u->deaths,INK);number(DOTA_HUD_SCORE_ASSISTS_X,y,dota_hero_assists(g,i),INK);
        if(i==4)box(10,y+16,460,1,EDGE);
    }
    text(15,251,"实时更新 / H 或 ESC 收起",DIM);
}
#ifndef DOTA_BAKED
static const S3DMesh *unit_mesh(const S3DAssets *a,const DotaUnit *u){
    if(!a)return 0;
    if(u->type==DOTA_HERO && (unsigned)u->hero_id<DOTA_HERO_TYPES)return a->heroes[u->hero_id];
    if(u->type==DOTA_TOWER)return a->towers[u->team&1];
    if(u->type==DOTA_BASE)return a->bases[u->team&1];
    if(u->type==DOTA_CREEP)return a->creeps[u->team&1];
    return 0;
}
#endif
static int project_ground(const DotaCamera *camera,int x,int y,int elevation,int *sx,int *sy){
#ifdef DOTA_BAKED
    return bs_project(camera,x,y,bs_ground_height(x,y)+elevation,sx,sy,0);
#else
    const S3DAssets *assets=dota_scene_assets();
    return s3d_project_ground(camera,x,y,s3d_ground_height(assets,x,y)+elevation,sx,sy);
#endif
}
static int objective_anchor(const DotaCamera *camera,int id,int rise,int *x,int *y,int *depth){
#ifdef DOTA_BAKED
    uint16_t d;int result=bs_project(camera,dota_objective_positions[id].x,dota_objective_positions[id].y,dota_objective_heights[id]+rise,x,y,&d);
    if(depth)*depth=d;
    return result;
#else
    if(depth)*depth=32768-dota_objective_positions[id].y-dota_objective_heights[id]-rise;
    return project_ground(camera,dota_objective_positions[id].x,dota_objective_positions[id].y,rise,x,y);
#endif
}
int dota_hit_objective(const DotaGame *g,const DotaView *v,int x,int y){
    DotaCamera camera;int i,best=-1,best_distance=0x7fffffff;
    if(!g || !v || v->scoreboard || x<0 || x>=480 || y<24 || y>=DOTA_HUD_STATUS_Y)return -1;
    camera=scene_camera(v);
    for(i=0;i<DOTA_OBJECTIVES;++i){
        const DotaPoint *p=&dota_objective_positions[i];const short *b=dota_fixture_pixel_bounds[i];int sx,sy,d,dx,dy,distance;
        if(!dota_sight_position_visible(g,g->player,p->x,p->y))continue;
        objective_anchor(&camera,i,0,&sx,&sy,&d);dx=x-sx-b[0];dy=y-sy-b[1];
        if(dx<-3 || dx>b[2]+3 || dy<-3 || dy>b[3]+3)continue;
        if((int)s3d_depth_buffer()[(y-24)*480+x]+2<(int)dota_fixture_min_depth[i])continue;
        dx-=b[2]/2;dy-=b[3]/2;
        distance=dx*dx+dy*dy;
        if(distance<best_distance){best_distance=distance;best=i;}
    }
    return best;
}
static void objective_overlay(const DotaGame *g,const DotaCamera *camera){
    int i;
    for(i=0;i<DOTA_OBJECTIVES;++i){
        const DotaPoint *p=&dota_objective_positions[i];int x,y,d,owner,amount,progress;uint16_t color;
        const char *label=i<2?"经验祭坛":"莲花池";
        if(!dota_sight_position_visible(g,g->player,p->x,p->y))continue;
        objective_anchor(camera,i,0,&x,&y,&d);
        x+=dota_fixture_pixel_bounds[i][0]+dota_fixture_pixel_bounds[i][2]/2;
        y+=dota_fixture_pixel_bounds[i][1]-4;
        if(x<4 || x>=476 || y<40 || y>=DOTA_HUD_STATUS_Y)continue;
        if((int)s3d_depth_buffer()[(y-24)*480+x]+2<(int)dota_fixture_min_depth[i])continue;
        owner=dota_objective_owner(g,i);amount=dota_objective_amount(g,i);progress=dota_objective_progress(g,i);
        color=i<2?RGB(162,118,238):i<4?RGB(226,144,207):owner==0?GREEN:owner==1?RED:DIM;
        if(i<4)text(x-text_width(label)/2,y-14,label,color);
        else {line(x-5,y-4,x,y-7,color);line(x,y-7,x+5,y-4,color);line(x-5,y-4,x,y-1,color);line(x,y-1,x+5,y-4,color);box(x-1,y-5,3,3,color);
            if(owner==-2){line(x-5,y-9,x+5,y+1,DIM);line(x+5,y-9,x-5,y+1,DIM);}}
        if(i<4 && amount>0){text(x-8,y,"+",color);if(i>=2)number(x+2,y,amount,color);}
        if(progress>0)bar(x-20,y+15,40,3,progress,1000,color);
    }
}
static const char *courier_phase_name(const DotaCourierView *c){
    return c->phase==DOTA_COURIER_DELIVER?"送货中":c->phase==DOTA_COURIER_RETURN?"返回取货":c->phase==DOTA_COURIER_WAIT_DEAD?"泉水暂存":"等待取货";
}
static void courier_panel(const DotaGame *g,int x,int y,int width){
    DotaCourierView c;int count=dota_courier_pending_count(g,g->player),at;
    if(!count || !dota_courier_view(g,g->player,&c))return;
    box(x,y,width,20,PANEL);frame(x,y,width,20,RGB(70,123,155));
    text(x+4,y+3,courier_phase_name(&c),RGB(120,196,236));at=x+8+text_width(courier_phase_name(&c));
    number(at,y+3,count,INK);at+=number_width(count)+5;
    if(c.eta_ticks>=0 && at+34<x+width){number(at,y+3,(c.eta_ticks+DOTA_HZ-1)/DOTA_HZ,GOLD);text(at+number_width((c.eta_ticks+DOTA_HZ-1)/DOTA_HZ),y+3,"s",DIM);}
}
static void ground_ring(const DotaCamera *camera,int x,int y,int radius,uint16_t color){
    static const int sine[16]={0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};
    int i,old_x=0,old_y=0;
    radius=clampi(radius,1,1200);
    for(i=0;i<=16;++i){int sx,sy,j=i&15;
        project_ground(camera,x+sine[j]*radius/256,y+sine[(j+4)&15]*radius/256,2,&sx,&sy);
        if(i)line(old_x,old_y,sx,sy,color);
        old_x=sx;old_y=sy;
    }
}
static int rune_anchor(const DotaCamera *camera,int spot,int *x,int *y,int *depth){
    const DotaPoint *p=&dota_rune_positions[spot];
#ifdef DOTA_BAKED
    uint16_t d;int visible=bs_project(camera,p->x,p->y,dota_rune_heights[spot],x,y,&d);*depth=d;return visible;
#else
    *depth=32768-p->y-dota_rune_heights[spot];return project_ground(camera,p->x,p->y,dota_rune_heights[spot],x,y);
#endif
}
static unsigned rune_read16(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8);}
static const DotaRuneFrame *rune_frame(const DotaGame *g,int spot){
    int kind=dota_rune_kind(g,spot);return (unsigned)kind<DOTA_RUNE_COUNT && dota_rune_frames[kind]?&dota_rune_frames[kind][(unsigned)g->tick/5u%8u]:0;
}
int dota_hit_rune(const DotaGame *g,const DotaView *v,int x,int y){
    DotaCamera camera;const uint16_t *depth=s3d_depth_buffer();int spot;
    if(!g || !v || v->scoreboard || x<0 || x>=480 || y<24 || y>=DOTA_HUD_STATUS_Y)return -1;
    camera=scene_camera(v);
    for(spot=0;spot<DOTA_RUNE_SPOTS;++spot){
        const DotaRuneFrame *f=rune_frame(g,spot);int sx,sy,base,i;
        if(!f || !dota_sight_position_visible(g,g->player,dota_rune_positions[spot].x,dota_rune_positions[spot].y))continue;
        rune_anchor(&camera,spot,&sx,&sy,&base);sx-=f->ax;sy-=f->ay;
        if(x<sx-4 || x>=sx+f->w+4 || y<sy-4 || y>=sy+f->h+4)continue;
        for(i=0;i<f->count;++i){const unsigned char *p=f->data+i*7;unsigned q=rune_read16(p);int px=sx+(int)(q%f->w),py=sy+(int)(q/f->w),d=base+(short)rune_read16(p+4);
            if(p[6]>32 && absi(x-px)<=4 && absi(y-py)<=4 && (unsigned)px<480u && py>=24 && py<DOTA_HUD_STATUS_Y && (int)depth[(py-24)*480+px]+2>=d)return spot;
        }
    }
    return -1;
}
static void rune_overlay(const DotaGame *g,const DotaCamera *camera){
    uint16_t *depth=(uint16_t*)s3d_depth_buffer();int spot;
    for(spot=0;spot<DOTA_RUNE_SPOTS;++spot){
        const DotaRuneFrame *f=rune_frame(g,spot);int x,y,base,i;
        if(!f || !dota_sight_position_visible(g,g->player,dota_rune_positions[spot].x,dota_rune_positions[spot].y))continue;
        rune_anchor(camera,spot,&x,&y,&base);x-=f->ax;y-=f->ay;
        if(x+f->w<0 || x>=480 || y+f->h<24 || y>=DOTA_HUD_TOP)continue;
        for(i=0;i<f->count;++i){const unsigned char *p=f->data+i*7;unsigned q=rune_read16(p),alpha=p[6];int sx=x+(int)(q%f->w),sy=y+(int)(q/f->w),d=base+(short)rune_read16(p+4),at;
            if((unsigned)sx>=480u || sy<24 || sy>=DOTA_HUD_TOP || d<0 || d>=65535)continue;
            at=(sy-24)*480+sx;if(d>depth[at])continue;
            pixel(sx,sy,blend((uint16_t)rune_read16(p+2),dota_pixels[sy*480+sx],alpha));if(alpha>=128)depth[at]=(uint16_t)d;
        }
    }
}
static void teleport_overlay(const DotaGame *g,const DotaCamera *camera){
    int h,x,y,left,total,sx,sy;
    for(h=0;h<DOTA_HERO_COUNT;++h)if(dota_items_teleport_channel(g,h,&x,&y,&left,&total)){
        const DotaUnit *u=&g->units[h];uint16_t c=u->team==DOTA_RADIANT?RGB(110,185,250):RGB(238,126,100);
        if(dota_sight_position_visible(g,g->player,u->x,u->y)){ground_ring(camera,u->x,u->y,28,c);ground_ring(camera,u->x,u->y,16+(g->tick%10),c);}
        if(dota_sight_position_visible(g,g->player,x,y)){ground_ring(camera,x,y,28,c);project_ground(camera,x,y,12,&sx,&sy);
            if(sx>20 && sx<460 && sy>42 && sy<174){line(sx-7,sy,sx-7,sy-22,c);line(sx+7,sy,sx+7,sy-22,c);}}
        if(h==g->player){box(176,128,128,24,PANEL);frame(176,128,128,24,c);text(183,132,"传送",c);number(216,132,(left+DOTA_HZ-1)/DOTA_HZ,INK);bar(237,135,58,5,total-left,total,c);}
    }
}
static void unit_overlay(const DotaGame *g,int index,const DotaCamera *camera){
#ifdef DOTA_BAKED
    const DotaUnit *u=&g->units[index];int height,scale=96;
    int bounds[4],x,y,width;uint16_t team=u->team==DOTA_NEUTRAL_TEAM?GOLD:u->team==DOTA_RADIANT?GREEN:RED;
    if(!u->active || u->hp<=0 || !dota_unit_visible(g,g->player,index) || !bs_unit_screen_bounds(index,bounds,0))return;
    height=bs_game_unit_height(g,index);
#else
    const S3DAssets *assets=dota_scene_assets();
    const DotaUnit *u=&g->units[index];const S3DMesh *mesh=unit_mesh(assets,u);
    int bounds[4],x,y,width,scale=s3d_unit_scale(u);uint16_t team=u->team==DOTA_RADIANT?GREEN:RED;
    int height=mesh?mesh->bounds[5]*scale/256:0;
    if(!mesh || !u->active || u->hp<=0 || !s3d_unit_screen_bounds(index,bounds,0))return;
#endif
    if(index==g->player)ground_ring(camera,u->x,u->y,clampi(60*scale/256,20,60),GOLD);
    project_ground(camera,u->x,u->y,height+8,&x,&y);
    width=u->type==DOTA_BASE?46:(u->type==DOTA_CREEP || u->type==DOTA_NEUTRAL || u->type==DOTA_SUMMON?14:32);
    if(u->type==DOTA_ROSHAN){
        text(x-12,y-15,"肉山",GOLD);bar(x-24,y,48,4,u->hp,u->max_hp,team);
    }else if(u->type==DOTA_TORMENTOR){
        text(x-24,y-15,"痛苦魔方",RGB(125,224,239));
        bar(x-24,y,48,4,u->hp-1,u->max_hp-1,RGB(82,183,233));
    }else bar(x-width/2,y,width,3,u->hp,u->max_hp,team);
    if(u->type==DOTA_HERO){
        bar(x-width/2,y+3,width,1,dota_items_available_mana(g,index),u->max_mana,RGB(87,151,218));
        if(u->stun_ticks){text(x-3,y-16,"!",GOLD);circle(x,y-8,5,GOLD);}
        if(u->channel_ticks)ground_ring(camera,u->x,u->y,100,RGB(120,205,240));
    }
}
static void battlefield(const DotaGame *g,const DotaView *v){
    int i,ward_allowed=1;DotaCamera camera=scene_camera(v);
    clip_top=24;clip_bottom=215;
#ifdef DOTA_BAKED
    bs_render_game(dota_pixels,g,&camera);bs_render_effects(dota_pixels,g,&camera);
    for(i=0;i<DOTA_MAX_UNITS;++i)unit_overlay(g,i,&camera);
#else
    const S3DAssets *assets=dota_scene_assets();
    s3d_render_game(dota_pixels,g,&camera,assets);
    for(i=0;i<DOTA_MAX_UNITS;++i)unit_overlay(g,i,&camera);
    for(i=0;i<DOTA_MAX_EFFECTS;++i){const DotaEffect *e=&g->effects[i];int x,y,r;uint16_t c;
        if(!e->active)continue;
        project_ground(&camera,e->x,e->y,2,&x,&y);r=e->radius;c=e->team==0?RGB(242,205,113):RGB(244,123,95);
        if(e->kind==DOTA_FX_ATTACK || e->kind==DOTA_FX_LINE){int to_x,to_y;
            project_ground(&camera,e->x,e->y,56,&x,&y);
            project_ground(&camera,e->to_x,e->to_y,56,&to_x,&to_y);
            line(x,y,to_x,to_y,c);line(x+1,y,to_x+1,to_y,c);
        }
        else if(e->kind==DOTA_FX_HEAL){ground_ring(&camera,e->x,e->y,r,RGB(112,224,154));line(x-4,y,x+4,y,GREEN);line(x,y-4,x,y+4,GREEN);}
        else if(e->kind==DOTA_FX_GOLD)text(x,y,"+",GOLD);
        else{ground_ring(&camera,e->x,e->y,r>6?r:24,c);ground_ring(&camera,e->x,e->y,r>15?r-9:12,c);}
    }
#endif
    for(i=0;i<16;++i){const DotaItemDrop *drop=&g->item_world.drop[i];int x,y;
        if(!drop->active || !dota_sight_position_visible(g,g->player,drop->x,drop->y))continue;
        project_ground(&camera,drop->x,drop->y,6,&x,&y);
        if(x<12 || x>=468 || y<36 || y>=190)continue;
        frame(x-11,y-21,22,22,GOLD);art_small(x-10,y-20,item_assets_item_art(drop->item));
    }
    for(i=0;i<16;++i){const DotaMadstoneDrop *drop=&g->items.neutral.drops[i];int x,y;
        if(!drop->active || !dota_sight_position_visible(g,g->player,drop->x,drop->y))continue;
        project_ground(&camera,drop->x,drop->y,6,&x,&y);
        if(x>=12 && x<468 && y>=36 && y<190){circle(x,y-5,7,GOLD);text(x-6,y-12,"材",GOLD);}
    }
    rune_overlay(g,&camera);teleport_overlay(g,&camera);objective_overlay(g,&camera);
    if(v->target_x>=0 && v->target_y>=0){int x,y,radius=18;
        project_ground(&camera,v->target_x,v->target_y,2,&x,&y);
        if(v->aim_skill>=0 && v->aim_skill<5){
            int hero=g->selected_hero,skill=v->aim_skill;
            if(skill==4)radius=(extra_radius(g,g->player)+3)/4;
            if((hero==DOTA_HERO_LINA && skill==1) || (hero==DOTA_HERO_CRYSTAL_MAIDEN && skill==0) ||
               (hero==DOTA_HERO_JUGGERNAUT && skill==1))radius=(dota_skill_effect_radius_source(g,g->player,skill)+3)/4;
        }
        ground_ring(&camera,v->target_x,v->target_y,radius,GOLD);
        line(x-9,y,x-4,y,GOLD);line(x+4,y,x+9,y,GOLD);line(x,y-9,x,y-4,GOLD);line(x,y+4,x,y+9,GOLD);
        if(v->aim_skill>=0 && v->aim_skill<5){int px,py,range=v->aim_skill==4?extra_range(g,g->player):dota_skill_cast_range_source(g,g->player,v->aim_skill);
            if(!(v->aim_skill==1 && g->selected_hero==DOTA_HERO_JUGGERNAUT && dota_healing_ward(g,g->player)>=0) && range>0)
                ground_ring(&camera,g->units[g->player].x,g->units[g->player].y,(range+3)/4,RGB(117,151,130));
            project_ground(&camera,g->units[g->player].x,g->units[g->player].y,2,&px,&py);line(px,py,x,y,RGB(185,170,89));
        }
        if(v->aim_item>=0){int item=dota_items_cast_item(g,g->player,v->aim_item);DotaItemRecord r;
            if(dota_items_read(item,&r)&&r.action==ITEM_ACTION_WARD){
                int range=(r.cast_range+dota_items_range_bonus((DotaGame *)g,g->player,0)+3)/4,dx=v->target_x-g->units[g->player].x,dy=v->target_y-g->units[g->player].y;
                int sentry=v->ward_mode,source=sentry?DOTA_ITEM_WARD_SENTRY:DOTA_ITEM_WARD_OBSERVER;
                int sight=dota_items_parameter(source,sentry?IP_TRUE_SIGHT_RANGE:IP_OBSERVER_VISION_RANGE_TOOLTIP,-1,0)/100;
                uint16_t c;
                ward_allowed=dota_item_world_ward_placeable(g,g->player,v->target_x,v->target_y);
                if(ward_allowed&&range>0&&dx*dx+dy*dy>range*range)ward_allowed=-1;
                c=ward_allowed>0?GREEN:RED;
                if(range>0)ground_ring(&camera,g->units[g->player].x,g->units[g->player].y,range,RGB(117,151,130));
                if(sight>0)ground_ring(&camera,v->target_x,v->target_y,(sight+3)/4,sentry?RGB(117,166,235):RGB(206,190,98));
                ground_ring(&camera,v->target_x,v->target_y,12,c);line(x,y-5,x,y-18,c);circle(x,y-22,4,c);
            }
        }
    }
    clip_top=0;clip_bottom=271;topbar(g,v);hud(g,v);minimap(g,v);status_bars(g);
    if(v->aim_skill<0 && !v->scoreboard)courier_panel(g,4,27,126);
    if(g->last_event_ticks>0 && g->last_event>2 && g->last_event<29 && g->last_event!=10 && g->last_event!=DOTA_STATUS_INFO_EVENT){
        const char *label=event_names[g->last_event];int w=text_width(label)+24;box((480-w)/2,154,w,20,PANEL);frame((480-w)/2,154,w,20,EDGE);center(0,157,480,label,g->last_event==8?GREEN:GOLD);
    }
    if(g->last_event_ticks>0 && g->last_event==DOTA_STATUS_INFO_EVENT)status_detail(g);
    if(g->units[g->player].hp<=0){box(157,112,166,42,PANEL);frame(157,112,166,42,EDGE);center(157,117,166,"英雄阵亡，等待复活",RED);center(157,134,166,"队友仍在推进",DIM);}
    {int neutral=dota_neutral_equipped(g,g->player);if(neutral>=0){int cd=dota_items_cooldown(g,g->player,6),charges=dota_items_charges(g,g->player,6);box(416,174,28,26,PANEL);art_small(420,175,item_assets_item_art(neutral));frame(416,174,28,26,GOLD);if(cd)tiny_number(426,187,(cd+DOTA_HZ-1)/DOTA_HZ,GOLD);else if(charges>0)tiny_number(432,187,charges,INK);if(dota_items_is_active(neutral))hotkey_badge(417,175,'Y',INK);}}
    if(v->aim_item>=0){int item=dota_items_cast_item(g,g->player,v->aim_item);char label[160];DotaItemRecord r;int ward=dota_items_read(item,&r)&&r.action==ITEM_ACTION_WARD,display=ward?(v->ward_mode?DOTA_ITEM_WARD_SENTRY:DOTA_ITEM_WARD_OBSERVER):item;
        item_assets_text(display,0,label,sizeof(label));box(105,29,290,20,PANEL);if(ward)art_small(111,29,item_assets_item_art(display));text_fit(ward?137:111,32,ward?252:278,label,ward&&v->ward_mode?RGB(117,166,235):GOLD);
        if(ward){box(126,53,228,item==DOTA_ITEM_WARD_DISPENSER?33:19,PANEL);center(126,55,228,ward_allowed>0?"点地放置眼；ESC取消":ward_allowed<0?"目标超出施法距离":"此处不能放置",ward_allowed>0?GOLD:RED);if(item==DOTA_ITEM_WARD_DISPENSER)center(126,69,228,"再按装备键或图标切换",DIM);}
    }
    if(v->aim_skill>=0 && v->aim_skill<5){box(105,29,290,20,PANEL);center(105,32,290,
        v->aim_skill==4?extra_name(g->selected_hero):v->aim_skill==1 && dota_healing_ward(g,g->player)>=0?"移动治疗守卫":skill_names[g->selected_hero][v->aim_skill],GOLD);}
    if(v->inventory_mode&&!v->scoreboard){
        box(105,29,290,20,PANEL);text_fit(111,32,278,v->inventory_mode==1?"方向选物品，ENTER选起；ESC取消":v->inventory_mode==2?"拖到装备或背包，松开换位":"方向选择位置，ENTER移动；ESC取消",GOLD);
        if(v->inventory_mode==2&&v->inventory_source>=0){int item=dota_inventory_item(g,g->player,v->inventory_source);if(item>=0&&v->drag_x>=0&&v->drag_y>=0){int x=clampi(v->drag_x-10,0,458),y=clampi(v->drag_y-10,24,250);box(x-1,y-1,22,22,PANEL);art_small(x,y,item_assets_item_art(item));frame(x-1,y-1,22,22,GOLD);}}
    }
    if(v->scoreboard)scoreboard(g);
}
static void darken(void){int i;for(i=0;i<480*272;++i)dota_pixels[i]=(uint16_t)((dota_pixels[i]>>1)&0x7bef);}
int dota_pause_button_at(int x,int y){
    int column,row;
    if(x<20||y<76)return -1;
    column=(x-20)/150;row=(y-76)/70;
    if(column>=3||row>=2||(x-20)%150>=140||(y-76)%70>=56)return -1;
    return row*3+column;
}
static void pause_screen(const DotaGame *g,const DotaView *v){
    static const char *labels[6]={"继续 P","商店 M","技能与天赋 U","帮助 H","重新选人 C","退出应用 X"};int i;
    battlefield(g,v);darken();box(12,30,456,228,PANEL);frame(12,30,456,228,GOLD);
    center(12,43,456,"战斗已暂停",GOLD);
    for(i=0;i<6;++i)button(20+(i%3)*150,76+(i/3)*70,140,56,labels[i],i==v->pause_selected);
    center(12,215,456,"方向选择 / ENTER确认",DIM);
    center(12,237,456,"BACK / ESC 返回战斗",GOLD);
}
static void progression_screen(const DotaGame *g,const DotaView *v){
    const DotaUnit *u=&g->units[g->player];int hero=u->hero_id,i,points=dota_progression_points(g,g->player),talent_points=dota_progression_talent_points(g,g->player),selected=v->skills_selected,next=dota_next_level_xp(u->level);
    backdrop();text(10,6,"技能与天赋",GOLD);text_fit(120,6,84,hero_names[hero],INK);text(209,6,"LV",DIM);number(225,6,u->level,INK);text(275,6,"技能点",DIM);number(316,6,points,GOLD);text(346,6,"天赋点",DIM);number(389,6,talent_points,RGB(129,176,234));button(438,0,42,24,"返回",0);
    if(v->skills_details){
        const char *name=selected<4?skill_names[hero][selected]:selected<12?dota_progression_talent_name(hero,selected-4):selected==13?dota_progression_innate_name(hero):"属性强化";
        text_fit(16,39,448,name,GOLD);box(16,59,448,1,EDGE);
        if(selected<4){int rank=dota_progression_rank(u,selected),cd=dota_skill_cooldown_ticks(g,g->player,selected);
            text(24,79,"当前等级",DIM);number(92,79,rank,GOLD);text(120,79,"最高等级",DIM);number(185,79,dota_progression_skill_max(hero,selected),INK);
            text(24,107,"下一等级需求",DIM);number(112,107,dota_progression_skill_required(u,selected),INK);text(145,107,"级",DIM);
            if(rank){text(24,137,"耗蓝",DIM);number(60,137,dota_skill_mana_cost(g,g->player,selected),INK);text(150,137,"冷却秒",DIM);number(198,137,(cd+DOTA_HZ-1)/DOTA_HZ,INK);
                text(24,165,"施法距离",DIM);number(84,165,dota_skill_cast_range_source(g,g->player,selected),INK);text(208,165,"作用范围",DIM);number(268,165,dota_skill_effect_radius_source(g,g->player,selected),INK);
            }else text(24,137,"尚未学习；使用技能点提升等级。",DIM);
            text(24,197,dota_skill_is_passive(hero,selected)?"被动自动触发，不设置施法快捷键。":"Q W E R 对应四个技能；SHIFT加键直接加点。",DIM);
        }else if(selected<12){text(20,73,"天赋独立点数",DIM);number(104,73,talent_points,RGB(129,176,234));text(20,96,"同排另一侧按高等级门槛解锁。",DIM);tooltip(20,120,440,8,dota_progression_talent_description(hero,selected-4),v->skills_scroll);}
        else if(selected==13){text(20,73,"先天等级",DIM);number(84,73,dota_progression_innate_rank(u),GOLD);tooltip(20,99,440,8,dota_progression_innate_description(hero),v->skills_scroll);}
        else{ text(24,85,"每级属性强化同时增加力量、敏捷和智力。",INK);text(24,117,"当前每项加成",DIM);number(117,117,dota_progression_attribute_bonus(u),GOLD);
            text(24,151,"强化等级",DIM);number(84,151,u->attribute_bonus_rank,GOLD);text(98,151,"/",DIM);number(108,151,dota_progression_attribute_max(),INK);
            if(u->attribute_bonus_rank<dota_progression_attribute_max()){text(24,181,"下一等级需求",DIM);number(112,181,dota_progression_attribute_required(u),INK);text(140,181,"级",DIM);}else text(24,181,"属性强化已满级",DIM);
        }
        text(16,246,"上下滚动；I 或 ESC 返回技能页",GOLD);return;
    }
    for(i=0;i<4;++i){int x=8+i*94,rank=dota_progression_rank(u,i),maximum=dota_progression_skill_max(hero,i),can=dota_progression_can_learn(g,g->player,i);
        box(x,30,88,47,selected==i?RGB(42,51,37):PANEL);frame(x,30,88,47,selected==i?GOLD:can?GREEN:EDGE);ability_icon(x+4,34,ability_art(hero,i));if(!rank)shade_art(x+4,34,24,24);
        if(!dota_skill_is_passive(hero,i))hotkey_badge(x+2,32,"QWER"[i],rank?GOLD:DIM);
        text_fit(x+32,32,52,skill_names[hero][i],INK);skill_level_marks(x+36,53,rank,maximum);
        if(can)text(x+70,58,"+",GREEN);else if(rank>=maximum)text(x+34,61,"满级",DIM);
        else if(u->level<dota_progression_skill_required(u,i)){int required=dota_progression_skill_required(u,i);text(x+32,61,"需",DIM);number(x+45,61,required,DIM);text(x+45+number_width(required),61,"级",DIM);}else text(x+32,61,"无点数",DIM);
    }
    box(384,30,88,47,selected==13?RGB(42,51,37):PANEL);frame(384,30,88,47,selected==13?GOLD:EDGE);ability_icon(388,34,ability_art(hero,5));text(415,32,"先天",DIM);number(447,32,dota_progression_innate_rank(u),GOLD);text_fit(415,52,52,dota_progression_innate_name(hero),INK);
    for(i=0;i<8;++i){int row=i/2,x=(i&1)?254:8,y=86+row*32,learned=dota_progression_has_talent(u,i),can=dota_progression_can_talent(g,g->player,i);uint16_t color=learned?GOLD:can?INK:DIM;
        box(x,y,218,30,learned?RGB(57,49,29):selected==i+4?RGB(34,46,40):PANEL);frame(x,y,218,30,selected==i+4?GOLD:learned?GOLD:can?GREEN:EDGE);text_fit(x+7,y+8,204,dota_progression_talent_name(hero,i),color);
        if(!(i&1)){int requirement=dota_progression_talent_level(row);number(232,y+8,requirement,u->level>=requirement?GOLD:DIM);}
    }
    button(8,219,160,22,"属性强化 +2",selected==12||dota_progression_can_attributes(g,g->player));
    text(179,222,"经验",DIM);if(next>0){number(208,222,u->xp,INK);text(208+number_width(u->xp),222,"/",DIM);number(215+number_width(u->xp),222,next,INK);}else text(208,222,"满级",GOLD);
    text(8,246,"QWER加点 / 方向选择 / ENTER确认",DIM);button(348,244,124,23,"选中说明 I",0);
}
static void shop(const DotaGame *g,const DotaView *v){
    static const char *tabs[SHOP_CATEGORIES]={"全部","基础","成装","消耗","卷轴","中立","附魔","其他","推荐","升级"};
    static char description[4096];char name[192];DotaItemRecord r;DotaItemPurchasePlan plan;
    int i,count=shop_catalog_count(v->shop_category),index=shop_catalog_index(v->shop_category,v->shop_item),selected=v->shop_item;
    const DotaUnit *p=&g->units[g->player];if(index<0){index=0;selected=shop_catalog_item(v->shop_category,0);}
    backdrop();text(12,6,"装备商店",GOLD);text(136,6,"金币",DIM);number(169,6,p->gold,GOLD);text(256,6,"已暂停 / 关闭后配送",GREEN);
    button(216,0,37,24,"眼 L",0);
    frame(440,0,40,24,EDGE);center(440,5,40,"关闭",INK);
    for(i=0;i<SHOP_CATEGORIES;++i){box(i*48,26,48,23,i==v->shop_category?RGB(46,53,39):PANEL);frame(i*48,26,48,23,i==v->shop_category?GOLD:EDGE);center(i*48,30,48,tabs[i],i==v->shop_category?GOLD:DIM);}
    if(v->shop_category==SHOP_RECOMMENDED){text_fit(12,53,166,hero_names[p->hero_id],GOLD);for(i=0;i<SHOP_RECOMMEND_STAGES;++i)button(184+i*70,50,68,19,shop_catalog_recommend_stage_name(i),i==shop_catalog_recommend_stage());}
    else if(v->shop_category==SHOP_UPGRADES){int base=shop_catalog_upgrade_base();text(12,53,"升级自",DIM);if(base>=0){item_assets_text(base,0,name,sizeof(name));text_fit(60,53,396,name,GOLD);}}
    else if(v->shop_category==5 || v->shop_category==6){text(12,53,"中立材料",DIM);number(65,53,dota_neutral_stones(g,g->player),GOLD);text(110,53,"制作消耗",DIM);number(166,53,dota_neutral_cost(g,g->player),INK);text(216,53,"当前阶级",DIM);number(271,53,dota_neutral_tier(g,g->player),GOLD);text(310,53,dota_neutral_phase(g,g->player)?"选择制作方案":"清理野怪营地收集",GREEN);}
    else courier_panel(g,12,51,456);
    if(!v->shop_details){
        if(!count)center(12,119,456,v->shop_category==SHOP_UPGRADES?"该物品没有后续升级":"此分类暂无物品",DIM);
        for(i=0;i<6;++i){int id=shop_catalog_item(v->shop_category,index/6*6+i),x=12+230*(i%2),y=72+40*(i/2),sel=id==selected;if(id<0)continue;
            if(!dota_items_read(id,&r))continue;
            item_assets_text(id,0,name,sizeof(name));
            box(x,y,224,37,sel?RGB(47,52,39):RGB(20,38,37));frame(x,y,224,37,sel?GOLD:EDGE);art(x+5,y+6,item_assets_item_art(id));text_fit(x+35,y+3,184,name,sel?GOLD:INK);
            if(r.flags&ITEM_F_BUY){text(x+35,y+19,"售价",DIM);number(x+68,y+19,r.cost,GOLD);}else{text(x+35,y+19,r.flags&ITEM_F_NEUTRAL?"中立装备":(r.flags&ITEM_F_ENCHANTMENT?"中立附魔":"战利品 / 奖励"),DIM);if(r.tier)number(x+160,y+19,r.tier,GOLD);}
        }
    }else if(dota_items_read(selected,&r)){
        DotaItemRecipe recipe;int recipe_found=0;
        dota_items_special_display_stats(selected,&r);
        item_assets_text(selected,0,name,sizeof(name));art(12,73,item_assets_item_art(selected));text_fit(42,73,420,name,GOLD);
        text(12,99,"金",DIM);number(30,99,r.cost,GOLD);text(100,99,"蓝",DIM);number(118,99,r.mana_cost,INK);text(185,99,"冷却",DIM);number(215,99,(r.cooldown_ticks+19)/20,INK);text(278,99,r.action?"主动":"被动",GREEN);if(r.cast_range){text(330,99,"距离",DIM);number(362,99,r.cast_range,INK);}
        item_tooltip(selected,&r,description,sizeof(description));tooltip(12,119,456,4,description,v->shop_scroll);
        for(i=0;i<dota_items_recipes_count();++i)if(dota_items_recipe_read(i,&recipe) && recipe.result==selected){int j;recipe_found=1;text(12,180,"合成",DIM);for(j=0;j<recipe.count && j<6;++j)art_small(52+j*35,177,item_assets_item_art(recipe.component[j]));if(dota_items_read(recipe.recipe,&r) && !(r.flags&ITEM_F_AUTO_RECIPE))art_small(52+recipe.count*35,177,item_assets_item_art(recipe.recipe));break;}
        if(!recipe_found)text(12,181,"方向键滚动说明",DIM);
    }
    if(!v->shop_details){button(12,195,68,22,"上一页",1);text(106,198,"页",DIM);number(126,198,index/6+1,GOLD);text(155,198,"/",DIM);number(169,198,count?(count+5)/6:1,INK);text(221,198,"详情 I",GOLD);button(400,195,68,22,"下一页",1);}
    else text(12,200,"上下滚动  I返回  左右翻说明",DIM);
    if(v->shop_category==5 || v->shop_category==6){int phase=dota_neutral_phase(g,g->player);button(140,220,200,26,phase==2?"选择附魔  ENTER":(phase==1?"选择装备  ENTER":"生成制作方案 ENTER"),dota_neutral_can_begin(g,g->player));text(12,225,phase?"独立中立槽":"需要材料与阶级",DIM);}
    else {int buy=dota_courier_can_buy(g,g->player,selected);button(140,220,174,26,"购买 ENTER",buy);button(326,220,142,26,"升级去向 G",0);if(dota_items_purchase_plan(g,g->player,selected,&plan)){text(12,225,"补购",DIM);number(47,225,plan.cost,GOLD);}else text(12,225,"来源见分类",DIM);}
    text_fit(12,252,456,v->shop_category==SHOP_RECOMMENDED?"Q/W分类  A/D出装阶段  G升级  ENTER购买":"Q/W分类  T推荐  G升级  Z~N售出  SHIFT拆解",DIM);
}
static void help(const DotaView *v){
    backdrop();text(16,7,"帮助说明",GOLD);button(432,0,48,26,"返回",0);
    button(16,34,140,26,"操作",v->help_page==0);button(164,34,140,26,"版权介绍",v->help_page==1);
    if(v->help_page==1){
        text(24,73,"作者  HelloClyde",GOLD);
        text(24,95,"离线复刻与 H1 适配；非官方游戏。",INK);
        text(24,121,"Dota 2 原始版权：Valve Corporation",GOLD);
        text(24,141,"地图、模型、动画、图像与音频属于 Valve。",INK);
        text(24,167,"自有代码：GPL-2.0-or-later",DIM);
        text(24,185,"H1 SDK：Apache-2.0",DIM);
        text(24,203,"字形来源：Microsoft YaHei / Consolas",DIM);
        text(24,225,"第三方素材与字形各自保留原有权属。",DIM);
        text(24,241,"本项目未另获字形再分发授权。",DIM);
    }else{
        text(24,76,"方向键 移动 / 瞄准",INK);text(254,76,"QWER 主动技能",INK);
        text(24,97,"J 普攻 / K 停止",INK);text(254,97,"ZXCVBN 装备主动",INK);
        text(24,118,"ASDF 镜头 / SPACE跟随",INK);text(254,118,"T 额外技能 / Y 中立",INK);
        text(24,139,"M 商店 / H 战绩 / L 莲花",INK);text(254,139,"U 技能天赋 / I 换位",INK);
        text(24,160,"SHIFT+QWER 技能加点",INK);text(254,160,"ENTER施放 / ESC取消",INK);
        box(24,184,432,1,EDGE);
        text(24,192,"点小地图切视野；点英雄状态回中。",DIM);
        text(24,212,"长按装备换位；SHIFT+物品给队友；眼同键切换。",DIM);
        text(24,232,"斯拉克E双击自动；物品双击自用；G双击回泉。",DIM);
    }
    text(16,258,"左右 / A D 切页；BACK / ESC 返回",GOLD);
}
#include "match_ui.h"
static void result(const DotaGame *g,const DotaView *v){
    (void)g;match_result_panel(v->result_record,v->result_selected,v->result_detail,v->save_error);
}
void dota_render(const DotaGame *g,const DotaView *v){
    clip_top=0;clip_bottom=271;hud_status_info_visible=0;if(!g || !v)return;
    if(v->screen!=DOTA_SCREEN_PLAY && v->screen!=DOTA_SCREEN_PAUSE && v->screen!=DOTA_SCREEN_RESULT)hud_status_count=0;
    switch(v->screen){
    case DOTA_SCREEN_MENU:menu(g,v);break;
    case DOTA_SCREEN_PAUSE:pause_screen(g,v);break;
    case DOTA_SCREEN_SHOP:shop(g,v);break;
    case DOTA_SCREEN_RESULT:result(g,v);break;
    case DOTA_SCREEN_HELP:help(v);break;
    case DOTA_SCREEN_SKILLS:progression_screen(g,v);break;
    case DOTA_SCREEN_HISTORY:match_history_panel(v->history,v->history_page,v->history_selected,v->history_mode,v->result_selected,v->save_error);break;
    default:battlefield(g,v);break;
    }
}
