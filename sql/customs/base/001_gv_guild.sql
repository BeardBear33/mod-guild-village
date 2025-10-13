CREATE DATABASE IF NOT EXISTS customs
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

-- Jedna vesnice na gildu (unikátní), vlastní souřadnice a fáze per gilda
CREATE TABLE IF NOT EXISTS customs.gv_guild (
  id          INT UNSIGNED NOT NULL AUTO_INCREMENT,
  guild       INT UNSIGNED NOT NULL,               -- guildId
  phase       INT UNSIGNED NOT NULL DEFAULT 1,     -- per-guild phase (roste nezávisle pro každou gildu)
  map         INT UNSIGNED NOT NULL,
  positionx   DOUBLE NOT NULL,
  positiony   DOUBLE NOT NULL,
  positionz   DOUBLE NOT NULL,
  orientation FLOAT  NOT NULL,
  last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uk_guild (guild),
  KEY idx_map (map),
  KEY idx_last_update (last_update)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
