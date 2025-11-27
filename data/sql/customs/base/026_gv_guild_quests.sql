-- Exportování struktury pro tabulku customs.gv_guild_quests
DROP TABLE IF EXISTS `customs`.`gv_guild_quests`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_guild_quests` (
  `guildId` int unsigned NOT NULL,
  `reset_type` tinyint unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL DEFAULT '1',
  `quest_id` int unsigned NOT NULL,
  `progress` int unsigned NOT NULL DEFAULT '0',
  `goal` int unsigned NOT NULL DEFAULT '1',
  `completed` tinyint(1) NOT NULL DEFAULT '0',
  `reward_claimed` tinyint(1) NOT NULL DEFAULT '0',
  `assigned_at` int unsigned NOT NULL,
  `next_rotation_at` int unsigned NOT NULL,
  PRIMARY KEY (`guildId`,`reset_type`,`slot`),
  KEY `idx_next` (`next_rotation_at`),
  KEY `fk_gvq_catalog` (`quest_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;