// modules/mod-guild-village/src/boss/boss_thalgron_the_earthshaker.cpp

#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "Config.h"
#include "ObjectAccessor.h"
#include "GossipDef.h"
#include "SpellAuraEffects.h"

#include <algorithm>
#include <vector>
#include <chrono>
#include <limits>

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
static inline bool ThalgronHeroic()
{
    return sConfigMgr->GetOption<bool>("GuildVillage.Thalgron.Heroic", false);
}

enum ThalgronSpells : uint32
{
    SPELL_BERSERK            = 62555,
    SPELL_RUNE_PUNCH         = 64874,
    SPELL_CRYSTALLINE_CHAINS = 57050,
    SPELL_CRYSTAL_BLOOM      = 48058,
    SPELL_CRYSTALFIRE_BREATH = 57091,
    SPELL_METEOR_STRIKE      = 74648,
    SPELL_STARFALL           = 37124 
};

enum ThalgronEvents : uint32
{
    EVENT_RUNE_PUNCH = 1,
    EVENT_BREATH,
    EVENT_METEOR,
    EVENT_STARFALL,
    EVENT_STARFALL_TICK,
    EVENT_BLOOM_PAIR,
    EVENT_BERSERK
};

// --- Časování (ms) ---
static constexpr uint32 PUNCH_FIRST_NORMAL_MS      = 10000;
static constexpr uint32 PUNCH_FIRST_HEROIC_MS      = 10000;

static constexpr uint32 BREATH_AFTER_PUNCH_N_MS    = 25000;
static constexpr uint32 BREATH_AFTER_PUNCH_H_MS    = 15000;

static constexpr uint32 METEOR_AFTER_BREATH_H_MS   = 15000;

static constexpr uint32 STARFALL_AFTER_BREATH_N_MS = 25000;
static constexpr uint32 STARFALL_AFTER_METEOR_MS   = 20000;

static constexpr uint32 BLOOM_PERIOD_NORMAL_MS     = 18000;
static constexpr uint32 BLOOM_PERIOD_HEROIC_MS     = 13000;

static constexpr uint32 BREATH_CAST_MS             = 1000;
static constexpr uint32 STARFALL_TICK_MS           = 700;
static constexpr uint32 STARFALL_COUNT_NORMAL      = 3;
static constexpr uint32 STARFALL_COUNT_HEROIC      = 6;

static constexpr uint32 SAFETY_RETRY_MS            = 200;

static constexpr float  MELEE_RANGE_PICK           = 5.5f;
static constexpr float  CHAINS_RANGE               = 30.0f;
static constexpr float  MAX_LOS_RANGE              = 70.0f;

static constexpr uint32 TIMER_INF                  = std::numeric_limits<uint32>::max();

struct boss_thalgron_the_earthshaker : public ScriptedAI
{
    boss_thalgron_the_earthshaker(Creature* c) : ScriptedAI(c) { }

    EventMap events;

    uint32 starfallLeft = 0;

    bool chainsRetryPending = false;

    void YellAggro()
    {
        me->Yell(GuildVillageLoc::T(
            "Krystaly zpívají… a země se zlomí!",
            "Crystals sing… and the earth will break!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellRunePunch(Unit* t)
    {
        if (t)
        {
            std::string cs = "Úder otisknul runy do " + t->GetName() + "!";
            std::string en = "The strike imprints runes onto " + t->GetName() + "!";
            me->Yell(GuildVillageLoc::T(cs.c_str(), en.c_str()), LANG_UNIVERSAL, nullptr);
        }
    }

    void YellBreath()
    {
        me->Yell(GuildVillageLoc::T(
            "Dech mrazu a plamene!",
            "Breath of frost and flame!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellStarfall()
    {
        me->Yell(GuildVillageLoc::T(
            "Hvězdy se lámou o zem!",
            "Stars shatter upon the earth!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellBerserk()
    {
        me->Yell(GuildVillageLoc::T(
            "Krystal praská vztekem!",
            "Crystal cracks with fury!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellDeath()
    {
        me->Yell(GuildVillageLoc::T(
            "Ticho… a střepy…",
            "Silence… and shards…"
        ), LANG_UNIVERSAL, nullptr);
    }

    void CollectPlayers(std::vector<Player*>& out, float radius = MAX_LOS_RANGE)
    {
        out.clear();
        if (Map* map = me->GetMap())
        {
            for (auto const& ref : map->GetPlayers())
            {
                if (Player* p = ref.GetSource())
                {
                    if (!p->IsAlive())
                        continue;
                    if (!me->IsWithinDistInMap(p, radius))
                        continue;
                    if (!me->IsWithinLOSInMap(p))
                        continue;
                    out.push_back(p);
                }
            }
        }
    }

    Player* RandomPlayerInRange(float radius)
    {
        std::vector<Player*> pl;
        CollectPlayers(pl, radius);
        if (pl.empty())
            return nullptr;
        uint32 i = urand(0u, (uint32)pl.size() - 1u);
        return pl[i];
    }

    Player* PickMeleeTarget()
    {
        if (Unit* v = me->GetVictim())
            if (me->GetDistance(v) <= MELEE_RANGE_PICK)
                return v->ToPlayer();

        std::vector<Player*> pl;
        CollectPlayers(pl, MELEE_RANGE_PICK + 1.0f);
        if (pl.empty())
            return nullptr;

        std::sort(pl.begin(), pl.end(), [this](Player* a, Player* b)
        {
            return me->GetDistance(a) < me->GetDistance(b);
        });

        for (Player* p : pl)
            if (me->GetDistance(p) <= MELEE_RANGE_PICK)
                return p;

        return nullptr;
    }

    void Reset() override
    {
        events.Reset();
        me->RemoveAllAuras();
        me->SetReactState(REACT_AGGRESSIVE);
        starfallLeft = 0;
        chainsRetryPending = false;
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

        uint32 firstPunch = ThalgronHeroic() ? PUNCH_FIRST_HEROIC_MS : PUNCH_FIRST_NORMAL_MS;
        events.ScheduleEvent(EVENT_RUNE_PUNCH, milliseconds(firstPunch));

        uint32 bloomPeriod = ThalgronHeroic() ? BLOOM_PERIOD_HEROIC_MS : BLOOM_PERIOD_NORMAL_MS;
        events.ScheduleEvent(EVENT_BLOOM_PAIR, milliseconds(bloomPeriod));

        events.ScheduleEvent(EVENT_BERSERK, minutes(5));
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    void ApplyCrystalBloom()
    {
        Aura* a = me->GetAura(SPELL_CRYSTAL_BLOOM);

        if (!a)
        {
            me->AddAura(SPELL_CRYSTAL_BLOOM, me);
            return;
        }

        uint8 stacks = a->GetStackAmount();
        if (stacks < 15)
            a->SetStackAmount(uint8(stacks + 1));

        if (a->GetMaxDuration() > 0)
            a->SetDuration(a->GetMaxDuration());
    }

    void DoRunePunch()
    {
        if (Player* t = PickMeleeTarget())
        {
            YellRunePunch(t);
            me->CastSpell(t, SPELL_RUNE_PUNCH, true);
        }
        else
        {
            if (Unit* v = me->GetVictim())
                me->CastSpell(v, SPELL_RUNE_PUNCH, true);
            else
                me->CastSpell(me, SPELL_RUNE_PUNCH, true);
        }
    }

    void DoBreath()
    {
        YellBreath();
        me->CastSpell(me->GetVictim() ? me->GetVictim() : me, SPELL_CRYSTALFIRE_BREATH, false);
    }

    void DoMeteor()
    {
        me->CastSpell(me, SPELL_METEOR_STRIKE, true);
    }

    void StartStarfallSequence(uint32 count)
    {
        starfallLeft = count;
        YellStarfall();
        me->CastSpell(me, SPELL_STARFALL, true);
        --starfallLeft;

        if (starfallLeft > 0)
            events.ScheduleEvent(EVENT_STARFALL_TICK, milliseconds(STARFALL_TICK_MS));
    }

    void DoStarfallTick()
    {
        if (starfallLeft == 0)
            return;

        me->CastSpell(me, SPELL_STARFALL, true);
        --starfallLeft;

        if (starfallLeft > 0)
            events.ScheduleEvent(EVENT_STARFALL_TICK, milliseconds(STARFALL_TICK_MS));
    }

    void DoCrystalBloomWithChains()
    {
        if (!chainsRetryPending)
            ApplyCrystalBloom();

        if (me->HasUnitState(UNIT_STATE_CASTING))
        {
            chainsRetryPending = true;
            events.ScheduleEvent(EVENT_BLOOM_PAIR, milliseconds(SAFETY_RETRY_MS));
            return;
        }

        if (Player* t = RandomPlayerInRange(CHAINS_RANGE))
        {
            me->CastSpell(t, SPELL_CRYSTALLINE_CHAINS, false);
        }

        chainsRetryPending = false;

        uint32 period = ThalgronHeroic() ? BLOOM_PERIOD_HEROIC_MS : BLOOM_PERIOD_NORMAL_MS;
        events.ScheduleEvent(EVENT_BLOOM_PAIR, milliseconds(period));
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        uint32 ev;
        while ((ev = events.ExecuteEvent()))
        {
            switch (ev)
            {
                case EVENT_RUNE_PUNCH:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_RUNE_PUNCH, milliseconds(SAFETY_RETRY_MS));
                        break;
                    }

                    DoRunePunch();

                    uint32 delay = ThalgronHeroic() ? BREATH_AFTER_PUNCH_H_MS : BREATH_AFTER_PUNCH_N_MS;
                    events.ScheduleEvent(EVENT_BREATH, milliseconds(delay));
                    break;
                }

                case EVENT_BREATH:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_BREATH, milliseconds(SAFETY_RETRY_MS));
                        break;
                    }

                    DoBreath();

                    if (ThalgronHeroic())
                    {
                        events.ScheduleEvent(EVENT_METEOR, milliseconds(METEOR_AFTER_BREATH_H_MS));
                    }
                    else
                    {
                        events.ScheduleEvent(EVENT_STARFALL, milliseconds(STARFALL_AFTER_BREATH_N_MS));
                    }
                    break;
                }

                case EVENT_METEOR:
                {
                    if (!ThalgronHeroic())
                        break;

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_METEOR, milliseconds(SAFETY_RETRY_MS));
                        break;
                    }

                    DoMeteor();

                    events.ScheduleEvent(EVENT_STARFALL, milliseconds(STARFALL_AFTER_METEOR_MS));
                    break;
                }

                case EVENT_STARFALL:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_STARFALL, milliseconds(SAFETY_RETRY_MS));
                        break;
                    }

                    uint32 count = ThalgronHeroic() ? STARFALL_COUNT_HEROIC : STARFALL_COUNT_NORMAL;
                    StartStarfallSequence(count);

                    uint32 finishBuffer = count * STARFALL_TICK_MS;
                    events.ScheduleEvent(EVENT_RUNE_PUNCH, milliseconds(finishBuffer + 200u));
                    break;
                }

                case EVENT_STARFALL_TICK:
                {
                    DoStarfallTick();
                    break;
                }

                case EVENT_BLOOM_PAIR:
                {
                    DoCrystalBloomWithChains();
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
void RegisterGuildVillageThalgron()
{
    RegisterCreatureAI(boss_thalgron_the_earthshaker);
}
