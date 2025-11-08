// modules/mod-guild-village/src/boss/boss_thalor_the_lifebinder.cpp

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
static inline bool ThalorHeroic()
{
    return sConfigMgr->GetOption<bool>("GuildVillage.Thalor.Heroic", false);
}

enum ThalorSpells : uint32
{
    SPELL_BERSERK           = 62555,
    SPELL_ENTANGLING_ROOTS  = 20699,
    SPELL_JUDGEMENT_WRATH   = 46033,
    SPELL_NATURE_BOMB       = 64587,
    SPELL_VENOM_BOLT        = 59839,
    SPELL_LIFEBLOOM_H       = 67959,
    SPELL_STORM_WAVE_H      = 63533 
};

enum ThalorEvents : uint32
{
    EVENT_ROOTS = 1,
    EVENT_WRATH,
    EVENT_NATURE_BOMB,
    EVENT_STORM_WAVE,
    EVENT_VENOM_TICK,
    EVENT_BERSERK
};

// --- Časy a parametry ---
static constexpr uint32 ROOTS_FIRST_MS        = 10000; // 10s po pullu
static constexpr uint32 WRATH_AFTER_ROOTS_MS  = 15000; // +15s od zapnutí Roots
static constexpr uint32 BOMB_AFTER_WRATH_MS   = 10000; // +10s od zapnutí Wrath
static constexpr uint32 ROOTS_AFTER_BOMB_MS   = 10000; // +10s po Nature Bomb (loop – Normal)

static constexpr uint32 STORM_AFTER_BOMB_MS   = 20000; // HEROIC: +20s po Nature Bomb

static constexpr uint32 VENOM_CAST_MS         = 1410;  // ~1.41s cast
static constexpr uint32 VENOM_TICK_MS         = 1000;  // zkusit cca každou 1s
static constexpr uint32 VENOM_SAFETY_BUFFER   = 300;   // rezerva, aby nepřetékalo do fáze

static constexpr float  MELEE_PICK_RANGE      = 5.0f;  // pro výběr 4 melee targetů
static constexpr float  ROOTS_RADIUS          = 30.0f; // dosah Roots
static constexpr float  MAX_LOS_RANGE         = 70.0f;
static constexpr float  STORM_MIN_DIST        = 9.0f;  // vzdálený cíl pro druhou vlnu

static constexpr uint32 TIMER_INF             = std::numeric_limits<uint32>::max();

struct boss_thalor_the_lifebinder : public ScriptedAI
{
    boss_thalor_the_lifebinder(Creature* c) : ScriptedAI(c) { }

    EventMap events;

    uint32 tRootsMs  = TIMER_INF;
    uint32 tWrathMs  = TIMER_INF;
    uint32 tBombMs   = TIMER_INF;
    uint32 tStormMs  = TIMER_INF;
	uint32 venomLockMs = 0;

    // -------- Hlášky --------
    void YellAggro()
    {
        me->Yell(GuildVillageLoc::T(
            "Kořeny se probouzí... Nechte se pohltit zelení!",
            "Roots awaken... Be swallowed by verdure!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellRoots()
    {
        me->Yell(GuildVillageLoc::T(
            "Půda vás spoutá trním!",
            "The soil binds you in thorns!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellWrath()
    {
        me->Yell(GuildVillageLoc::T(
            "Hněv přírody skosí vaše kroky!",
            "Nature’s wrath severs your stride!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellBomb()
    {
        me->Yell(GuildVillageLoc::T(
            "Živoucí výboj vás očistí... nebo zlomí!",
            "A living surge will cleanse... or break you!"
        ), LANG_UNIVERSAL, nullptr);
    }
	
	void YellStormWave()
    {
        me->Yell(GuildVillageLoc::T(
            "Vlna života vás smete!",
            "A surge of life will sweep you away!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellBerserk()
    {
        me->Yell(GuildVillageLoc::T(
            "Míza plane! Les řve!",
            "Sap ignites! The forest roars!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellDeath()
    {
        me->Yell(GuildVillageLoc::T(
            "Listy... tiší šum...",
            "Leaves... grow still..."
        ), LANG_UNIVERSAL, nullptr);
    }

    // -------- Utility: sběr hráčů --------
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

    Player* RandomPlayer(float radius = MAX_LOS_RANGE)
    {
        std::vector<Player*> pl;
        CollectPlayers(pl, radius);
        if (pl.empty())
            return nullptr;
        uint32 i = urand(0u, (uint32)pl.size() - 1u);
        return pl[i];
    }

    void PickUpToFourMelee(std::vector<Player*>& out)
    {
        out.clear();
        std::vector<Player*> cands;
        CollectPlayers(cands, MELEE_PICK_RANGE + 1.0f);

        std::sort(cands.begin(), cands.end(), [this](Player* a, Player* b)
        {
            return me->GetDistance(a) < me->GetDistance(b);
        });

        for (Player* p : cands)
        {
            if (me->GetDistance(p) <= MELEE_PICK_RANGE)
                out.push_back(p);
            if (out.size() >= 4)
                break;
        }
    }

    Player* PickDistantTarget(float minDist = STORM_MIN_DIST, float maxDist = MAX_LOS_RANGE)
    {
        std::vector<Player*> cands;
        CollectPlayers(cands, maxDist);
        std::vector<Player*> far;
        for (Player* p : cands)
        {
            if (me->GetDistance(p) >= minDist)
                far.push_back(p);
        }
        if (far.empty())
            return nullptr;
        uint32 i = urand(0u, (uint32)far.size() - 1u);
        return far[i];
    }

    void MaybeApplyLifebloomHeroic()
    {
        if (!ThalorHeroic())
            return;

        if (Aura* a = me->GetAura(SPELL_LIFEBLOOM_H))
        {
            uint8 stacks = a->GetStackAmount();
            if (stacks < 3)
                a->SetStackAmount(stacks + 1);
            a->SetDuration(6000);
            a->SetMaxDuration(6000);
        }
        else
        {
            me->AddAura(SPELL_LIFEBLOOM_H, me);
            if (Aura* b = me->GetAura(SPELL_LIFEBLOOM_H))
            {
                b->SetDuration(6000);
                b->SetMaxDuration(6000);
                b->SetStackAmount(1);
            }
        }
    }

    void Reset() override
    {
        events.Reset();
        me->RemoveAllAuras();
        me->SetReactState(REACT_AGGRESSIVE);
		venomLockMs = 0;

        tRootsMs = tWrathMs = tBombMs = tStormMs = TIMER_INF;
    }

    void JustReachedHome() override
    {
        me->setActive(false);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        using namespace std::chrono;

        me->setActive(true);
        me->CallForHelp(175.0f);
        YellAggro();

        events.ScheduleEvent(EVENT_ROOTS, milliseconds(ROOTS_FIRST_MS));
        tRootsMs = ROOTS_FIRST_MS;

        events.ScheduleEvent(EVENT_VENOM_TICK, milliseconds(VENOM_TICK_MS));

        events.ScheduleEvent(EVENT_BERSERK, 5min);
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    // -------- Jednotlivé akce --------
    void DoEntanglingRoots()
    {
        YellRoots();
        me->CastSpell(me, SPELL_ENTANGLING_ROOTS, true);

        MaybeApplyLifebloomHeroic();
    }

    void DoJudgementOfWrath()
    {
        YellWrath();

        std::vector<Player*> targets;
        PickUpToFourMelee(targets);

        if (targets.empty())
        {
            if (Unit* v = me->GetVictim())
                if (me->GetDistance(v) <= MELEE_PICK_RANGE + 1.0f)
                    me->CastSpell(v, SPELL_JUDGEMENT_WRATH, false);
        }
        else
        {
            for (Player* p : targets)
                me->CastSpell(p, SPELL_JUDGEMENT_WRATH, false);
        }

        MaybeApplyLifebloomHeroic();
    }

    void DoNatureBomb()
    {
        YellBomb();
        me->CastSpell(me, SPELL_NATURE_BOMB, true);

        MaybeApplyLifebloomHeroic();
    }

    void DoStormWaveHeroic()
    {
        if (!ThalorHeroic())
            return;
		
		YellStormWave();

        me->CastSpell(me, SPELL_STORM_WAVE_H, true);

        if (Player* far = PickDistantTarget(STORM_MIN_DIST, MAX_LOS_RANGE))
            me->CastSpell(far, SPELL_STORM_WAVE_H, false);
        else
            me->CastSpell(me, SPELL_STORM_WAVE_H, true);
    }

    void TryCastVenomBolt()
    {
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        uint32 nextMajor = std::min({ tRootsMs, tWrathMs, tBombMs, tStormMs });

        if (nextMajor <= (VENOM_CAST_MS + VENOM_SAFETY_BUFFER))
            return;

		if (Player* t = RandomPlayer(MAX_LOS_RANGE))
		{
			venomLockMs = VENOM_CAST_MS + 1000;
			me->CastSpell(t, SPELL_VENOM_BOLT, false);
		}
    }

    // -------- Update --------
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        auto dec = [](uint32& t, uint32 d)
        {
            if (t == TIMER_INF) return;
            if (d >= t) t = 0; else t -= d;
        };
        dec(tRootsMs, diff);
        dec(tWrathMs, diff);
        dec(tBombMs,  diff);
        dec(tStormMs, diff);
		dec(venomLockMs, diff);

        events.Update(diff);

        using namespace std::chrono;

        uint32 ev;
        while ((ev = events.ExecuteEvent()))
        {
            switch (ev)
            {
                case EVENT_ROOTS:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_ROOTS, 200ms);
                        tRootsMs = 200;
                        break;
                    }

                    DoEntanglingRoots();

                    events.ScheduleEvent(EVENT_WRATH, milliseconds(WRATH_AFTER_ROOTS_MS));
                    tRootsMs = TIMER_INF;
                    tWrathMs = WRATH_AFTER_ROOTS_MS;
                    break;
                }

                case EVENT_WRATH:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_WRATH, 200ms);
                        tWrathMs = 200;
                        break;
                    }

                    DoJudgementOfWrath();

                    events.ScheduleEvent(EVENT_NATURE_BOMB, milliseconds(BOMB_AFTER_WRATH_MS));
                    tWrathMs = TIMER_INF;
                    tBombMs  = BOMB_AFTER_WRATH_MS;
                    break;
                }

                case EVENT_NATURE_BOMB:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_NATURE_BOMB, 200ms);
                        tBombMs = 200;
                        break;
                    }

                    DoNatureBomb();

                    if (ThalorHeroic())
                    {
                        events.ScheduleEvent(EVENT_STORM_WAVE, milliseconds(STORM_AFTER_BOMB_MS));
                        tBombMs  = TIMER_INF;
                        tStormMs = STORM_AFTER_BOMB_MS;
                        tRootsMs = TIMER_INF;
                    }
                    else
                    {
                        events.ScheduleEvent(EVENT_ROOTS, milliseconds(ROOTS_AFTER_BOMB_MS));
                        tBombMs  = TIMER_INF;
                        tRootsMs = ROOTS_AFTER_BOMB_MS;
                    }
                    break;
                }

                case EVENT_STORM_WAVE:
                {
                    if (!ThalorHeroic())
                        break;

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_STORM_WAVE, 200ms);
                        tStormMs = 200;
                        break;
                    }

                    DoStormWaveHeroic();

                    events.ScheduleEvent(EVENT_ROOTS, 0ms);
                    tStormMs = TIMER_INF;
                    tRootsMs = 0;
                    break;
                }

				case EVENT_VENOM_TICK:
				{
					if (venomLockMs == 0)
						TryCastVenomBolt();
				
					events.ScheduleEvent(EVENT_VENOM_TICK, std::chrono::milliseconds(VENOM_TICK_MS));
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
void RegisterGuildVillageThalor()
{
    RegisterCreatureAI(boss_thalor_the_lifebinder);
}
