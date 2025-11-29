-- WORLD DB

-- 1) Dummy graveyard na mapě 37 (GV)
DELETE FROM game_graveyard WHERE ID = 2000;
INSERT IGNORE INTO game_graveyard (ID, Map, x, y, z, Comment)
VALUES
(2000, 37, 833.867, 105.303, 269.313, 'GV dummy graveyard for zone 268');

-- 2) Napojení zóny 268 na ten hřbitov pro obě frakce (Faction=0 = obě)
DELETE FROM graveyard_zone WHERE GhostZone = 268;
INSERT IGNORE INTO graveyard_zone (ID, GhostZone, Faction, Comment) VALUES
(2000, 268, 0, 'GV zone 268 -> dummy GY (both factions)');
