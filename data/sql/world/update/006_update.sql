-- ================================
-- acore_world Update: Expedition Core + Village Seller
-- ================================

-- ---- creature_template: Expedition Camp Core (987445) ----
INSERT INTO `creature_template`
(`entry`,`difficulty_entry_1`,`difficulty_entry_2`,`difficulty_entry_3`,
 `KillCredit1`,`KillCredit2`,`name`,`subname`,`IconName`,`gossip_menu_id`,
 `minlevel`,`maxlevel`,`exp`,`faction`,`npcflag`,
 `speed_walk`,`speed_run`,`speed_swim`,`speed_flight`,`detection_range`,
 `scale`,`rank`,`dmgschool`,`DamageModifier`,`BaseAttackTime`,`RangeAttackTime`,
 `BaseVariance`,`RangeVariance`,`unit_class`,`unit_flags`,`unit_flags2`,
 `dynamicflags`,`family`,`trainer_type`,`trainer_spell`,`trainer_class`,
 `trainer_race`,`type`,`type_flags`,`lootid`,`pickpocketloot`,`skinloot`,
 `PetSpellDataId`,`VehicleId`,`mingold`,`maxgold`,`AIName`,`MovementType`,
 `HoverHeight`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,`ExperienceModifier`,
 `RacialLeader`,`movementId`,`RegenHealth`,`mechanic_immune_mask`,
 `spell_school_immune_mask`,`flags_extra`,`ScriptName`,`VerifiedBuild`)
VALUES
(987445,0,0,0,0,0,'Expedition Camp Core','Upgrade Interface',NULL,0,
 85,85,0,35,1,1,1.14286,1,1,20,1,3,0,20,500,1000,1,1,1,0,0,0,0,0,0,0,0,
 7,76,0,0,0,0,0,0,0,'',0,1,50,1,1,1,0,0,1,0,0,0,'npc_gv_upgrades2',12340)
AS new
ON DUPLICATE KEY UPDATE
 `name`=new.`name`,
 `subname`=new.`subname`,
 `npcflag`=new.`npcflag`,
 `speed_walk`=new.`speed_walk`,
 `speed_run`=new.`speed_run`,
 `HealthModifier`=new.`HealthModifier`,
 `ManaModifier`=new.`ManaModifier`,
 `ArmorModifier`=new.`ArmorModifier`,
 `ScriptName`=new.`ScriptName`,
 `VerifiedBuild`=new.`VerifiedBuild`;


-- ---- creature_template: Merrin the Village Broker (987454) ----
INSERT INTO `creature_template`
(`entry`,`difficulty_entry_1`,`difficulty_entry_2`,`difficulty_entry_3`,
 `KillCredit1`,`KillCredit2`,`name`,`subname`,`IconName`,`gossip_menu_id`,
 `minlevel`,`maxlevel`,`exp`,`faction`,`npcflag`,
 `speed_walk`,`speed_run`,`speed_swim`,`speed_flight`,`detection_range`,
 `scale`,`rank`,`dmgschool`,`DamageModifier`,`BaseAttackTime`,`RangeAttackTime`,
 `BaseVariance`,`RangeVariance`,`unit_class`,`unit_flags`,`unit_flags2`,
 `dynamicflags`,`family`,`trainer_type`,`trainer_spell`,`trainer_class`,
 `trainer_race`,`type`,`type_flags`,`lootid`,`pickpocketloot`,`skinloot`,
 `PetSpellDataId`,`VehicleId`,`mingold`,`maxgold`,`AIName`,`MovementType`,
 `HoverHeight`,`HealthModifier`,`ManaModifier`,`ArmorModifier`,`ExperienceModifier`,
 `RacialLeader`,`movementId`,`RegenHealth`,`mechanic_immune_mask`,
 `spell_school_immune_mask`,`flags_extra`,`ScriptName`,`VerifiedBuild`)
VALUES
(987454,0,0,0,0,0,'Merrin the Village Broker','Village Seller',NULL,0,
 1,1,0,35,129,1,1.14286,1,1,20,1,0,0,1,0,0,1,1,1,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,'',0,1,1,1,1,1,0,0,1,0,0,0,'npc_guild_village_seller',12340)
AS new
ON DUPLICATE KEY UPDATE
 `name`=new.`name`,
 `subname`=new.`subname`,
 `npcflag`=new.`npcflag`,
 `Speed_walk`=new.`Speed_walk`,
 `Speed_run`=new.`Speed_run`,
 `ScriptName`=new.`ScriptName`,
 `VerifiedBuild`=new.`VerifiedBuild`;


-- ---- creature_template_model: display for Expedition Camp Core ----
INSERT INTO `creature_template_model`
(`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`,`VerifiedBuild`)
VALUES
(987445,0,16135,1,1,12340)
AS new
ON DUPLICATE KEY UPDATE
 `CreatureDisplayID`=new.`CreatureDisplayID`,
 `DisplayScale`=new.`DisplayScale`,
 `Probability`=new.`Probability`,
 `VerifiedBuild`=new.`VerifiedBuild`;
