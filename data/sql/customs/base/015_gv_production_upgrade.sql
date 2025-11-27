-- Exportování struktury pro tabulku customs.gv_production_upgrade
DROP TABLE IF EXISTS `customs`.`gv_production_upgrade`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_production_upgrade` (
  `guildId` int unsigned NOT NULL,
  `material_id` tinyint unsigned NOT NULL,
  `amount_rank` tinyint unsigned NOT NULL DEFAULT '0',
  `speed_rank` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guildId`,`material_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;