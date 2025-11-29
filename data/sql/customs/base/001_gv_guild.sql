-- Exportování struktury pro tabulku customs.gv_guild
DROP TABLE IF EXISTS `customs`.`gv_guild`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_guild` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `guild` int unsigned NOT NULL,
  `phase` int unsigned NOT NULL DEFAULT '0',
  `map` int unsigned NOT NULL,
  `positionx` double NOT NULL,
  `positiony` double NOT NULL,
  `positionz` double NOT NULL,
  `orientation` float NOT NULL,
  `last_update` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_gvguild_guild` (`guild`),
  UNIQUE KEY `uq_gvguild_phase` (`phase`),
  KEY `idx_map` (`map`),
  KEY `idx_last_update` (`last_update`),
  CONSTRAINT `chk_gvguild_phase_valid` CHECK ((`phase` >= 1))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;