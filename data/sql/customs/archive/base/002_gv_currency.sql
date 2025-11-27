CREATE DATABASE IF NOT EXISTS customs
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

-- CS: Valuty guildovní vesnice (jedna řádka na gildovní ID)
-- EN: Guild village currencies (one row per guild)
CREATE TABLE IF NOT EXISTS customs.gv_currency (
  guildId     INT UNSIGNED NOT NULL,
  timber      BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- Timber (wood)
  stone       BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- Stone
  iron        BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- Iron
  crystal     BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- Crystal
  last_update TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (guildId),
  KEY idx_last_update (last_update)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
