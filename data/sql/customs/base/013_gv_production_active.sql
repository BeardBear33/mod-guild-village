-- Exportování struktury pro tabulku customs.gv_production_active
DROP TABLE IF EXISTS `customs`.`gv_production_active`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_production_active` (
  `guildId` int unsigned NOT NULL,
  `material_id` tinyint unsigned NOT NULL,
  `started_at` int unsigned NOT NULL,
  `last_tick` int unsigned NOT NULL,
  PRIMARY KEY (`guildId`,`material_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;