CREATE DATABASE IF NOT EXISTS customs
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

-- Jedna vesnice na gildu (unikátní), globálně unikátní phase bit (2..31), vlastní souřadnice
CREATE TABLE IF NOT EXISTS customs.gv_guild (
  id          INT UNSIGNED NOT NULL AUTO_INCREMENT,
  guild       INT UNSIGNED NOT NULL,                 -- guildId
  phase       INT UNSIGNED NOT NULL DEFAULT 0,       -- 0 = nealokováno; jinak power-of-two bit 2..31
  map         INT UNSIGNED NOT NULL,
  positionx   DOUBLE NOT NULL,
  positiony   DOUBLE NOT NULL,
  positionz   DOUBLE NOT NULL,
  orientation FLOAT NOT NULL,
  last_update TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_gvguild_guild (guild),
  UNIQUE KEY uq_gvguild_phase (phase),
  KEY idx_map (map),
  KEY idx_last_update (last_update),
  -- Validace: phase je 0 (nealok.), nebo power-of-two >= 4 (tj. bity 2..31)
  CONSTRAINT chk_gvguild_phase_valid CHECK (
    phase = 0 OR (phase >= 4 AND (phase & (phase - 1)) = 0)
  )
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
