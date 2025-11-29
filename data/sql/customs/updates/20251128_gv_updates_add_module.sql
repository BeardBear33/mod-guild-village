-- Migrace gv_updates: přidání 'module' a přechod na unikát (module, filename)
-- Uprav název modulu pro daný modul:
SET @MODULE := 'mod-guild-village';

-- Přidej sloupec `module`, pokud chybí
SET @c := (SELECT COUNT(*)
           FROM INFORMATION_SCHEMA.COLUMNS
           WHERE TABLE_SCHEMA='customs'
             AND TABLE_NAME='gv_updates'
             AND COLUMN_NAME='module');

SET @sql := IF(@c=0,
  'ALTER TABLE `customs`.`gv_updates` ADD COLUMN `module` VARCHAR(64) NOT NULL DEFAULT '''' AFTER `id`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Doplň prázdné module aktuálním názvem modulu (předpoklad: dosud zapisoval jen tento modul)
UPDATE `customs`.`gv_updates` SET `module`=@MODULE WHERE `module`='';

-- Zahoď starý unikát na `filename`, pokud existuje
SET @idx := (SELECT INDEX_NAME
             FROM INFORMATION_SCHEMA.STATISTICS
             WHERE TABLE_SCHEMA='customs' AND TABLE_NAME='gv_updates' AND INDEX_NAME='uq_filename'
             LIMIT 1);

SET @sql := IF(@idx IS NOT NULL,
  'ALTER TABLE `customs`.`gv_updates` DROP INDEX `uq_filename`',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;

-- Vytvoř nový unikát (module, filename), pokud chybí
SET @idx2 := (SELECT INDEX_NAME
              FROM INFORMATION_SCHEMA.STATISTICS
              WHERE TABLE_SCHEMA='customs' AND TABLE_NAME='gv_updates' AND INDEX_NAME='uq_module_file'
              LIMIT 1);

SET @sql := IF(@idx2 IS NULL,
  'ALTER TABLE `customs`.`gv_updates` ADD UNIQUE INDEX `uq_module_file` (`module`,`filename`)',
  'DO 0');
PREPARE s FROM @sql; EXECUTE s; DEALLOCATE PREPARE s;
