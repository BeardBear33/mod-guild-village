USE `customs`;

ALTER TABLE `gv_loot`
  MODIFY `currency` ENUM('timber','stone','iron','crystal','material1','material2','material3','material4') NOT NULL;

UPDATE `gv_loot` SET `currency`='material1' WHERE `currency`='timber';
UPDATE `gv_loot` SET `currency`='material2' WHERE `currency`='stone';
UPDATE `gv_loot` SET `currency`='material3' WHERE `currency`='iron';
UPDATE `gv_loot` SET `currency`='material4' WHERE `currency`='crystal';

ALTER TABLE `gv_loot`
  MODIFY `currency` ENUM('material1','material2','material3','material4') NOT NULL;

