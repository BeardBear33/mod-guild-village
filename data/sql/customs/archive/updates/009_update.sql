-- customs.gv_teleport_player: osobní TP body hráčů do guild vesnice
-- Ukládáme i guild id, aby TP platil jen pokud je hráč stále v té samé gildě.
-- Primární klíč = player (guid low); unique zaručí 1 záznam na hráče.

CREATE TABLE IF NOT EXISTS customs.gv_teleport_player (
  player      INT UNSIGNED NOT NULL,         -- Player GUID (low)
  guild       INT UNSIGNED NOT NULL,         -- Guild ID v okamžiku nastavení
  map         INT UNSIGNED NOT NULL,         -- očekává se 37 (ale necháváme obecné)
  positionx   DOUBLE NOT NULL,
  positiony   DOUBLE NOT NULL,
  positionz   DOUBLE NOT NULL,
  orientation FLOAT  NOT NULL,
  phase       INT UNSIGNED NOT NULL DEFAULT 0,  -- phase mask pro vesnici (z customs.gv_guild.phase)
  set_time    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (player),
  KEY idx_guild (guild)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
