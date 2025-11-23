// modules/mod-guild-village/src/guild_village_quests_hooks.cpp

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Item.h"
#include "SharedDefines.h"
#include "GameTime.h"
#include "AllSpellScript.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Group.h"
#include "DatabaseEnv.h"

#include <unordered_map>
#include <algorithm>
#include <string>

// Forward deklarace public helperů definovaných v guild_village_quests.cpp
namespace GuildVillage {
    void GV_QuestProgress_OnCraft(::Player* player, std::string const& questType);
    void GV_QuestProgress_OnLoot(::Player* player, std::string const& questType);
    void GV_QuestProgress_OnPvPKill(::Player* player);
    void GV_QuestProgress_OnDungeonBossKill(::Player* killer, ::Creature* killed);
	void GV_QuestProgress_OnCraftItem(::Player* player, uint32 itemEntry);
	void GV_QuestProgress_TapCredit_KillCreature(uint32 guildId, uint32 creatureEntry, Player* notifier);
    void GV_QuestProgress_TapCredit_KillCreatureType(uint32 guildId, uint8 creatureType, Player* notifier);
}

namespace
{
	
    // =========================
    // Gather anti-abuse: herb/mining per guild + GO s TTL
    // =========================

    using RawGuid = uint64;

    struct GatherTrack
    {
        std::unordered_map<RawGuid, uint32> recent;
    };

    // guildId -> její krátkodobá historie nodeů
    static std::unordered_map<uint32, GatherTrack> s_gatherByGuild;

    // jak dlouho držím blok na konkrétní node (v sekundách)
    static constexpr uint32 GATHER_TTL = 300; // 5 minut

    static inline uint32 Now()
    {
        return GameTime::GetGameTime().count();
    }

    static void Gather_Cleanup(GatherTrack& gt)
    {
        uint32 now = Now();
        for (auto it = gt.recent.begin(); it != gt.recent.end(); )
        {
            if (it->second <= now)
                it = gt.recent.erase(it);
            else
                ++it;
        }
    }

    static bool Gather_TryConsume(Player* pl, GameObject* go)
    {
        if (!pl || !go)
            return false;

        uint32 guildId = pl->GetGuildId();
        if (!guildId)
            return false;

        RawGuid goRaw = go->GetGUID().GetRawValue();

        GatherTrack& gt = s_gatherByGuild[guildId];
        Gather_Cleanup(gt);

        uint32 now = Now();
        auto it = gt.recent.find(goRaw);
        if (it != gt.recent.end() && it->second > now)
        {
            return false;
        }

        gt.recent[goRaw] = now + GATHER_TTL;
        return true;
    }

	// =========================
    // Zjištění profesního SkillLine z kouzla (Spell -> SkillLine)
    // =========================
    static uint32 GetProfessionSkillFromSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return 0;

        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellInfo->Id);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (!ability)
                continue;

            return ability->SkillLine;
        }
        return 0;
    }

    // =========================
    // Player hooks – sjednocené
    // =========================

	// Unikátní gildy z TAPu (loot recipient/ group), jen hráči v dosahu odměny.
	// Vrací pouze gildy, které mají Quests upgrade.
	static std::vector<uint32> GetTapGuildsWithQuests(Creature* killed)
	{
		std::vector<uint32> out;
		if (!killed) return out;
	
		auto pushGuild = [&](Player* pl)
		{
			if (!pl) return;
			if (pl->GetMap() != killed->GetMap()) return;
			if (!pl->IsAtGroupRewardDistance(killed)) return;
	
			uint32 gid = pl->GetGuildId();
			if (!gid) return;
	
			// má gilda Quests upgrade?
			if (!WorldDatabase.Query("SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='quests' LIMIT 1", gid))
				return;
	
			if (std::find(out.begin(), out.end(), gid) == out.end())
				out.push_back(gid);
		};
	
		if (Player* rec = killed->GetLootRecipient())
		{
			if (Group* grp = killed->GetLootRecipientGroup())
			{
				for (GroupReference* it = grp->GetFirstMember(); it; it = it->next())
					if (Player* m = it->GetSource())
						pushGuild(m);
			}
			else
			{
				pushGuild(rec);
			}
		}
		return out;
	}
	
	// NOVÉ: Unikátní gildy z TAPu + vybraný "notifier" (první člen gildy v dosahu odměny).
	// Vrací jen gildy, které mají zakoupený Quests upgrade.
	// Pair: <guildId, notifierPlayer*>
	static std::vector<std::pair<uint32, Player*>>
	GetTapGuildsWithQuestsAndNotifier(Creature* killed)
	{
		std::vector<std::pair<uint32, Player*>> out;
		if (!killed)
			return out;
	
		auto emplaceIf = [&](Player* pl)
		{
			if (!pl) return;
			if (pl->GetMap() != killed->GetMap()) return;
			if (!pl->IsAtGroupRewardDistance(killed)) return;
	
			uint32 gid = pl->GetGuildId();
			if (!gid) return;
	
			// kontrola, že guilda má Quests upgrade
			if (!WorldDatabase.Query(
					"SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='quests' LIMIT 1",
					gid))
				return;
	
			for (auto const& pr : out)
				if (pr.first == gid)
					return;
	
			out.emplace_back(gid, pl);
		};
	
		if (Player* rec = killed->GetLootRecipient())
		{
			if (Group* grp = killed->GetLootRecipientGroup())
			{
				for (GroupReference* it = grp->GetFirstMember(); it; it = it->next())
					if (Player* m = it->GetSource())
						emplaceIf(m);
			}
			else
			{
				emplaceIf(rec);
			}
		}
	
		return out;
	}

    class GV_Quests_Wiring_Player : public PlayerScript
    {
    public:
        GV_Quests_Wiring_Player() : PlayerScript("GV_Quests_Wiring_Player") {}

        // 1) KILL CREATURE – univerzální kill + dungeon boss
        void OnPlayerCreatureKill(Player* killer, Creature* killed) override
		{
			if (!killer || !killed) return;
		
			const uint32 entry = killed->GetEntry();
			const uint8  ctype = static_cast<uint8>(killed->GetCreatureType());
		
			// --- TAP kredit pro kill_creature / kill_creature_type ---
			{
				auto tap = GetTapGuildsWithQuestsAndNotifier(killed);
			
				for (auto const& pr : tap)
				{
					uint32 gid   = pr.first;
					Player* who  = pr.second; // notifier
			
					if (entry)
						GuildVillage::GV_QuestProgress_TapCredit_KillCreature(gid, entry, who);
			
					if (ctype)
						GuildVillage::GV_QuestProgress_TapCredit_KillCreatureType(gid, ctype, who);
				}
			}
		
			// --- Boss kredit ---
			auto creditFor = [&](Player* p){
				if (!p) return;
				if (p->GetMap() != killer->GetMap()) return;
				if (p->GetInstanceId() != killer->GetInstanceId()) return;
				GuildVillage::GV_QuestProgress_OnDungeonBossKill(p, killed);
			};
		
			creditFor(killer);
		
			if (Group* gr = killer->GetGroup())
			{
				for (GroupReference* it = gr->GetFirstMember(); it; it = it->next())
					if (Player* m = it->GetSource())
						if (m != killer) creditFor(m);
			}
		}

		// 2) FISHING – „úspěšný ulovek“ při zpracování skillu
		bool OnPlayerUpdateFishingSkill(Player* player,
										int32 /*skill*/,
										int32 /*zoneSkill*/,
										int32 /*chance*/,
										int32 /*roll*/) override
		{
			if (!player)
				return true;
	
			GuildVillage::GV_QuestProgress_OnLoot(player, "loot_fish");
	
			return true;
		}

        // 3) CRAFT – profesní questy řeší AllSpell; tady jen přesný „craft_item“
        void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
        {
            if (!player || !item)
                return;

            // přesný hook pro „craft_item“
            GuildVillage::GV_QuestProgress_OnCraftItem(player, item->GetEntry());
        }

        // 4) PvP HONORABLE KILL HOOK
		void OnPlayerVictimRewardAfter(Player* killer,
									Player* victim,
									uint32& /*killer_title*/,
									uint32& /*victim_rank*/,
									float& honor_f) override
		{
			if (!killer || !victim) return;
			if (honor_f <= 0.0f)     return;
	
			GuildVillage::GV_QuestProgress_OnPvPKill(killer);
		}
    };

    // =========================
    // AllSpell hook: craft profese + gather (herb/mining) + enchant/disenchant + skinning
    // =========================

    class GV_Quests_Wiring_AllSpell : public AllSpellScript
    {
    public:
        GV_Quests_Wiring_AllSpell() : AllSpellScript("GV_Quests_Wiring_AllSpell") {}

        void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* spellInfo, bool /*skipCheck*/) override
        {
            if (!spell || !caster || !spellInfo)
                return;

            Player* pl = caster->ToPlayer();
            if (!pl)
                return;

            // --- 1) Zjistit SkillLine z kouzla (jednou pro všechno) ---
            uint32 skillId = GetProfessionSkillFromSpell(spellInfo);

            // --- 1a) Profesní CRAFT questy (alchemy/bs/eng/insc/jewel/leather/tailor/cooking) ---
            if (skillId)
            {
                std::string craftType;

                switch (skillId)
                {
                    case SKILL_ALCHEMY:         craftType = "craft_alchemy";        break;
                    case SKILL_BLACKSMITHING:   craftType = "craft_blacksmithing";  break;
                    case SKILL_ENGINEERING:     craftType = "craft_engineering";    break;
                    case SKILL_INSCRIPTION:     craftType = "craft_inscription";    break;
                    case SKILL_JEWELCRAFTING:   craftType = "craft_jewelcrafting";  break;
                    case SKILL_LEATHERWORKING:  craftType = "craft_leatherworking"; break;
                    case SKILL_TAILORING:       craftType = "craft_tailoring";      break;
                    case SKILL_COOKING:         craftType = "craft_cooking";        break;
                    default:
                        break;
                }

                if (!craftType.empty())
                    GuildVillage::GV_QuestProgress_OnCraft(pl, craftType);
            }

            // --- 1b) GATHER questy – herbalism / mining (počítat po úspěšném castu) ---
            if (skillId == SKILL_HERBALISM || skillId == SKILL_MINING)
            {
                // Cílový gameobject toho gather spellu (kytka/vein)
                GameObject* go = spell->m_targets.GetGOTarget();
                if (go && Gather_TryConsume(pl, go))
                {
                    if (skillId == SKILL_HERBALISM)
                    {
                        // Úspěšný gather kytky – první pokus v rámci TTL
                        GuildVillage::GV_QuestProgress_OnLoot(pl, "loot_herbal");
                    }
                    else // SKILL_MINING
                    {
                        // Úspěšný gather žíly – první pokus v rámci TTL
                        GuildVillage::GV_QuestProgress_OnLoot(pl, "loot_ores");
                    }
                }
            }


            // --- 2) Enchant / Disenchant (beze změny) ---
            bool doEnchant = false;
            bool doDisenchant = false;

            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                switch (spellInfo->Effects[i].Effect)
                {
                    case SPELL_EFFECT_ENCHANT_ITEM:
                    case SPELL_EFFECT_ENCHANT_HELD_ITEM:
                    case SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC:
                        doEnchant = true;
                        break;

                    case SPELL_EFFECT_DISENCHANT:
                        doDisenchant = true;
                        break;

                    default:
                        break;
                }
            }

            if (doEnchant && !pl->HasSkill(SKILL_ENCHANTING))
                doEnchant = false;

            if (doDisenchant && !pl->HasSkill(SKILL_ENCHANTING))
                doDisenchant = false;

            if (doEnchant)
                GuildVillage::GV_QuestProgress_OnCraft(pl, "enchant");

            if (doDisenchant)
                GuildVillage::GV_QuestProgress_OnCraft(pl, "disenchant");

            // --- 3) Skinning – počítat po úspěšném castu ---
            bool hasSkin = false;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spellInfo->Effects[i].Effect == SPELL_EFFECT_SKINNING)
                {
                    hasSkin = true;
                    break;
                }
            }

            if (hasSkin)
            {
                // najít cíl (creature)
                Unit* target = spell->m_targets.GetUnitTarget();
                if (!target)
                    target = pl->GetSelectedUnit();

                if (target && target->IsCreature())
                {
                    Creature* cr = target->ToCreature();
                    if (cr && !cr->IsAlive())
                    {
                        // Úspěšný skinning tohohle těla – 1x progress
                        GuildVillage::GV_QuestProgress_OnLoot(pl, "loot_skinning");
                    }
                }
            }
        }
    };
} // namespace

void RegisterGuildVillageQuestsWiring()
{
    new GV_Quests_Wiring_Player();
    new GV_Quests_Wiring_AllSpell();
}
