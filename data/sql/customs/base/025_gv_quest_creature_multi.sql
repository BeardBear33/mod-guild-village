-- Exportování struktury pro tabulku customs.gv_quest_creature_multi
DROP TABLE IF EXISTS `customs`.`gv_quest_creature_multi`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_quest_creature_multi` (
  `quest_id` int unsigned NOT NULL,
  `creature_entry` int unsigned NOT NULL,
  PRIMARY KEY (`quest_id`,`creature_entry`),
  KEY `idx_creature_entry` (`creature_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;