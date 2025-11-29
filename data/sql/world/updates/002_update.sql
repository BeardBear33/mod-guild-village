DELETE FROM `spell_script_names`
WHERE `ScriptName` IN (
  'spell_thranok_rock_shards',
  'spell_thalgron_flaming_cinder'
);

-- Thranok: Rock Shards SpellScript patří na 58941 (ne 58678)
INSERT IGNORE INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
  (58941, 'spell_thranok_rock_shards');
