-- Exportování struktury pro tabulku customs.gv_production_catalog
DROP TABLE IF EXISTS `customs`.`gv_production_catalog`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_production_catalog` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `material_id` tinyint unsigned NOT NULL,
  `upgrade_type` tinyint unsigned NOT NULL,
  `rank` tinyint unsigned NOT NULL,
  `label_cs` varchar(255) NOT NULL,
  `label_en` varchar(255) NOT NULL,
  `cost_material1` int unsigned NOT NULL DEFAULT '0',
  `cost_material2` int unsigned NOT NULL DEFAULT '0',
  `cost_material3` int unsigned NOT NULL DEFAULT '0',
  `cost_material4` int unsigned NOT NULL DEFAULT '0',
  `sort_order` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_mat_type_rank` (`material_id`,`upgrade_type`,`rank`)
) ENGINE=InnoDB AUTO_INCREMENT=25 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Exportování dat pro tabulku customs.gv_production_catalog
INSERT INTO `customs`.`gv_production_catalog` (`id`, `material_id`, `upgrade_type`, `rank`, `label_cs`, `label_en`, `cost_material1`, `cost_material2`, `cost_material3`, `cost_material4`, `sort_order`) VALUES
	(1, 1, 1, 1, 'Zvýšení produkce množství 1', 'Increase production amount 1', 250, 0, 0, 0, 1),
	(2, 1, 1, 2, 'Zvýšení produkce množství 2', 'Increase production amount 2', 500, 0, 0, 0, 2),
	(3, 1, 1, 3, 'Zvýšení produkce množství 3', 'Increase production amount 3', 1000, 0, 0, 0, 3),
	(4, 1, 2, 1, 'Zrychlení produkce 1', 'Speed up production 1', 250, 0, 0, 0, 1),
	(5, 1, 2, 2, 'Zrychlení produkce 2', 'Speed up production 2', 500, 0, 0, 0, 2),
	(6, 1, 2, 3, 'Zrychlení produkce 3', 'Speed up production 3', 1000, 0, 0, 0, 3),
	(7, 2, 1, 1, 'Zvýšení produkce množství 1', 'Increase production amount 1', 0, 250, 0, 0, 1),
	(8, 2, 1, 2, 'Zvýšení produkce množství 2', 'Increase production amount 2', 0, 500, 0, 0, 2),
	(9, 2, 1, 3, 'Zvýšení produkce množství 3', 'Increase production amount 3', 0, 1000, 0, 0, 3),
	(10, 2, 2, 1, 'Zrychlení produkce 1', 'Speed up production 1', 0, 250, 0, 0, 1),
	(11, 2, 2, 2, 'Zrychlení produkce 2', 'Speed up production 2', 0, 500, 0, 0, 2),
	(12, 2, 2, 3, 'Zrychlení produkce 3', 'Speed up production 3', 0, 1000, 0, 0, 3),
	(13, 3, 1, 1, 'Zvýšení produkce množství 1', 'Increase production amount 1', 0, 0, 250, 0, 1),
	(14, 3, 1, 2, 'Zvýšení produkce množství 2', 'Increase production amount 2', 0, 0, 500, 0, 2),
	(15, 3, 1, 3, 'Zvýšení produkce množství 3', 'Increase production amount 3', 0, 0, 1000, 0, 3),
	(16, 3, 2, 1, 'Zrychlení produkce 1', 'Speed up production 1', 0, 0, 250, 0, 1),
	(17, 3, 2, 2, 'Zrychlení produkce 2', 'Speed up production 2', 0, 0, 500, 0, 2),
	(18, 3, 2, 3, 'Zrychlení produkce 3', 'Speed up production 3', 0, 0, 1000, 0, 3),
	(19, 4, 1, 1, 'Zvýšení produkce množství 1', 'Increase production amount 1', 0, 0, 0, 250, 1),
	(20, 4, 1, 2, 'Zvýšení produkce množství 2', 'Increase production amount 2', 0, 0, 0, 500, 2),
	(21, 4, 1, 3, 'Zvýšení produkce množství 3', 'Increase production amount 3', 0, 0, 0, 1000, 3),
	(22, 4, 2, 1, 'Zrychlení produkce 1', 'Speed up production 1', 0, 0, 0, 250, 1),
	(23, 4, 2, 2, 'Zrychlení produkce 2', 'Speed up production 2', 0, 0, 0, 500, 2),
	(24, 4, 2, 3, 'Zrychlení produkce 3', 'Speed up production 3', 0, 0, 0, 1000, 3);