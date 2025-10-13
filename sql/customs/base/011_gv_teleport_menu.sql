-- Exportování struktury databáze pro
CREATE DATABASE IF NOT EXISTS `customs` /*!40100 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci */ /*!80016 DEFAULT ENCRYPTION='N' */;
USE `customs`;

-- Exportování struktury pro tabulka customs.gv_teleport_menu
CREATE TABLE IF NOT EXISTS `gv_teleport_menu` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `teleporter_entry` int unsigned NOT NULL DEFAULT '990203',
  `label_cs` varchar(64) NOT NULL,
  `label_en` varchar(64) NOT NULL,
  `x` float NOT NULL,
  `y` float NOT NULL,
  `z` float NOT NULL,
  `o` float NOT NULL,
  `sort_index` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `idx_entry` (`teleporter_entry`),
  KEY `idx_sort` (`teleporter_entry`,`sort_index`)
) ENGINE=InnoDB AUTO_INCREMENT=11 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Exportování dat pro tabulku customs.gv_teleport_menu: ~3 rows (přibližně)
INSERT INTO `gv_teleport_menu` (`id`, `teleporter_entry`, `label_cs`, `label_en`, `x`, `y`, `z`, `o`, `sort_index`) VALUES
	(1, 990203, 'Do vesnice', 'To the Village', 1063.29, 271.808, 336.837, 4.39102, 10),
	(2, 990203, 'Kovárna mistra oceli', 'Forge of the Steel Master', 1051.56, 28.2456, 315.229, 4.69428, 20),
	(3, 990203, 'Dílna mistra kožešin', 'Workshop of the Fur Master', 738.18, 170.549, 274.603, 5.29509, 30),
	(4, 990203, 'Zahrada mistra bylin', 'Garden of the Herbal Master', 917.491, 348.518, 270.731, 1.68697, 40),
	(7, 990203, 'Thranok the Unyielding (Boss)', 'Thranok the Unyielding (Boss)', 399.661, -154.433, 266.946, 4.66128, 70),
	(8, 990203, 'Thalor the Lifebinder (Boss)', 'Thalor the Lifebinder (Boss)', 180.379, 2.1369, 238.02, 5.39172, 80),
	(9, 990203, 'Voltrix the Unbound (Boss)', 'Voltrix the Unbound (Boss)', -47.1828, 293.399, 290.626, 2.509, 90),
	(10, 990203, 'Thalgron the Earthshaker (Boss)', 'Thalgron the Earthshaker (Boss)', 164.313, 584.39, 271.354, 2.82002, 100);
