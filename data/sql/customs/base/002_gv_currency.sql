-- Exportování struktury pro tabulku customs.gv_currency
DROP TABLE IF EXISTS `customs`.`gv_currency`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_currency` (
  `guildId` int unsigned NOT NULL,
  `material1` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'Timber',
  `material2` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'Stone',
  `material3` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'Iron',
  `material4` bigint unsigned NOT NULL DEFAULT '0' COMMENT 'Crystal',
  `last_update` timestamp NULL DEFAULT NULL,
  PRIMARY KEY (`guildId`),
  KEY `idx_last_update` (`last_update`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;