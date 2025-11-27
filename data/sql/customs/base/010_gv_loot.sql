-- Exportování struktury pro tabulka customs.gv_loot
CREATE TABLE IF NOT EXISTS `gv_loot` (
  `entry` int unsigned NOT NULL,
  `currency` enum('material1','material2','material3','material4','random') NOT NULL,
  `chance` float NOT NULL DEFAULT '100',
  `min_amount` int unsigned NOT NULL DEFAULT '1',
  `max_amount` int unsigned NOT NULL DEFAULT '1',
  `comment` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`entry`,`currency`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Exportování dat pro tabulku customs.gv_loot
INSERT INTO `customs`.`gv_loot` (`entry`, `currency`, `chance`, `min_amount`, `max_amount`, `comment`) VALUES
	(987400, 'material2', 100, 80, 150, 'boss stone'),
	(987401, 'material1', 100, 80, 150, 'boss wood'),
	(987402, 'material2', 100, 5, 10, 'elite stone'),
	(987403, 'material2', 100, 5, 10, 'elite stone'),
	(987404, 'material1', 100, 5, 10, 'elite wood'),
	(987405, 'material1', 100, 5, 10, 'elite wood'),
	(987406, 'material3', 100, 5, 10, 'elite iron'),
	(987407, 'material3', 100, 5, 10, 'elite iron'),
	(987408, 'material3', 100, 80, 150, 'boss iron'),
	(987409, 'material4', 100, 5, 10, 'elite crystal'),
	(987410, 'material4', 100, 5, 10, 'elite crystal'),
	(987411, 'material4', 100, 80, 150, 'boss crystal'),
	(987412, 'random', 50, 2, 8, 'Old Mammoth'),
	(987413, 'random', 50, 2, 8, 'Sinewy Wolf'),
	(987414, 'random', 50, 2, 8, 'Dappled Stag'),
	(987415, 'random', 50, 2, 8, 'Grove Walker'),
	(987416, 'random', 50, 2, 8, 'Shandaral Druid Spirit'),
	(987417, 'random', 50, 2, 8, 'Shandaral Warrior Spirit'),
	(987418, 'random', 50, 2, 8, 'Storm Revenant'),
	(987419, 'random', 50, 2, 8, 'Aqueous Spirit'),
	(987420, 'random', 50, 2, 8, 'Stormwatcher'),
	(987421, 'random', 50, 2, 8, 'Raging Flame');