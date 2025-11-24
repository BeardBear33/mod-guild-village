-- ================================
-- customs Update: Add 'all' currency option to loot: randomly selects one of the four guild materials.
-- ================================

-- add 'all' enum
ALTER TABLE customs.gv_loot
    MODIFY COLUMN currency ENUM('material1','material2','material3','material4','all')
    NOT NULL;


-- remove old 
DELETE FROM customs.gv_loot WHERE entry BETWEEN 987412 AND 987421;

-- insert new
INSERT INTO `customs`.`gv_loot` (`entry`, `currency`, `chance`, `min_amount`, `max_amount`, `comment`) VALUES
(987412, 'all', 50, 1, 3, 'Old Mammoth'),
(987413, 'all', 50, 1, 3, 'Sinewy Wolf'),
(987414, 'all', 50, 1, 3, 'Dappled Stag'),
(987415, 'all', 50, 1, 3, 'Grove Walker'),
(987416, 'all', 50, 1, 3, 'Shandaral Druid Spirit'),
(987417, 'all', 50, 1, 3, 'Shandaral Warrior Spirit'),
(987418, 'all', 50, 1, 3, 'Storm Revenant'),
(987419, 'all', 50, 1, 3, 'Aqueous Spirit'),
(987420, 'all', 50, 1, 3, 'Stormwatcher'),
(987421, 'all', 50, 1, 3, 'Raging Flame');
