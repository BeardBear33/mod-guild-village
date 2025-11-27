-- Exportování struktury pro tabulku customs.gv_expedition_member_watch
DROP TABLE IF EXISTS `customs`.`gv_expedition_member_watch`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_member_watch` (
  `guildId` int unsigned NOT NULL,
  `playerGuid` int unsigned NOT NULL,
  `join_time` int unsigned NOT NULL,
  PRIMARY KEY (`guildId`,`playerGuid`),
  KEY `idx_playerGuid` (`playerGuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Seed join_time pro stávající členy guild
-- (Všichni dostanou fixní "předpokládaný vstup" = 2025-10-01)
SET @SEED_JOIN := UNIX_TIMESTAMP('2025-10-01 00:00:00');

INSERT IGNORE INTO `customs`.`gv_expedition_member_watch`
    (`guildId`, `playerGuid`, `join_time`)
SELECT
    gm.guildid  AS `guildId`,
    gm.guid     AS `playerGuid`,
    @SEED_JOIN  AS `join_time`
FROM `acore_characters`.`guild_member` gm
ON DUPLICATE KEY UPDATE
    `join_time` = VALUES(`join_time`);