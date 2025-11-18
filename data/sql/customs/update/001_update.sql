-- ================================================
-- Guild Village - Expansion Creatures (Professions)
-- ================================================
USE `customs`;

INSERT INTO `gv_expansion_creatures` (
    `id`,
    `expansion_key`,
    `entry`,
    `map`,
    `position_x`,
    `position_y`,
    `position_z`,
    `orientation`,
    `spawntimesecs`,
    `spawndist`,
    `movementtype`,
    `faction`
) VALUES
    (284, 'prof_tailoring',      28995, 37, 1200.970, 289.121, 359.079, 3.96546, 300, 0, 0, 0),
    (285, 'prof_tailoring',      35496, 37, 1192.850, 291.306, 359.079, 4.49561, 300, 0, 0, 0),
    (286, 'prof_leatherworking', 29523, 37, 1081.880, 384.168, 348.389, 4.05185, 300, 0, 0, 0),
    (287, 'prof_leatherworking', 35500, 37, 1079.590, 388.814, 348.242, 3.54134, 300, 0, 0, 0),
    (288, 'prof_leatherworking', 28992, 37, 1096.880, 375.459, 349.097, 4.35424, 300, 0, 0, 0),
    (289, 'prof_leatherworking', 35497, 37, 1101.650, 375.360, 349.499, 4.96292, 300, 0, 0, 0),
    (290, 'prof_blacksmithing',  34252, 37, 1090.550,  27.853, 317.934, 5.16320, 300, 0, 0, 0),
    (291, 'prof_blacksmithing',  28997, 37, 1097.230,  31.398, 317.934, 4.54275, 300, 0, 0, 0),
    (292, 'prof_blacksmithing',  35498, 37, 1093.930,  29.581, 317.934, 5.11609, 300, 0, 0, 0),
    (293, 'prof_skinning',       28718, 37,  736.268, 156.242, 273.240, 0.04942, 300, 0, 0, 0);
