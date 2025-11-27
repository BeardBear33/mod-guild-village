-- Exportování struktury pro tabulku customs.gv_expedition_loot
DROP TABLE IF EXISTS `customs`.`gv_expedition_loot`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_loot` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `guildId` int unsigned NOT NULL,
  `itemid` int unsigned NOT NULL,
  `amount` int unsigned NOT NULL DEFAULT '1',
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_stack` (`guildId`,`itemid`),
  UNIQUE KEY `uq_gv_loot_guild_item` (`guildId`,`itemid`),
  KEY `idx_guild` (`guildId`)
) ENGINE=InnoDB AUTO_INCREMENT=251 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;