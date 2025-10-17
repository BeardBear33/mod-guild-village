-- ============================
-- Guild Village - Purchased Upgrades
-- ============================

CREATE TABLE IF NOT EXISTS customs.gv_upgrades (
  guildId       INT UNSIGNED NOT NULL,
  expansion_key VARCHAR(64) NOT NULL,
  purchased_at  INT UNSIGNED NOT NULL,
  PRIMARY KEY (guildId, expansion_key)
);

