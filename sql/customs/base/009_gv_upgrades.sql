-- ============================
-- Guild Village - Purchased Upgrades
-- ============================
CREATE TABLE IF NOT EXISTS customs.gv_upgrades (
  guildId       INT UNSIGNED NOT NULL,
  expansion_key VARCHAR(64)  NOT NULL,
  purchased_at  INT UNSIGNED NOT NULL,   -- unix timestamp (sekundy)

  PRIMARY KEY (guildId, expansion_key),  -- unikátní kombinace
  KEY idx_guild (guildId),
  KEY idx_key (expansion_key),

  CONSTRAINT fk_gv_upgrades_guild FOREIGN KEY (guildId)
    REFERENCES guild (guildid) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
