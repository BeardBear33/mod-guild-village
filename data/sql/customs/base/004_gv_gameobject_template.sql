-- Exportování struktury databáze pro
CREATE DATABASE IF NOT EXISTS `customs` /*!40100 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci */ /*!80016 DEFAULT ENCRYPTION='N' */;
USE `customs`;

-- Exportování struktury pro tabulka customs.gv_gameobject_template
CREATE TABLE IF NOT EXISTS `gv_gameobject_template` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `layout_key` varchar(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'base',
  `entry` int unsigned NOT NULL,
  `map` int unsigned NOT NULL,
  `position_x` float NOT NULL,
  `position_y` float NOT NULL,
  `position_z` float NOT NULL,
  `orientation` float NOT NULL,
  `rotation0` float NOT NULL DEFAULT '0',
  `rotation1` float NOT NULL DEFAULT '0',
  `rotation2` float NOT NULL DEFAULT '0',
  `rotation3` float NOT NULL DEFAULT '0',
  `spawntimesecs` int NOT NULL DEFAULT '0',
  `comment` varchar(100) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_layout` (`layout_key`),
  KEY `idx_entry` (`entry`)
) ENGINE=InnoDB AUTO_INCREMENT=7 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Exportování dat pro tabulku customs.gv_gameobject_template: ~4 rows (přibližně)
INSERT INTO `gv_gameobject_template` (`id`, `layout_key`, `entry`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecs`, `comment`) VALUES
	(1, 'base', 180322, 37, -164.395, -75.5743, 268.8, 3.33241, 0, 0, 0, 0, 0, 'village object-wall'),
	(2, 'base', 180322, 37, -167.811, -57.8902, 268, 3.33241, 0, 0, 0, 0, 0, 'village object-wall'),
	(3, 'base', 180322, 37, -169.3, -50.0935, 268.5, 3.33241, 0, 0, 0, 0, 0, 'village object-wall'),
	(4, 'base', 184828, 37, 1225.98, 229.63, 355.137, 3.86335, 0, 0, 0, 0, 0, 'village object-ring');
