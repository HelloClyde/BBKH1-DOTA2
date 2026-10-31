/* Original baked particle images and meshes follow the real20Hz rules state. */
static int roster_fx_visible(const DotaGame *g,int x,int y){return extra_fx_visible(x,y)&&dota_sight_position_visible(g,g->player,x,y);}
static void roster_line(uint16_t *pixels,const BSCamera *camera,int model,int clip,int x,int y,int xx,int yy,unsigned phase){
    DotaSkillEvent e;clear_bytes(&e,sizeof(e));e.origin_x=x;e.origin_y=y;e.target_x=xx;e.target_y=yy;
    original_beam(pixels,camera,&e,phase,model,clip,1);
}
static void render_roster_effects(uint16_t *pixels,const DotaGame *g,const BSCamera *camera){
    int i,j;unsigned tick=(unsigned)g->tick;
    if(pack.version<12u)return;
    for(i=0;i<DOTA_HERO_COUNT;++i){const DotaUnit *u=&g->units[i];const DotaRosterHero *r=&g->roster.hero[i];
        if(!u->active||!dota_roster_is_hero(u)||r->generation!=u->generation)continue;
        if(u->hp>0&&roster_fx_visible(g,u->x,u->y)){
            if(u->hero_id==6){
                if(r->rot_on)skill_fx(pixels,camera,98,1,0,tick,1,u->x,u->y,(dota_roster_radius(g,i,1)+3)/4);
                if(r->flesh_until>g->tick)skill_fx(pixels,camera,99,1,0,tick,1,u->x,u->y,0);
                if(r->channel_until>g->tick&&(unsigned)r->channel_target<DOTA_MAX_UNITS){const DotaUnit *v=&g->units[r->channel_target];if(v->generation==r->channel_generation&&v->active&&v->hp>0&&extra_fx_visible(v->x,v->y))skill_fx(pixels,camera,100,1,0,tick,1,v->x,v->y,0);}
            }else if(u->hero_id==7){
                if(r->aim_until>g->tick)skill_fx(pixels,camera,103,1,0,tick,1,u->x,u->y,0);
                if(r->cast_at>g->tick&&r->cast_skill==3&&(unsigned)r->cast_target<DOTA_MAX_UNITS){const DotaUnit *v=&g->units[r->cast_target];if(v->active&&v->generation==r->cast_generation&&extra_fx_visible(v->x,v->y))skill_fx(pixels,camera,104,0,0,tick,1,v->x,v->y,0);}
            }else if(u->hero_id==8){if(r->gods_until>g->tick)skill_fx(pixels,camera,108,1,0,tick,1,u->x,u->y,0);}
            else if(r->shell_until>g->tick)skill_fx(pixels,camera,110,1,0,tick,1,u->x,u->y,0);
        }
        if(u->hero_id==9){const SkillTrack *cast=&skill_tracks[i][3];int start=r->ravage_at,age;
            if(!start&&cast->valid&&cast->event.kind==DOTA_SKILL_EVENT_CAST&&cast->event.step!=-4)start=cast->event.started_tick+6;
            age=g->tick-start;
            if(start>0&&age>=0&&age<46&&roster_fx_visible(g,r->ravage_x,r->ravage_y)){
                int wave=clamp(age*725/20,0,r->ravage_radius)/4;
                if(age<36)skill_fx(pixels,camera,112,1,0,(unsigned)age,1,r->ravage_x,r->ravage_y,wave);
                /* A sparse sequence of expanding rings of original animated
                 * tentacles. Their births follow the725 Source-unit wave. */
                for(j=0;j<9;++j){int born=j*4+1,local=age-born,k,radius=born*725/80;if(local<0||local>=10||radius*4>r->ravage_radius)continue;
                    for(k=0;k<8;++k){int dir=(k*2+j)&15,x=r->ravage_x-radius*sine[dir]/256,y=r->ravage_y+radius*sine[(dir+4)&15]/256;
                        if(extra_fx_visible(x,y))skill_fx(pixels,camera,119,0,0,(unsigned)local,0,x,y,0);
                    }
                }
            }
        }
    }
    for(i=0;i<DOTA_ROSTER_FIELDS;++i){const DotaRosterField *f=&g->roster.field[i];
        if(!(g->roster.field_mask&(1u<<i))||g->tick<f->starts_at||g->tick>f->until||!roster_fx_visible(g,f->x,f->y))continue;
        skill_fx(pixels,camera,101,1,0,(unsigned)(g->tick-f->starts_at),1,f->x,f->y,(f->radius+3)/4);
    }
    for(i=0;i<DOTA_ROSTER_SPELLS;++i){const DotaRosterSpell *s=&g->roster.spell[i];int x,y,dir,model;
        if(!s->kind||s->delay>0||(unsigned)s->source>=DOTA_MAX_UNITS)continue;
        x=s->x_q16/DOTA_MOVE_ONE;y=s->y_q16/DOTA_MOVE_ONE;if(!roster_fx_visible(g,x,y))continue;
        dir=direction_yaw(s->to_x-x,s->to_y-y,0);
        if(s->kind==RS_HOOK){const DotaUnit *u=&g->units[s->source];roster_line(pixels,camera,97,1,u->x,u->y,x,y,tick);extra_fx_sprite(pixels,camera,117,1,dir,tick,1,x,y,bs_ground_height(x,y)+40);}
        else if(s->kind==RS_STORM)extra_fx_sprite(pixels,camera,118,1,dir,tick,1,x,y,bs_ground_height(x,y)+40);
        else if(s->kind==RS_ASSASSINATE){int dx=s->to_x-s->origin_x,dy=s->to_y-s->origin_y,len=(int)integer_sqrt((unsigned)(dx*dx+dy*dy));if(len)roster_line(pixels,camera,104,1,x-dx*24/len,y-dy*24/len,x,y,tick);}
        else if(s->kind==RS_GUSH||s->kind==RS_GUSH_WAVE){roster_line(pixels,camera,109,1,x+sine[dir]*30/256,y-sine[(dir+4)&15]*30/256,x,y,tick);}
        else{model=s->kind==RS_GRENADE?113:114;extra_fx_sprite(pixels,camera,model,1,dir,tick,1,x,y,bs_ground_height(x,y)+30);}
    }
    for(i=0;i<DOTA_ROSTER_MASK_WORDS;++i){uint32_t mask=g->roster.target_active[i];while(mask){unsigned bit=(unsigned)__builtin_ctz(mask);int unit=i*32+(int)bit;const DotaRosterTarget *t=&g->roster.target[unit];const DotaUnit *u=&g->units[unit];mask&=mask-1u;
        if(unit>=DOTA_MAX_UNITS||!u->active||u->hp<=0||u->generation!=t->generation||!roster_fx_visible(g,u->x,u->y))continue;
        if(t->headshot_ticks)skill_fx(pixels,camera,102,1,0,tick,1,u->x,u->y,0);
        if(t->warcry_ticks)skill_fx(pixels,camera,107,1,0,tick,1,u->x,u->y,0);
        if(t->gush_ticks)skill_fx(pixels,camera,109,2,0,tick,1,u->x,u->y,0);
        if(t->tether_ticks&&(unsigned)t->tether_anchor<DOTA_MAX_UNITS){const DotaUnit *a=&g->units[t->tether_anchor];int dx=u->x-a->x,dy=u->y-a->y,length=(int)integer_sqrt((unsigned)(dx*dx+dy*dy)),at;if(a->active&&a->hp>0&&extra_fx_visible(a->x,a->y))for(at=8;at<length;at+=12){int x=a->x+dx*at/length,y=a->y+dy*at/length;extra_fx_sprite(pixels,camera,120,0,direction_yaw(dx,dy,0),0,0,x,y,bs_ground_height(x,y)+8);}}
    }}
    {unsigned count=g->skill_event_count,k;if(count>DOTA_MAX_SKILL_EVENTS)count=DOTA_MAX_SKILL_EVENTS;
        for(k=0;k<count;++k){const DotaSkillEvent *e=dota_get_skill_event(g,g->skill_event_serial-count+1u+k);int age,model,clip,dir,x,y;const ClipGroup *group;
            if(!e||e->hero_id<6||e->hero_id>9||!event_caster_valid(g,e))continue;
            age=g->tick-e->started_tick;if(age<0)continue;
            model=97+(e->hero_id-6)*4+e->skill;if(e->step==-4)model=e->hero_id==7?113:114;
            if(e->kind==DOTA_SKILL_EVENT_CAST){clip=0;x=e->origin_x;y=e->origin_y;if(e->hero_id==7&&e->skill==3)continue;}
            else if(e->kind==DOTA_SKILL_EVENT_STRIKE||e->kind==DOTA_SKILL_EVENT_PASSIVE){clip=2;x=e->target_x;y=e->target_y;}else continue;
            if(e->kind==DOTA_SKILL_EVENT_PASSIVE&&e->hero_id==8&&e->skill==1){clip=0;x=e->origin_x;y=e->origin_y;}
            if(!roster_fx_visible(g,x,y))continue;
            dir=direction_yaw(e->target_x-e->origin_x,e->target_y-e->origin_y,0);group=find_group(model,clip,dir,0);
            if(group&&(unsigned)age<group->ticks)skill_fx(pixels,camera,model,clip,dir,(unsigned)age,0,x,y,0);
        }
    }
}
