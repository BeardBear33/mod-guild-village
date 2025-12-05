// modules/mod-guild-village/src/boss/boss_voltrix_the_unbound.cpp

#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "Config.h"
#include "ObjectAccessor.h"
#include <chrono>
#include <vector>
#include <algorithm>
#include <queue>

using namespace std::chrono;

// -------- Lokalizace (cs/en) --------
namespace GuildVillageLoc
{
    enum class Lang { CS, EN };

    static inline Lang LangOpt()
    {
        std::string loc = sConfigMgr->GetOption<std::string>("GuildVillage.Locale", "cs");
        std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);
        return (loc == "en" || loc == "english") ? Lang::EN : Lang::CS;
    }

    static inline char const* T(char const* cs, char const* en)
    {
        return (LangOpt() == Lang::EN) ? en : cs;
    }
}

// -------- Přepínač obtížnosti --------
static inline bool VoltrixHeroic()
{
    return sConfigMgr->GetOption<bool>("GuildVillage.Voltrix.Heroic", false);
}

enum Spells : uint32
{
    SPELL_FIRE_MISSILE_VISUAL = 45971,
    SPELL_FIRE_MISSILE_DAMAGE = 74421,
    SPELL_FLAME_BREATH        = 50989,
    SPELL_SMASH               = 62465,
    SPELL_BERSERK             = 62555,

    SPELL_CORE_BURST_SINGLE   = 54531,
    SPELL_CORE_SELF_DEBUFF    = 39095
};

enum Events : uint32
{
    EVENT_FIRE_VOLLEY = 1,
    EVENT_FIRE_VOLLEY_TICK,
    EVENT_FIRE_VOLLEY_HIT,
    EVENT_FLAME_BREATH,
    EVENT_SMASH,
    EVENT_BERSERK
};

// -------- Parametry salvy raket (Normal/Heroic) --------
static constexpr uint32 FIRE_VOLLEY_TOTAL_PROJECTILES_N = 20;
static constexpr uint32 FIRE_VOLLEY_TARGETS_N           = 5;
static constexpr uint32 FIRE_PER_TARGET_N               = 4;

static constexpr uint32 FIRE_VOLLEY_TOTAL_PROJECTILES_H = 50;
static constexpr uint32 FIRE_VOLLEY_TARGETS_H           = 10;
static constexpr uint32 FIRE_PER_TARGET_H               = 5;

static inline uint32 FIRE_VOLLEY_TOTAL_PROJECTILES() { return VoltrixHeroic() ? FIRE_VOLLEY_TOTAL_PROJECTILES_H : FIRE_VOLLEY_TOTAL_PROJECTILES_N; }
static inline uint32 FIRE_VOLLEY_TARGETS()           { return VoltrixHeroic() ? FIRE_VOLLEY_TARGETS_H           : FIRE_VOLLEY_TARGETS_N; }
static inline uint32 FIRE_PER_TARGET()               { return VoltrixHeroic() ? FIRE_PER_TARGET_H               : FIRE_PER_TARGET_N; }

// Časování vizuálů/hitů
static constexpr uint32 FIRE_TICK_DELAY_MS = 120;
static constexpr uint32 FIRE_HIT_DELAY_MS  = 140;

struct boss_voltrix_the_unbound : public ScriptedAI
{
    boss_voltrix_the_unbound(Creature* c) : ScriptedAI(c) { }

    EventMap events;

    std::vector<ObjectGuid> volleyTargets;
    std::vector<ObjectGuid> volleyOrder;
    uint32                  volleyTickIndex = 0;

    std::queue<ObjectGuid>  pendingHits;

    uint8                   coreBurstStage = 0;

    void YellAggro()
    {
        me->Yell(GuildVillageLoc::T(
            "Inicializace bojového protokolu. Cíle detekovány.",
            "Initializing combat protocol. Targets acquired."
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellVolley()
    {
        me->Yell(GuildVillageLoc::T(
            "Protokol: SALVA RAKET – spuštěn.",
            "Protocol: ROCKET VOLLEY – engaged."
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellBreath(Unit* t)
    {
        if (t)
        {
            std::string msgCS = "Protokol: PLAMENOMET – cíl: " + t->GetName() + ".";
            std::string msgEN = "Protocol: FLAMETHROWER – target: " + t->GetName() + ".";
            me->Yell(GuildVillageLoc::T(msgCS.c_str(), msgEN.c_str()), LANG_UNIVERSAL, nullptr);
        }
        else
        {
            me->Yell(GuildVillageLoc::T(
                "Protokol: PLAMENOMET – aktivní.",
                "Protocol: FLAMETHROWER – active."
            ), LANG_UNIVERSAL, nullptr);
        }
    }

    void YellSmash(Unit* t)
    {
        if (t)
        {
            std::string msgCS = "Protokol: SMASH – cíl: " + t->GetName() + ".";
            std::string msgEN = "Protocol: SMASH – target: " + t->GetName() + ".";
            me->Yell(GuildVillageLoc::T(msgCS.c_str(), msgEN.c_str()), LANG_UNIVERSAL, nullptr);
        }
        else
        {
            me->Yell(GuildVillageLoc::T(
                "Protokol: SMASH – provádím.",
                "Protocol: SMASH – executing."
            ), LANG_UNIVERSAL, nullptr);
        }
    }

    void YellBerserk()
    {
        me->Yell(GuildVillageLoc::T(
            "Varování: limit výkonu překročen. Režim BERSERK aktivní.",
            "Warning: performance threshold exceeded. BERSERK mode engaged."
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellCoreBurstHeroic()
    {
        me->Yell(GuildVillageLoc::T(
            "Kritické poškození jádra – uvolňuji přebytečnou energii!",
            "Critical core damage – releasing excess energy!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellShieldOverloadNormal()
    {
        me->Yell(GuildVillageLoc::T(
            "Přetížení systému – snižuji výkon štítu.",
            "System overload – reducing shield output."
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellDeath()
    {
        me->Yell(GuildVillageLoc::T(
            "Kritické selhání systému… vypínám.",
            "Critical system failure… shutting down."
        ), LANG_UNIVERSAL, nullptr);
    }

    void Reset() override
    {
        events.Reset();
        while (!pendingHits.empty()) pendingHits.pop();
        volleyTargets.clear();
        volleyOrder.clear();
        volleyTickIndex = 0;
        coreBurstStage = 0;
        me->RemoveAllAuras();
        me->SetReactState(REACT_AGGRESSIVE);
    }

    void JustReachedHome() override
    {
        me->setActive(false);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        me->setActive(true);
        me->CallForHelp(175.0f);
        YellAggro();

        events.ScheduleEvent(EVENT_SMASH, milliseconds(10000));
        events.ScheduleEvent(EVENT_BERSERK, minutes(5));
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    void CheckCoreBursts()
    {
        if (coreBurstStage > 2)
            return;

        static uint8 const thresholds[3] = { 75, 50, 25 };

        if (!me->HealthBelowPct(thresholds[coreBurstStage]))
            return;

        if (VoltrixHeroic())
        {
            YellCoreBurstHeroic();

            std::vector<Player*> candidates;
            CollectPlayers(candidates, 45.0f);

            if (!candidates.empty())
            {
                uint32 n = candidates.size();

                uint32 i1 = urand(0u, n - 1u);
                if (Unit* t1 = candidates[i1])
                    me->CastSpell(t1, SPELL_CORE_BURST_SINGLE, false);

                if (n >= 2)
                {
                    std::swap(candidates[i1], candidates[n - 1]);
                    uint32 i2 = urand(0u, n - 2u);
                    if (Unit* t2 = candidates[i2])
                        me->CastSpell(t2, SPELL_CORE_BURST_SINGLE, false);
                }
            }

            me->AddAura(SPELL_CORE_SELF_DEBUFF, me);
        }
        else
        {
            YellShieldOverloadNormal();
            me->AddAura(SPELL_CORE_SELF_DEBUFF, me);
        }

        ++coreBurstStage;
    }

    void CollectPlayers(std::vector<Player*>& out, float radius = 60.0f)
    {
        out.clear();
        if (Map* map = me->GetMap())
        {
            for (auto const& ref : map->GetPlayers())
            {
                if (Player* p = ref.GetSource())
                    if (p->IsAlive() && me->IsWithinDistInMap(p, radius) && me->IsWithinLOSInMap(p))
                        out.push_back(p);
            }
        }
    }

    void PickVolleyTargets()
    {
        volleyTargets.clear();

        std::vector<Player*> candidates;
        CollectPlayers(candidates, 60.0f);
        if (candidates.empty())
            return;

        uint32 n = candidates.size();
        uint32 need = std::min<uint32>(FIRE_VOLLEY_TARGETS(), n);

        for (uint32 i = 0; i < need; ++i)
        {
            uint32 j = urand(i, n - 1);
            std::swap(candidates[i], candidates[j]);
            volleyTargets.push_back(candidates[i]->GetGUID());
        }
    }

    void BuildVolleyOrder()
    {
        volleyOrder.clear();
        if (volleyTargets.empty())
            return;

        for (ObjectGuid const& g : volleyTargets)
            for (uint32 k = 0; k < FIRE_PER_TARGET(); ++k)
                volleyOrder.push_back(g);

        while (volleyOrder.size() < FIRE_VOLLEY_TOTAL_PROJECTILES())
            volleyOrder.push_back(volleyTargets[urand(0u, (uint32)volleyTargets.size() - 1u)]);

        for (uint32 i = 0; i + 1 < volleyOrder.size(); ++i)
        {
            uint32 j = urand(i, (uint32)volleyOrder.size() - 1u);
            std::swap(volleyOrder[i], volleyOrder[j]);
        }
    }

    Unit* SelectNearestMelee(float maxDist = 8.0f)
    {
        Unit* best = nullptr;
        float bestDist = maxDist + 0.5f;

        std::vector<Player*> pl;
        CollectPlayers(pl, 30.0f);

        for (Player* p : pl)
        {
            float d = me->GetDistance(p);
            if (d < bestDist)
            {
                best = p;
                bestDist = d;
            }
        }
        return best;
    }

    void StartFireVolley()
    {
        PickVolleyTargets();
        if (volleyTargets.empty())
            return;

        BuildVolleyOrder();
        if (volleyOrder.empty())
            return;

        YellVolley();
        volleyTickIndex = 0;

        events.ScheduleEvent(EVENT_FIRE_VOLLEY_TICK, milliseconds(0));
    }

    void DoFireVolleyTick()
    {
        if (volleyOrder.empty() || volleyTickIndex >= volleyOrder.size())
            return;

        ObjectGuid g = volleyOrder[volleyTickIndex];

        if (Unit* t = ObjectAccessor::GetUnit(*me, g))
        {
            if (t->IsAlive() && me->IsWithinLOSInMap(t) && me->IsWithinDistInMap(t, 60.0f))
            {
                me->CastSpell(t, SPELL_FIRE_MISSILE_VISUAL, false);

                pendingHits.push(g);
                events.ScheduleEvent(EVENT_FIRE_VOLLEY_HIT, milliseconds(FIRE_HIT_DELAY_MS));
            }
        }

        volleyTickIndex++;

        if (volleyTickIndex < FIRE_VOLLEY_TOTAL_PROJECTILES())
        {
            events.ScheduleEvent(EVENT_FIRE_VOLLEY_TICK, milliseconds(FIRE_TICK_DELAY_MS));
        }
    }

    void DoFireVolleyHit()
    {
        if (pendingHits.empty())
            return;

        ObjectGuid g = pendingHits.front();
        pendingHits.pop();

        if (Unit* t = ObjectAccessor::GetUnit(*me, g))
        {
            if (t->IsAlive() && me->IsWithinLOSInMap(t) && me->IsWithinDistInMap(t, 60.0f))
                me->CastSpell(t, SPELL_FIRE_MISSILE_DAMAGE, true);
        }
    }

    void StartFlameBreath()
    {
        if (Unit* m = SelectNearestMelee(8.0f))
        {
            YellBreath(m);
            DoCast(m, SPELL_FLAME_BREATH);
        }
    }

    void DoSmash()
    {
        if (Unit* v = me->GetVictim())
        {
            YellSmash(v);
            me->CastSpell(v, SPELL_SMASH, false);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        CheckCoreBursts();

        events.Update(diff);

        uint32 evId;
        while ((evId = events.ExecuteEvent()))
        {
            switch (evId)
            {
                case EVENT_SMASH:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_SMASH, milliseconds(1000));
                        break;
                    }

                    DoSmash();
                    events.ScheduleEvent(EVENT_FIRE_VOLLEY, milliseconds(25000));
                    break;
                }

                case EVENT_FIRE_VOLLEY:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_FIRE_VOLLEY, milliseconds(1000));
                        break;
                    }

                    StartFireVolley();
                    events.ScheduleEvent(EVENT_FLAME_BREATH, milliseconds(25000));
                    break;
                }

                case EVENT_FIRE_VOLLEY_TICK:
                {
                    DoFireVolleyTick();
                    break;
                }

                case EVENT_FIRE_VOLLEY_HIT:
                {
                    DoFireVolleyHit();
                    break;
                }

                case EVENT_FLAME_BREATH:
                {
                    StartFlameBreath();
                    events.ScheduleEvent(EVENT_SMASH, milliseconds(10000));
                    break;
                }

                case EVENT_BERSERK:
                {
                    YellBerserk();
                    me->CastSpell(me, SPELL_BERSERK, true);
                    break;
                }
            }
        }

        if (!me->HasUnitState(UNIT_STATE_CASTING))
            DoMeleeAttackIfReady();
    }
};

// ============================
// Registrace
// ============================
void RegisterGuildVillageVoltrix()
{
    RegisterCreatureAI(boss_voltrix_the_unbound);
}
