-- === gv_currency Migrate: timber/stone/iron/crystal -> material1/material2/material3/material4 ===
CREATE DATABASE IF NOT EXISTS `customs`
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;
USE `customs`;

DELIMITER //
CREATE PROCEDURE gv_currency_migrate()
BEGIN
  IF EXISTS (
    SELECT 1 FROM information_schema.tables
    WHERE table_schema = DATABASE() AND table_name = 'gv_currency'
  ) THEN
    IF EXISTS (
      SELECT 1 FROM information_schema.columns
      WHERE table_schema = DATABASE() AND table_name = 'gv_currency' AND column_name = 'timber'
    ) THEN
      ALTER TABLE `gv_currency`
        CHANGE COLUMN `timber`  `material1` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Timber (wood)',
        CHANGE COLUMN `stone`   `material2` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Stone',
        CHANGE COLUMN `iron`    `material3` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Iron',
        CHANGE COLUMN `crystal` `material4` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Crystal';
    END IF;

    ALTER TABLE `gv_currency` CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
  END IF;
END//
DELIMITER ;

CALL gv_currency_migrate();
DROP PROCEDURE gv_currency_migrate;
