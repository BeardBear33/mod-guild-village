-- ================================
-- customs Update: Boss strategy/position
-- ================================

-- Update gv_creature_template boss spawn (customs DB)
UPDATE `customs`.`gv_creature_template`
SET
    `id` = 3,
    `layout_key` = 'base',
    `entry` = 987400,
    `map` = 37,
    `position_x` = 176.613,
    `position_y` = -348.106,
    `position_z` = 247.338,
    `orientation` = 6.27212,
    `spawntimesecs` = 604800,
    `spawndist` = 0,
    `movementtype` = 0,
    `comment` = 'Village boss'
WHERE `id` = 3;
