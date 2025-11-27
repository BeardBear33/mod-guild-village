-- Exportování struktury pro tabulku customs.gv_expedition_catalog
DROP TABLE IF EXISTS `customs`.`gv_expedition_catalog`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_catalog` (
  `slot` tinyint unsigned NOT NULL,
  `label_cs` varchar(64) NOT NULL DEFAULT 'Hrdina',
  `label_en` varchar(64) NOT NULL DEFAULT 'Hero',
  `cost_mat1` int unsigned NOT NULL DEFAULT '0',
  `cost_mat2` int unsigned NOT NULL DEFAULT '0',
  `cost_mat3` int unsigned NOT NULL DEFAULT '0',
  `cost_mat4` int unsigned NOT NULL DEFAULT '0',
  `cost_gold` int unsigned NOT NULL DEFAULT '0',
  `successChance` tinyint unsigned NOT NULL DEFAULT '100',
  PRIMARY KEY (`slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Exportování dat pro tabulku customs.gv_expedition_catalog
INSERT INTO `customs`.`gv_expedition_catalog` (`slot`, `label_cs`, `label_en`, `cost_mat1`, `cost_mat2`, `cost_mat3`, `cost_mat4`, `cost_gold`, `successChance`) VALUES
	(1, 'Arendil', 'Arendil', 25, 25, 50, 50, 250, 50),
	(2, 'Morvyn', 'Morvyn', 25, 25, 50, 50, 250, 60),
	(3, 'Kaelthara', 'Kaelthara', 25, 25, 50, 50, 250, 70),
	(4, 'Dravon', 'Dravon', 25, 25, 50, 50, 250, 80),
	(5, 'Lysara', 'Lysara', 50, 50, 75, 75, 500, 90),
	(6, 'Vorthan', 'Vorthan', 75, 75, 100, 100, 750, 35),
	(7, 'Elurien', 'Elurien', 75, 75, 100, 100, 750, 45),
	(8, 'Sarthor', 'Sarthor', 75, 75, 100, 100, 750, 55),
	(9, 'Myrdel', 'Myrdel', 75, 75, 100, 100, 750, 75),
	(10, 'Thaliora', 'Thaliora', 100, 100, 125, 125, 1000, 85),
	(11, 'Radanor', 'Radanor', 125, 125, 150, 150, 1250, 5),
	(12, 'Velthar', 'Velthar', 125, 125, 150, 150, 1250, 5),
	(13, 'Serina', 'Serina', 125, 125, 150, 150, 1250, 5),
	(14, 'Korveth', 'Korveth', 125, 125, 150, 150, 1250, 5),
	(15, 'Malenra', 'Malenra', 125, 125, 150, 150, 1250, 15),
	(16, 'Dareth', 'Dareth', 125, 125, 150, 150, 1250, 15),
	(17, 'Isyra', 'Isyra', 125, 125, 150, 150, 1250, 15),
	(18, 'Thorgar', 'Thorgar', 125, 125, 150, 150, 1250, 15),
	(19, 'Nerya', 'Nerya', 125, 125, 150, 150, 1250, 15),
	(20, 'Keldros', 'Keldros', 125, 125, 150, 150, 1250, 50),
	(21, 'Althira', 'Althira', 125, 125, 150, 150, 1250, 50),
	(22, 'Fenrath', 'Fenrath', 125, 125, 150, 150, 1250, 55),
	(23, 'Sylven', 'Sylven', 175, 175, 200, 200, 1500, 60),
	(24, 'Garanir', 'Garanir', 225, 225, 250, 250, 2000, 65),
	(25, 'Veyra', 'Veyra', 275, 275, 300, 300, 2500, 70);