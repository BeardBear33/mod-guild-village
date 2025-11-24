-- spell_script_names: remove old script names
DELETE FROM `spell_script_names`
WHERE `ScriptName` IN (
    'spell_thalgron_flaming_cinder',
    'spell_thalgron_meteor_fists_aura',
    'spell_thranok_rock_shards',
    'spell_voltrix_gravity_bomb',
    'spell_voltrix_searing_light',
	'spell_voltrix_tympanic_tantrum'
);