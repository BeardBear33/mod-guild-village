-- ===================================
--  GUILD VILLAGE: GV-Fabricator 01
--  Idempotent inserts/updates (UPSERT)
--  Target: acore_world
-- ===================================

START TRANSACTION;

USE `acore_world`;

-- -----------------------------------
-- creature_template (PK: entry)
-- -----------------------------------
INSERT INTO `creature_template` (
    `entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`,
    `KillCredit1`, `KillCredit2`, `name`, `subname`, `IconName`,
    `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
    `speed_walk`, `speed_run`, `speed_swim`, `speed_flight`,
    `detection_range`, `scale`, `rank`, `dmgschool`, `DamageModifier`,
    `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`,
    `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`, `family`,
    `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`,
    `PetSpellDataId`, `VehicleId`, `mingold`, `maxgold`, `AIName`,
    `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
    `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
    `RegenHealth`, `mechanic_immune_mask`, `spell_school_immune_mask`,
    `flags_extra`, `ScriptName`, `VerifiedBuild`
) VALUES
(987448, 0, 0, 0, 0, 0, 'GV-Fabricator 01', '', NULL, 0, 85, 85, 0, 35, 1,
 1, 1.14286, 1, 1, 20, 1, 2, 0, 13, 500, 1000, 1, 1, 1, 0, 0, 0, 0, 7, 76, 0, 0, 0, 0, 0, 0, 0, '', 0, 1, 10, 1, 1, 1, 0, 0, 1, 0, 0, 0,
 'npc_gv_production', 12340)
AS new
ON DUPLICATE KEY UPDATE
  `difficulty_entry_1` = new.`difficulty_entry_1`,
  `difficulty_entry_2` = new.`difficulty_entry_2`,
  `difficulty_entry_3` = new.`difficulty_entry_3`,
  `KillCredit1`        = new.`KillCredit1`,
  `KillCredit2`        = new.`KillCredit2`,
  `name`               = new.`name`,
  `subname`            = new.`subname`,
  `IconName`           = new.`IconName`,
  `gossip_menu_id`     = new.`gossip_menu_id`,
  `minlevel`           = new.`minlevel`,
  `maxlevel`           = new.`maxlevel`,
  `exp`                = new.`exp`,
  `faction`            = new.`faction`,
  `npcflag`            = new.`npcflag`,
  `speed_walk`         = new.`speed_walk`,
  `speed_run`          = new.`speed_run`,
  `speed_swim`         = new.`speed_swim`,
  `speed_flight`       = new.`speed_flight`,
  `detection_range`    = new.`detection_range`,
  `scale`              = new.`scale`,
  `rank`               = new.`rank`,
  `dmgschool`          = new.`dmgschool`,
  `DamageModifier`     = new.`DamageModifier`,
  `BaseAttackTime`     = new.`BaseAttackTime`,
  `RangeAttackTime`    = new.`RangeAttackTime`,
  `BaseVariance`       = new.`BaseVariance`,
  `RangeVariance`      = new.`RangeVariance`,
  `unit_class`         = new.`unit_class`,
  `unit_flags`         = new.`unit_flags`,
  `unit_flags2`        = new.`unit_flags2`,
  `dynamicflags`       = new.`dynamicflags`,
  `family`             = new.`family`,
  `type`               = new.`type`,
  `type_flags`         = new.`type_flags`,
  `lootid`             = new.`lootid`,
  `pickpocketloot`     = new.`pickpocketloot`,
  `skinloot`           = new.`skinloot`,
  `PetSpellDataId`     = new.`PetSpellDataId`,
  `VehicleId`          = new.`VehicleId`,
  `mingold`            = new.`mingold`,
  `maxgold`            = new.`maxgold`,
  `AIName`             = new.`AIName`,
  `MovementType`       = new.`MovementType`,
  `HoverHeight`        = new.`HoverHeight`,
  `HealthModifier`     = new.`HealthModifier`,
  `ManaModifier`       = new.`ManaModifier`,
  `ArmorModifier`      = new.`ArmorModifier`,
  `ExperienceModifier` = new.`ExperienceModifier`,
  `RacialLeader`       = new.`RacialLeader`,
  `movementId`         = new.`movementId`,
  `RegenHealth`        = new.`RegenHealth`,
  `mechanic_immune_mask`     = new.`mechanic_immune_mask`,
  `spell_school_immune_mask` = new.`spell_school_immune_mask`,
  `flags_extra`        = new.`flags_extra`,
  `ScriptName`         = new.`ScriptName`,
  `VerifiedBuild`      = new.`VerifiedBuild`;

-- -----------------------------------
-- creature_template_model (PK: CreatureID, Idx)
-- -----------------------------------
INSERT INTO `creature_template_model` (
  `CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`
) VALUES
(987448, 0, 8369, 1, 1, 12340)
AS new
ON DUPLICATE KEY UPDATE
  `CreatureDisplayID` = new.`CreatureDisplayID`,
  `DisplayScale`      = new.`DisplayScale`,
  `Probability`       = new.`Probability`,
  `VerifiedBuild`     = new.`VerifiedBuild`;

-- -----------------------------------
-- points_of_interest (PK: ID)
-- -----------------------------------
INSERT INTO `points_of_interest` (
  `ID`, `PositionX`, `PositionY`, `Icon`, `Flags`, `Importance`, `Name`
) VALUES
(1247, 1066, 279.232, 7, 99, 0, 'Production Unit')
AS new
ON DUPLICATE KEY UPDATE
  `PositionX` = new.`PositionX`,
  `PositionY` = new.`PositionY`,
  `Icon`      = new.`Icon`,
  `Flags`     = new.`Flags`,
  `Importance`= new.`Importance`,
  `Name`      = new.`Name`;

COMMIT;
