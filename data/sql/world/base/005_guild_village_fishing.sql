-- Cleanup
DELETE FROM `fishing_loot_template` WHERE `Entry` = 268;
DELETE FROM `skill_fishing_base_level` WHERE `entry` = 268;

-- Loot rows
INSERT IGNORE INTO `fishing_loot_template`
(`Entry`,`Item`,`Reference`,`Chance`,`QuestRequired`,`LootMode`,`GroupId`,`MinCount`,`MaxCount`,`Comment`)
VALUES
(268,1,11107,100,0,1,0,1,1,'(ReferenceTable)'),
(268,3671,0,4.5,0,1,1,1,1,'Lifeless Skull'),
(268,4875,0,5,0,1,1,1,1,'Slimy Bone'),
(268,11018,11018,100,0,1,1,1,1,'(ReferenceTable)'),
(268,11019,11019,100,0,1,0,1,1,'(ReferenceTable)'),
(268,11020,11020,100,0,1,0,1,1,'(ReferenceTable)'),
(268,11021,11021,100,0,1,0,1,1,'(ReferenceTable)'),
(268,11022,11022,100,0,1,0,1,1,'(ReferenceTable)'),
(268,25447,0,4.5,0,1,1,1,1,'Broken Skull'),
(268,36794,0,5.4,0,1,1,1,1,'Scoured Fishbones'),
(268,37705,0,1,0,1,1,1,1,'Crystallized Water'),
(268,41808,0,58,0,1,1,1,1,'Bonescale Snapper'),
(268,43572,0,20.5,0,1,1,1,1,'Magic Eater'),
(268,44475,0,1.1,0,1,1,1,1,'Reinforced Crate'),
(268,45902,0,20,1,1,0,1,1,'Phantom Ghostfish'),
(268,45904,0,50,1,1,0,1,1,'Terrorfish');

-- Base skill
INSERT IGNORE INTO `skill_fishing_base_level` (`entry`,`skill`)
VALUES
(268,400);
