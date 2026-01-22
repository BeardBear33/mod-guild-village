-- -------------------------------------------------
-- acore_world.points_of_interest
-- Teleporter POI entry for Guild Village
-- -------------------------------------------------

INSERT INTO `points_of_interest`
    (`ID`, `PositionX`, `PositionY`, `Icon`, `Flags`, `Importance`, `Name`)
VALUES
    (1249, 1066, 279.232, 7, 99, 0, 'Teleporter')
ON DUPLICATE KEY UPDATE
    `PositionX`   = VALUES(`PositionX`),
    `PositionY`   = VALUES(`PositionY`),
    `Icon`        = VALUES(`Icon`),
    `Flags`       = VALUES(`Flags`),
    `Importance`  = VALUES(`Importance`),
    `Name`        = VALUES(`Name`);

-- -------------------------------------------------
-- acore_world.creature_template
-- Guild Village Expedition NPCs
-- -------------------------------------------------

INSERT INTO `creature_template`
(`entry`, `difficulty_entry_1`, `difficulty_entry_2`, `difficulty_entry_3`, `KillCredit1`, `KillCredit2`,
 `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
 `speed_walk`, `speed_run`, `speed_swim`, `speed_flight`, `detection_range`, `scale`, `rank`, `dmgschool`,
 `DamageModifier`, `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
 `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`, `lootid`, `pickpocketloot`, `skinloot`, `PetSpellDataId`, `VehicleId`,
 `mingold`, `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`, `ManaModifier`,
 `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`, `RegenHealth`, `mechanic_immune_mask`,
 `spell_school_immune_mask`, `flags_extra`, `ScriptName`, `VerifiedBuild`)
VALUES
(987446, 0, 0, 0, 0, 0, 'Expedition Officer Lorien', 'Master of Expeditions', NULL, 0, 85, 85, 0, 35, 1,
 1, 1.14286, 1, 1, 20, 1, 2, 0, 13, 500, 1000, 1, 1, 1, 0, 0, 0, 0, 7, 76, 0, 0, 0, 0, 0, 0, 0,
 '', 0, 1, 10, 1, 1, 1, 0, 0, 1, 0, 0, 0, 'npc_gv_expeditions_missions', 12340),
(987447, 0, 0, 0, 0, 0, 'Hero Quartermaster Kaelor', 'Hero Management', NULL, 0, 85, 85, 0, 35, 1,
 1, 1.14286, 1, 1, 20, 1, 2, 0, 13, 500, 1000, 1, 1, 1, 0, 0, 0, 0, 7, 76, 0, 0, 0, 0, 0, 0, 0,
 '', 0, 1, 10, 1, 1, 1, 0, 0, 1, 0, 0, 0, 'npc_gv_expeditions', 12340)
ON DUPLICATE KEY UPDATE
 `name`        = VALUES(`name`),
 `subname`     = VALUES(`subname`),
 `faction`     = VALUES(`faction`),
 `npcflag`     = VALUES(`npcflag`),
 `ScriptName`  = VALUES(`ScriptName`),
 `VerifiedBuild` = VALUES(`VerifiedBuild`);

-- -------------------------------------------------
-- acore_world.creature_template_model
-- Display models for Guild Village Expedition NPCs
-- -------------------------------------------------

INSERT INTO `creature_template_model`
    (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
VALUES
    (987446, 0, 28189, 1, 1, 12340),
    (987447, 0, 26067, 1, 1, 12340)
ON DUPLICATE KEY UPDATE
    `CreatureDisplayID` = VALUES(`CreatureDisplayID`),
    `DisplayScale`      = VALUES(`DisplayScale`),
    `Probability`       = VALUES(`Probability`),
    `VerifiedBuild`     = VALUES(`VerifiedBuild`);
