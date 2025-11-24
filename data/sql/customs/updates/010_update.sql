-- ================================
-- Guild Village Update: Upgrade Catalog + Teleporter Menu
-- ================================

-- ---- customs.gv_upgrade_catalog.expansion_key_required ----
SET @sql := IF (
  (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
     WHERE TABLE_SCHEMA='customs'
       AND TABLE_NAME='gv_upgrade_catalog'
       AND COLUMN_NAME='expansion_key_required') = 0,
  'ALTER TABLE `customs`.`gv_upgrade_catalog` ADD COLUMN `expansion_key_required` VARCHAR(64) NULL AFTER `expansion_key`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- ---- customs.gv_upgrade_catalog.catalog_npc ----
SET @sql := IF (
  (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
     WHERE TABLE_SCHEMA='customs'
       AND TABLE_NAME='gv_upgrade_catalog'
       AND COLUMN_NAME='catalog_npc') = 0,
  'ALTER TABLE `customs`.`gv_upgrade_catalog` ADD COLUMN `catalog_npc` TINYINT UNSIGNED NULL AFTER `sort_order`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- ---- customs.gv_upgrade_catalog.enabled ----
SET @sql := IF (
  (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
     WHERE TABLE_SCHEMA='customs'
       AND TABLE_NAME='gv_upgrade_catalog'
       AND COLUMN_NAME='enabled') = 0,
  'ALTER TABLE `customs`.`gv_upgrade_catalog` ADD COLUMN `enabled` TINYINT(1) NULL DEFAULT 1 AFTER `catalog_npc`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Dosazení 1 do nového/sloupcově NULL pole
UPDATE `customs`.`gv_upgrade_catalog`
SET `catalog_npc` = 1
WHERE `catalog_npc` IS NULL;


-- ---- customs.gv_teleport_menu.expansion_required ----
SET @sql := IF (
  (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
     WHERE TABLE_SCHEMA='customs'
       AND TABLE_NAME='gv_teleport_menu'
       AND COLUMN_NAME='expansion_required') = 0,
  'ALTER TABLE `customs`.`gv_teleport_menu` ADD COLUMN `expansion_required` VARCHAR(64) NULL DEFAULT NULL',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- ---- customs.gv_upgrade_catalog seed/update ----
INSERT INTO `customs`.`gv_upgrade_catalog`
(`id`,`category`,`expansion_key`,`expansion_key_required`,
 `label_cs`,`label_en`,`info_cs`,`info_en`,
 `cost_material1`,`cost_material2`,`cost_material3`,`cost_material4`,
 `sort_order`,`catalog_npc`,`enabled`)
VALUES
(1,'trainers','trainer_paladin',NULL,'Trenér: Paladin','Trainer: Paladin',NULL,NULL,16,0,24,0,1,1,1),
(2,'trainers','trainer_hunter',NULL,'Trenér: Hunter','Trainer: Hunter',NULL,NULL,24,8,8,0,1,1,1),
(3,'trainers','trainer_mage',NULL,'Trenér: Mage','Trainer: Mage',NULL,NULL,16,0,0,24,1,1,1),
(4,'trainers','trainer_priest',NULL,'Trenér: Priest','Trainer: Priest',NULL,NULL,36,0,0,24,1,1,1),
(5,'trainers','trainer_druid',NULL,'Trenér: Druid','Trainer: Druid',NULL,NULL,12,8,10,10,1,1,1),
(6,'trainers','trainer_shaman',NULL,'Trenér: Shaman','Trainer: Shaman',NULL,NULL,14,6,0,20,1,1,1),
(7,'trainers','trainer_warlock',NULL,'Trenér: Warlock','Trainer: Warlock',NULL,NULL,16,0,0,24,1,1,1),
(8,'trainers','trainer_rogue',NULL,'Trenér: Rogue','Trainer: Rogue',NULL,NULL,16,0,24,0,1,1,1),
(9,'trainers','trainer_warrior',NULL,'Trenér: Warrior','Trainer: Warrior',NULL,NULL,16,0,24,0,1,1,1),
(10,'trainers','trainer_dk',NULL,'Trenér: Death Knight','Trainer: Death Knight',NULL,NULL,16,0,24,0,1,1,1),
(11,'professions','prof_alchemy','prof_herbalism','Profese: Alchemy','Profession: Alchemy',NULL,NULL,8,0,0,12,1,1,1),
(12,'professions','prof_engineering','prof_mining','Profese: Engineering','Profession: Engineering',NULL,NULL,0,4,12,4,1,1,1),
(13,'professions','prof_inscription','prof_herbalism','Profese: Inscription','Profession: Inscription',NULL,NULL,8,0,0,12,1,1,1),
(14,'professions','prof_blacksmithing','prof_mining','Profese: Blacksmithing','Profession: Blacksmithing',NULL,NULL,4,0,12,0,1,1,1),
(15,'professions','prof_jewelcrafting','prof_mining','Profese: Jewelcrafting','Profession: Jewelcrafting',NULL,NULL,0,12,0,8,1,1,1),
(16,'professions','prof_tailoring',NULL,'Profese: Tailoring','Profession: Tailoring',NULL,NULL,10,0,6,4,1,1,1),
(17,'professions','prof_leatherworking','prof_skinning','Profese: Leatherworking','Profession: Leatherworking',NULL,NULL,10,10,0,0,1,1,1),
(18,'professions','prof_enchanting',NULL,'Profese: Enchanting','Profession: Enchanting',NULL,NULL,4,0,0,16,1,1,1),
(19,'professions','prof_skinning',NULL,'Profese: Skinning','Profession: Skinning',NULL,NULL,0,10,10,0,1,1,1),
(20,'professions','prof_herbalism',NULL,'Profese: Herbalism','Profession: Herbalism',NULL,NULL,20,0,10,0,1,1,1),
(21,'professions','prof_mining',NULL,'Profese: Mining','Profession: Mining',NULL,NULL,0,3,7,0,1,1,1),
(22,'professions','prof_fishing',NULL,'Profese: Fishing','Profession: Fishing',NULL,NULL,20,0,0,0,1,1,1),
(23,'professions','prof_firstaid',NULL,'Profese: First Aid','Profession: First Aid',NULL,NULL,0,0,0,20,1,1,1),
(24,'professions','prof_cooking',NULL,'Profese: Cooking','Profese: Cooking',NULL,NULL,20,0,0,0,1,1,1),
(25,'portal','portal_stormwind',NULL,'Portál: Stormwind','Portal: Stormwind',NULL,NULL,9,18,27,50,1,1,1),
(26,'portal','portal_ironforge','portal_stormwind','Portál: Ironforge','Portal: Ironforge',NULL,NULL,9,18,27,50,1,1,1),
(27,'portal','portal_darnassus','portal_stormwind','Portál: Darnassus','Portal: Darnassus',NULL,NULL,9,18,27,50,1,1,1),
(28,'portal','portal_exodar','portal_stormwind','Portál: Exodar','Portal: Exodar',NULL,NULL,9,18,27,50,1,1,1),
(29,'portal','portal_orgrimmar',NULL,'Portál: Orgrimmar','Portal: Orgrimmar',NULL,NULL,9,18,27,50,1,1,1),
(30,'portal','portal_undercity','portal_orgrimmar','Portál: Undercity','Portal: Undercity',NULL,NULL,9,18,27,50,1,1,1),
(31,'portal','portal_thunderbluff','portal_orgrimmar','Portál: Thunder Bluff','Portal: Thunder Bluff',NULL,NULL,9,18,27,50,1,1,1),
(32,'portal','portal_silvermoon','portal_orgrimmar','Portál: Silvermoon','Portal: Silvermoon',NULL,NULL,9,18,27,50,1,1,1),
(33,'portal','portal_shattrath',NULL,'Portál: Shattrath','Portal: Shattrath',NULL,NULL,11,23,39,80,1,1,1),
(34,'portal','portal_dalaran','portal_shattrath','Portál: Dalaran','Portal: Dalaran',NULL,NULL,18,36,54,100,1,1,1),
(35,'objects','guild_bank','personal_bank','Guild banka','Guild Bank',NULL,NULL,0,20,50,10,1,1,1),
(36,'others','personal_bank',NULL,'Osobní banka','Personal Bank',NULL,NULL,0,10,0,6,1,1,1),
(37,'others','auction','mailbox','Aukce','Auction',NULL,NULL,0,0,6,4,1,1,1),
(38,'objects','mailbox',NULL,'Poštovní schránka','Mailbox',NULL,NULL,2,0,4,0,1,1,1),
(39,'vendor','vendor_fooddrink',NULL,'Vendor: Jídlo&Pití','Vendor: Food&Drink',NULL,NULL,8,0,16,0,1,1,1),
(40,'vendor','vendor_reagent',NULL,'Vendor: Reagent','Vendor: Reagent',NULL,NULL,0,6,0,18,1,1,1),
(41,'trainers','trainer_riding',NULL,'Trenér: Jezdectví','Trainer: Riding',NULL,NULL,0,8,12,0,1,1,1),
(42,'objects','training_dummy',NULL,'Trénovací panák','Training dummy',NULL,NULL,16,4,0,0,1,1,1),
(43,'vendor','vendor_heirloom',NULL,'Vendor: Heirloom','Vendor: Heirloom',NULL,NULL,16,6,0,0,1,1,1),
(44,'vendor','vendor_heroism',NULL,'Vendor: Heroism emblém','Vendor: Emblem of Heroism',NULL,NULL,20,12,8,12,1,1,1),
(45,'vendor','vendor_valor',NULL,'Vendor: Valor emblém','Vendor: Emblem of Valor',NULL,NULL,24,18,16,24,1,1,1),
(46,'vendor','vendor_triumph',NULL,'Vendor: Triumph emblém','Vendor: Emblem of Triumph',NULL,NULL,28,24,24,36,1,1,1),
(47,'vendor','vendor_conquest',NULL,'Vendor: Conquest emblem','Vendor: Emblem of Conquest',NULL,NULL,32,30,24,48,1,1,1),
(48,'vendor','vendor_frost',NULL,'Vendor: Frost emblém','Vendor: Emblem of Frost',NULL,NULL,36,36,40,60,1,1,1),
(49,'others','guards',NULL,'Stráže vesnice','Village Guards','Pridává guardy kterí vám reknou cestu pokud neco hledáte.','Adds guards who will tell you the way if you’re looking for something.',40,42,48,72,1,1,1),
(50,'objects','ore_pack1','prof_mining','Balík rudy I','Ore Bundle I','Spawnovací sada minerálů v oblasti vesnice.','A spawning set of minerals in the village area.',0,120,80,0,1,1,1),
(51,'objects','ore_pack2','ore_pack1','Balík rudy II','Ore Bundle II','Spawnovací sada minerálů v oblasti vesnice.','A spawning set of minerals in the village area.',0,160,120,0,1,1,1),
(52,'objects','ore_pack3','ore_pack2','Balík rudy III','Ore Bundle III','Spawnovací sada minerálů v oblasti vesnice.','A spawning set of minerals in the village area.',0,200,160,0,1,1,1),
(53,'objects','herb_pack1','prof_herbalism','Balík kytek I','Herb Pack I','Spawnovací sada kytek v oblasti vesnice.','A set of herb spawns in the village area.',120,0,0,80,1,1,1),
(54,'objects','herb_pack2','herb_pack1','Balík kytek II','Herb Pack II','Spawnovací sada kytek v oblasti vesnice.','A set of herb spawns in the village area.',160,0,0,120,1,1,1),
(55,'objects','herb_pack3','herb_pack2','Balík kytek III','Herb Pack III','Spawnovací sada kytek v oblasti vesnice.','A set of herb spawns in the village area.',20,0,0,160,1,1,1),
(56,'others','monster_pack1','prof_skinning','Balík monster I','Monster Pack I','Spawnovací sada monster ze kterých padá material.','A set of monsters that drop materials.',90,80,50,120,1,1,1),
(57,'others','monster_pack2','monster_pack1','Balík monster II','Monster Pack II','Spawnovací sada monster ze kterých padá material.','A set of monsters that drop materials.',130,120,90,160,1,1,1),
(58,'others','monster_pack3','monster_pack2','Balík monster III','Monster Pack III','Spawnovací sada monster ze kterých padá material.','A set of monsters that drop materials.',170,160,130,200,1,1,1),
(59,'objects','teleporter','guards','Teleportér','Teleportér','Pridává teleporty a odemyká expedice','Adds a teleport and unlocks a expedition base.',50,30,190,360,1,1,1),
(60,'objects','stone_path',NULL,'Kamená cesta','Stone Path','Pouze dekorativní prvek','Decorative element only.',0,90,0,0,1,1,1),
(61,'objects','outdoor_lamp',NULL,'Venkovní lampy','Outdoor Lamps','Pouze dekorativní prvek','Decorative element only.',15,30,20,50,1,1,1),
(62,'objects','indoor_lamp',NULL,'Lampy (Interiér)','Inside Lamps','Pouze dekorativní prvek','Decorative element only.',15,30,20,50,1,1,1),
(63,'others','material_production','teleporter','Výrobní Jednotka','Production Unit','Spawnuje NPC které vyrábí material pro vaši vesnici.','Spawns an NPC that produces materials for your village.',200,200,200,200,1,1,1),
(64,'vendor','bg_arena',NULL,'Vendor: PvP','Vendor: PvP','Pridává npc pro nakup PvP vybaveni','Adds an NPC for purchasing PvP gear.',0,0,18,24,1,1,1),
(65,'others','expedition','teleporter','Expedice','Expedition','Pridává teleport do tábora s expedicí. Umožní posílat hrdiny na výpravy.','Adds a teleport to the expedition camp. Allows you to send heroes on expeditions.',40,25,150,100,1,1,1),
(66,'objects','exp_mailbox','expedition','Poštovní schránka','Mailbox','', '',0,0,6,4,1,2,1)
AS new
ON DUPLICATE KEY UPDATE
 `category`=new.`category`,
 `expansion_key`=new.`expansion_key`,
 `expansion_key_required`=new.`expansion_key_required`,
 `label_cs`=new.`label_cs`,
 `label_en`=new.`label_en`,
 `info_cs`=new.`info_cs`,
 `info_en`=new.`info_en`,
 `cost_material1`=new.`cost_material1`,
 `cost_material2`=new.`cost_material2`,
 `cost_material3`=new.`cost_material3`,
 `cost_material4`=new.`cost_material4`,
 `sort_order`=new.`sort_order`,
 `catalog_npc`=new.`catalog_npc`,
 `enabled`=new.`enabled`;

-- ---- customs.gv_expansion_creatures seed/update ----
INSERT INTO `customs`.`gv_expansion_creatures`
(`id`, `expansion_key`, `entry`, `map`,
 `position_x`, `position_y`, `position_z`, `orientation`,
 `spawntimesecs`, `spawndist`, `movementtype`, `faction`)
VALUES
(300, 'expedition', 987447, 37, 127.442, 976.121, 295.072, 1.16472, 300, 0, 0, 0),
(301, 'expedition', 987446, 37, 109.794, 1006.94, 295.768, 0.300778, 300, 0, 0, 0),
(302, 'expedition', 987445, 37, 104.098, 1035.39, 297.059, 5.72787, 300, 0, 0, 0)
AS new
ON DUPLICATE KEY UPDATE
 `expansion_key` = new.`expansion_key`,
 `entry` = new.`entry`,
 `map` = new.`map`,
 `position_x` = new.`position_x`,
 `position_y` = new.`position_y`,
 `position_z` = new.`position_z`,
 `orientation` = new.`orientation`,
 `spawntimesecs` = new.`spawntimesecs`,
 `spawndist` = new.`spawndist`,
 `movementtype` = new.`movementtype`,
 `faction` = new.`faction`;

-- ---- customs.gv_expansion_gameobjects seed/update ----
INSERT INTO `customs`.`gv_expansion_gameobjects`
(`id`, `expansion_key`, `entry`, `map`,
 `position_x`, `position_y`, `position_z`, `orientation`,
 `rotation0`, `rotation1`, `rotation2`, `rotation3`,
 `spawntimesecs`, `faction`)
VALUES
(719, 'expedition', 990203, 37, 113.003, 990.629, 296.39, -0.339317, 0, 0, 0, 0, 120, 0),
(720, 'exp_mailbox', 191955, 37, 112.675, 1024.16, 296.214, 5.5119, 0, 0, 0, 1, 0, 0)
AS new
ON DUPLICATE KEY UPDATE
 `expansion_key` = new.`expansion_key`,
 `entry` = new.`entry`,
 `map` = new.`map`,
 `position_x` = new.`position_x`,
 `position_y` = new.`position_y`,
 `position_z` = new.`position_z`,
 `orientation` = new.`orientation`,
 `rotation0` = new.`rotation0`,
 `rotation1` = new.`rotation1`,
 `rotation2` = new.`rotation2`,
 `rotation3` = new.`rotation3`,
 `spawntimesecs` = new.`spawntimesecs`,
 `faction` = new.`faction`;

-- ---- customs.gv_teleport_menu seed/update ----
INSERT INTO `customs`.`gv_teleport_menu`
(`id`,`teleporter_entry`,`label_cs`,`label_en`,
 `x`,`y`,`z`,`o`,`sort_index`,`expansion_required`)
VALUES
(1, 990203, 'Do vesnice', 'To the Village', 1063.29, 271.808, 336.837, 4.39102, 10, 'teleporter'),
(2, 990203, 'Kovárna mistra oceli', 'Forge of the Steel Master', 1051.56, 28.2456, 315.229, 4.69428, 20, 'prof_mining'),
(3, 990203, 'Dílna mistra kožešin', 'Workshop of the Fur Master', 738.18, 170.549, 274.603, 5.29509, 30, 'prof_skinning'),
(4, 990203, 'Zahrada mistra bylin', 'Garden of the Herbal Master', 917.491, 348.518, 270.731, 1.68697, 40, 'prof_herbalism'),
(5, 990203, 'Základna Expedic', 'Expedition Base', 113.003, 990.629, 297.25, 0.446081, 50, 'expedition'),
(6, 990203, 'Thranok the Unyielding (Boss)', 'Thranok the Unyielding (Boss)', 399.661, -154.433, 266.946, 4.66128, 60, NULL),
(7, 990203, 'Thalor the Lifebinder (Boss)', 'Thalor the Lifebinder (Boss)', 180.379, 2.1369, 238.02, 5.39172, 70, NULL),
(8, 990203, 'Voltrix the Unbound (Boss)', 'Voltrix the Unbound (Boss)', -47.1828, 293.399, 290.626, 2.509, 80, NULL),
(9, 990203, 'Thalgron the Earthshaker (Boss)', 'Thalgron the Earthshaker (Boss)', 164.313, 584.39, 271.354, 2.82002, 90, NULL)
AS new
ON DUPLICATE KEY UPDATE
 `teleporter_entry` = new.`teleporter_entry`,
 `label_cs` = new.`label_cs`,
 `label_en` = new.`label_en`,
 `x` = new.`x`,
 `y` = new.`y`,
 `z` = new.`z`,
 `o` = new.`o`,
 `sort_index` = new.`sort_index`,
 `expansion_required` = new.`expansion_required`;
