-- Exportování struktury pro tabulku customs.gv_upgrades
DROP TABLE IF EXISTS `customs`.`gv_upgrades`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_upgrades` (
  `guildId` int unsigned NOT NULL,
  `expansion_key` varchar(64) NOT NULL,
  `purchased_at` int unsigned NOT NULL,
  PRIMARY KEY (`guildId`,`expansion_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;