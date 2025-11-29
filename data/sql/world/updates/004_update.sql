-- ================================================
-- Guild Village: PvP / Arena POI
-- ================================================
USE `acore_world`;

DELETE FROM `points_of_interest`
WHERE `ID` = 1248;

INSERT IGNORE INTO `points_of_interest` (
    `ID`,
    `PositionX`,
    `PositionY`,
    `Icon`,
    `Flags`,
    `Importance`,
    `Name`
) VALUES
    (1248, 997.877, 344.597, 7, 99, 0, 'PvP');
