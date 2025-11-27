-- Exportování struktury pro tabulku customs.gv_teleport_player
DROP TABLE IF EXISTS `customs`.`gv_teleport_player`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_teleport_player` (
  `player` int unsigned NOT NULL,
  `guild` int unsigned NOT NULL,
  `map` int unsigned NOT NULL,
  `positionx` double NOT NULL,
  `positiony` double NOT NULL,
  `positionz` double NOT NULL,
  `orientation` float NOT NULL,
  `phase` int unsigned NOT NULL DEFAULT '0',
  `set_time` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`player`),
  KEY `idx_guild` (`guild`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;