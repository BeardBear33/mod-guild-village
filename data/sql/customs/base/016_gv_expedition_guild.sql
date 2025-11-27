-- Exportování struktury pro tabulku customs.gv_expedition_guild
DROP TABLE IF EXISTS `customs`.`gv_expedition_guild`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_guild` (
  `guildId` int unsigned NOT NULL,
  `heroes_owned` tinyint unsigned NOT NULL DEFAULT '0',
  `heroes_on_mission` tinyint unsigned NOT NULL DEFAULT '0',
  `heroes_max` tinyint unsigned NOT NULL DEFAULT '25',
  `gear_level` smallint unsigned NOT NULL DEFAULT '0',
  `last_update` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guildId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;