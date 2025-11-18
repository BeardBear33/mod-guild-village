-- ===================================
--  GUILD VILLAGE: GV-Fabricator 01
--  Inserts for acore_world + customs
-- ===================================

-- -----------------------------------
--  ACORE_WORLD
-- -----------------------------------
USE `acore_world`;

INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `name`, `subname`, `IconName`,
    `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
    `speed_walk`, `speed_run`, `speed_swim`, `speed_flight`,
    `detection_range`, `scale`, `rank`, `dmgschool`, `DamageModifier`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`,
    `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`, `family`,
    `trainer_type`, `trainer_spell`, `trainer_class`, `trainer_race`,
    `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`,
    `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`,
    `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`,
    `flags_extra`, `ScriptName`, `VerifiedBuild`
) VALUES
(987448, 0, 0, 0, 0, 0, 'GV-Fabricator 01', '', NULL, 0, 85, 85, 0, 35, 1, 1, 1.14286, 1, 1, 20, 1, 2, 0, 13, 500, 1000, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 7, 76, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 10, 1, 1, 1, 0, 0, 1, 0, 0, 0, 'npc_gv_production', 12340);

INSERT INTO `creature_template_model` (
    `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`
) VALUES
(987448, 0, 8369, 1, 1, 12340);

INSERT INTO `points_of_interest` (
    `ID`, `PositionX`, `PositionY`, `Icon`, `Flags`, `Importance`, `Name`
) VALUES
(1247, 1066, 279.232, 7, 99, 0, 'Production Unit');