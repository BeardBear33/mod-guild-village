-- ########################################################
-- Guild Village – Quest Board GameObject (acore_world)
-- ########################################################

INSERT INTO `gameobject_template` (
  `entry`,`type`,`displayId`,`name`,`IconName`,`castBarCaption`,`unk1`,`size`,
  `Data0`,`Data1`,`Data2`,`Data3`,`Data4`,`Data5`,`Data6`,`Data7`,
  `Data8`,`Data9`,`Data10`,`Data11`,`Data12`,`Data13`,`Data14`,`Data15`,
  `Data16`,`Data17`,`Data18`,`Data19`,`Data20`,`Data21`,`Data22`,`Data23`,
  `AIName`,`ScriptName`,`VerifiedBuild`
) VALUES (
  990204, 2, 3053, 'Guild Quest Board', '', '', '', 1,
  57, 7826, 4, 8062, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  '', 'go_gv_quests', 12340
)
ON DUPLICATE KEY UPDATE
  `type`=VALUES(`type`),
  `displayId`=VALUES(`displayId`),
  `name`=VALUES(`name`),
  `IconName`=VALUES(`IconName`),
  `castBarCaption`=VALUES(`castBarCaption`),
  `unk1`=VALUES(`unk1`),
  `size`=VALUES(`size`),
  `Data0`=VALUES(`Data0`), `Data1`=VALUES(`Data1`), `Data2`=VALUES(`Data2`), `Data3`=VALUES(`Data3`),
  `Data4`=VALUES(`Data4`), `Data5`=VALUES(`Data5`), `Data6`=VALUES(`Data6`), `Data7`=VALUES(`Data7`),
  `Data8`=VALUES(`Data8`), `Data9`=VALUES(`Data9`), `Data10`=VALUES(`Data10`), `Data11`=VALUES(`Data11`),
  `Data12`=VALUES(`Data12`), `Data13`=VALUES(`Data13`), `Data14`=VALUES(`Data14`), `Data15`=VALUES(`Data15`),
  `Data16`=VALUES(`Data16`), `Data17`=VALUES(`Data17`), `Data18`=VALUES(`Data18`), `Data19`=VALUES(`Data19`),
  `Data20`=VALUES(`Data20`), `Data21`=VALUES(`Data21`), `Data22`=VALUES(`Data22`), `Data23`=VALUES(`Data23`),
  `AIName`=VALUES(`AIName`),
  `ScriptName`=VALUES(`ScriptName`),
  `VerifiedBuild`=VALUES(`VerifiedBuild`);

