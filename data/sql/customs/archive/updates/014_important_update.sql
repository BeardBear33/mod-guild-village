-- Exportování struktury pro tabulku customs.gv_updates
CREATE TABLE IF NOT EXISTS `customs`.`gv_updates` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `filename` varchar(255) NOT NULL,
  `sha1` char(40) NOT NULL DEFAULT '',
  `applied_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_filename` (`filename`)
) ENGINE=InnoDB AUTO_INCREMENT=27 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Exportování dat pro tabulku customs.gv_updates
INSERT INTO `customs`.`gv_updates` (`id`, `filename`, `sha1`, `applied_at`) VALUES
	(1, 'base/base/001_gv_guild.sql', 'b8fb46eabe3fe9e9cb33b57697e5efdb1782d913', '2025-11-26 08:06:52'),
	(2, 'base/base/002_gv_currency.sql', '45ad3fd1b6202c62a97da892520881fab1b678b3', '2025-11-26 08:06:52'),
	(3, 'base/base/003_gv_creature_template.sql', '8aae1e2e087f0ce67423fd80a19293ab20404c41', '2025-11-26 08:06:53'),
	(4, 'base/base/004_gv_gameobject_template.sql', '58b6072914fdf8efccac704ff5b6ea677ebeb81d', '2025-11-26 08:06:53'),
	(5, 'base/base/005_gv_expansion_creatures.sql', '9d75e502fa9a1e1d7bb508da3db54f2220fffd70', '2025-11-26 08:06:53'),
	(6, 'base/base/006_gv_expansion_gameobjects.sql', '9c3a2f21ed2d92b344ee1e4e480e96c6616a9005', '2025-11-26 08:06:53'),
	(7, 'base/base/007_gv_upgrades.sql', 'aa178a246b5af67d3a19c86edba1a11b8d499741', '2025-11-26 08:06:53'),
	(8, 'base/base/008_gv_upgrade_catalog.sql', 'e789b56508b458b913402d996c331a2340f3ffda', '2025-11-26 08:06:53'),
	(9, 'base/base/009_gv_upgrade_poi.sql', 'ff9c02af1dc899a46ccc860a114acc086badae6a', '2025-11-26 08:06:53'),
	(10, 'base/base/010_gv_loot.sql', '23b0c847cc5650d87c3b1ad1c28b62ff3024ab32', '2025-11-26 08:06:53'),
	(11, 'base/base/011_gv_teleport_menu.sql', '8978bf68fe70d42899d29f1858bfa47de5fb45ea', '2025-11-26 08:06:54'),
	(12, 'base/base/012_gv_teleport_player.sql', '0a304034406428a504ec170dd5e586a59d41fa22', '2025-11-26 08:06:54'),
	(13, 'base/base/013_gv_production_active.sql', 'b916c3d4514f1efb3c979aa6afe60931d6a0d922', '2025-11-26 08:06:54'),
	(14, 'base/base/014_gv_production_catalog.sql', '767e829f1ada54f62501f53231b559709ccbf60d', '2025-11-26 08:06:54'),
	(15, 'base/base/015_gv_production_upgrade.sql', '7749821abbb098da985bcd3d2805a65e5cd972a2', '2025-11-26 08:06:54'),
	(16, 'base/base/016_gv_expedition_guild.sql', 'c64491e3baf41720c69fe52e973c737bb6650a36', '2025-11-26 08:06:54'),
	(17, 'base/base/017_gv_expedition_catalog.sql', '2d991e586fbfa7b5dc170516064c26c401eb3f6f', '2025-11-26 08:06:54'),
	(18, 'base/base/018_gv_expedition_gear_catalog.sql', 'aeac5845eb9fc6f296c83a9d52dd48f4696b201b', '2025-11-26 08:06:54'),
	(19, 'base/base/019_gv_expedition_mission_cost.sql', 'eda9f2fbbda1e8264791f3b92b77f9a313d10548', '2025-11-26 08:06:54'),
	(20, 'base/base/020_gv_expedition_requirements.sql', '899342168b887f0744a861bd5acb0a02d99c5d45', '2025-11-26 08:06:54'),
	(21, 'base/base/021_gv_expedition_loot.sql', '5eef28044082b437f8d54454c19fff75b6d74ad5', '2025-11-26 08:06:54'),
	(22, 'base/base/022_gv_expedition_active.sql', 'cdb9a6171d46d20ccc6aba6b0c175b2e88fc0542', '2025-11-26 08:06:55'),
	(23, 'base/base/023_gv_expedition_member_watch.sql', 'e7832c4da15ddf2049e5ae1a5174a3df9e2b8f03', '2025-11-26 08:06:55'),
	(24, 'base/base/024_gv_quest_catalog.sql', '6b0be3ef64d9ed447ea6c6c1487ac36a14a2c172', '2025-11-26 08:06:55'),
	(25, 'base/base/025_gv_quest_creature_multi.sql', '1d1a77a4dcb12b293af9d39f26901d7746a5b79d', '2025-11-26 08:06:55'),
	(26, 'base/base/026_gv_guild_quests.sql', '94e0b8339a178bc80f8ddf594a904a7d2f51d351', '2025-11-26 08:06:55');