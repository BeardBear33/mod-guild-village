CREATE TABLE IF NOT EXISTS customs.gv_production_active (
    guildId      INT UNSIGNED NOT NULL,
    material_id  TINYINT UNSIGNED NOT NULL,  -- 1..4
    started_at   INT UNSIGNED NOT NULL,      -- UNIX_TIMESTAMP startu
    last_tick    INT UNSIGNED NOT NULL,      -- UNIX_TIMESTAMP posledního ticku
    PRIMARY KEY (guildId, material_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS customs.gv_production_upgrade (
    guildId      INT UNSIGNED NOT NULL,
    material_id  TINYINT UNSIGNED NOT NULL,  -- 1..4
    amount_rank  TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0..3
    speed_rank   TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0..3
    PRIMARY KEY (guildId, material_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS customs.gv_production_catalog (
    id               INT UNSIGNED NOT NULL AUTO_INCREMENT,
    material_id      TINYINT UNSIGNED NOT NULL,    -- 1..4
    upgrade_type     TINYINT UNSIGNED NOT NULL,    -- 1=amount, 2=speed
    `rank`           TINYINT UNSIGNED NOT NULL,    -- 1..3

    label_cs         VARCHAR(255) NOT NULL,
    label_en         VARCHAR(255) NOT NULL,

    cost_material1   INT UNSIGNED NOT NULL DEFAULT 0,
    cost_material2   INT UNSIGNED NOT NULL DEFAULT 0,
    cost_material3   INT UNSIGNED NOT NULL DEFAULT 0,
    cost_material4   INT UNSIGNED NOT NULL DEFAULT 0,

    sort_order       TINYINT UNSIGNED NOT NULL DEFAULT 0,

    PRIMARY KEY (id),
    UNIQUE KEY uq_mat_type_rank (material_id, upgrade_type, `rank`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO customs.gv_production_catalog
(material_id, upgrade_type, `rank`, label_cs, label_en,
 cost_material1, cost_material2, cost_material3, cost_material4, sort_order)
VALUES
-- Material1 - amount ranks
(1, 1, 1,
 'Zvýšení produkce množství 1',
 'Increase production amount 1',
 10, 0, 0, 0,
 1),

(1, 1, 2,
 'Zvýšení produkce množství 2',
 'Increase production amount 2',
 25, 0, 0, 0,
 2),

(1, 1, 3,
 'Zvýšení produkce množství 3',
 'Increase production amount 3',
 75, 0, 0, 0,
 3),

-- Material1 - speed ranks
(1, 2, 1,
 'Zrychlení produkce 1',
 'Speed up production 1',
 10, 0, 0, 0,
 1),

(1, 2, 2,
 'Zrychlení produkce 2',
 'Speed up production 2',
 25, 0, 0, 0,
 2),

(1, 2, 3,
 'Zrychlení produkce 3',
 'Speed up production 3',
 75, 0, 0, 0,
 3);
