-- Exportování struktury pro tabulku customs.gv_expedition_active
DROP TABLE IF EXISTS `customs`.`gv_expedition_active`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_active` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `guildId` int unsigned NOT NULL,
  `mission_name` varchar(100) NOT NULL,
  `heroes_sent` tinyint unsigned NOT NULL DEFAULT '0',
  `success_chance` tinyint unsigned NOT NULL DEFAULT '100',
  `end_time` datetime NOT NULL,
  `resolved` tinyint unsigned NOT NULL DEFAULT '0',
  `resolved_at` datetime DEFAULT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_guild` (`guildId`),
  KEY `idx_endtime` (`end_time`),
  KEY `idx_resolved` (`resolved`),
  KEY `idx_gv_active_resolved_at` (`resolved`,`resolved_at`)
) ENGINE=InnoDB AUTO_INCREMENT=6 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;