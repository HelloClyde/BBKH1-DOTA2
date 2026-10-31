"""Explicit original-source pose mapping for all sixteen H1 hero skills.

The H1 rules make the three original passive E abilities active. Their body
feedback therefore uses a documented original attack/buff/cast pose rather than
pretending that Source 2 supplied a passive spell-cast animation.
"""

SKILLS = {
    'axe': [
        {'skill':0,'name':'狂战士之吼','source_clip':'berserkers_call','mode':'one_shot'},
        {'skill':1,'name':'战斗饥渴','source_clip':'battle_hunger','mode':'one_shot'},
        {'skill':2,'name':'反击螺旋','source_clip':'counter_helix_anim','mode':'one_shot',
         'adaptation':'Original reactive Counter Helix spin pose, triggered by this edition\'s active E.'},
        {'skill':3,'name':'淘汰之刃','source_clip':'culling_blade','mode':'one_shot'},
    ],
    'lina': [
        {'skill':0,'name':'龙破斩','source_clip':'dragon_slave','mode':'one_shot'},
        {'skill':1,'name':'光击阵','source_clip':'light_strike_array_rhand','mode':'one_shot'},
        {'skill':2,'name':'炽魂','source_clip':'cast_flame_cloak','mode':'one_shot',
         'adaptation':'Original Flame Cloak self-buff pose, adapted to this edition\'s active Fiery Soul.'},
        {'skill':3,'name':'神灭斩','source_clip':'laguna_blade','mode':'one_shot'},
    ],
    'juggernaut': [
        {'skill':0,'name':'剑刃风暴','source_clip':'attack_spin','mode':'persistent_loop','frames':16,'start_clip_id':9},
        {'skill':1,'name':'治疗守卫','source_clip':'Healing_ward','mode':'one_shot'},
        {'skill':2,'name':'剑舞','source_clip':'attack_crit','mode':'one_shot',
         'adaptation':'Original Blade Dance critical-attack pose, adapted to this edition\'s active damage buff.'},
        {'skill':3,'name':'无敌斩','source_clip':'omni','mode':'action_loop','start_clip_id':10},
    ],
    'crystal_maiden': [
        {'skill':0,'name':'冰霜新星','source_clip':'nova','mode':'one_shot'},
        {'skill':1,'name':'冰封禁制','source_clip':'frostbite','mode':'one_shot'},
        {'skill':2,'name':'奥术光环','source_clip':'cm_cast_crystal_clone','mode':'one_shot',
         'adaptation':'Original Crystal Clone staff-cast pose, adapted to this edition\'s active mana restoration aura.'},
        {'skill':3,'name':'极寒领域','source_clip':'freezing_field','mode':'channel_start',
         'source_start':0.0,'source_end':0.6,'loop_clip_id':9,'release_clip_id':10},
    ],
}

EXTRA_PHASES = {
    'juggernaut': [
        {'clip_id':9,'skill':0,'phase':'start','source_clip':'attack_spin_cast','mode':'one_shot'},
        {'clip_id':10,'skill':3,'phase':'start','source_clip':'attack_omni_cast','mode':'one_shot'},
    ],
    'crystal_maiden': [
        {'clip_id':9,'skill':3,'phase':'channel','source_clip':'freezing_field','mode':'persistent_loop',
         'source_start':0.6,'source_end':3.9,'frames':16},
        {'clip_id':10,'skill':3,'phase':'release','source_clip':'freezing_field','mode':'one_shot',
         'source_start':3.9},
    ],
}


def motion_specs(slug, legacy_clips):
    """Return fixed runtime clip IDs without changing the legacy five slots."""
    out=[{'clip_id':i,'phase':'legacy','source_clip':clip,
          'mode':'persistent_loop' if i in (0,1) else 'one_shot'}
         for i,clip in enumerate(legacy_clips)]
    for skill in SKILLS.get(slug,[]):
        out.append(dict(skill,clip_id=5+skill['skill'],phase='cast'))
    out.extend(dict(spec) for spec in EXTRA_PHASES.get(slug,[]))
    return out


def source_window(spec, duration):
    start=float(spec.get('source_start',0));end=float(spec.get('source_end',duration))
    if not 0<=start<end<=duration+1e-5:raise ValueError('Invalid original animation sampling window')
    return start,min(end,duration)
