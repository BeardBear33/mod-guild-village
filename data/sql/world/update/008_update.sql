-- ########################################################
-- Guild Village – Quest Board GameObject (acore_world)
-- ########################################################

INSERT IGNORE INTO `gameobject_template`
(`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`,
 `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`,
 `Data8`, `Data9`, `Data10`, `Data11`, `Data12`, `Data13`, `Data14`, `Data15`,
 `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`, `Data23`,
 `AIName`, `ScriptName`, `VerifiedBuild`)
VALUES
(990204, 2, 3053, 'Guild Quest Board', '', '', '', 1,
 57, 7826, 4, 8062, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0, 0, 0,
 '', 'go_gv_quests', 12340);
