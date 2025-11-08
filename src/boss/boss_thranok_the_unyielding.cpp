// modules/mod-guild-village/src/boss/boss_thranok_the_unyielding.cpp

#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "Config.h"
#include "ObjectAccessor.h"
#include "GossipDef.h"
#include "ScriptMgr.h"
#include <algorithm>
#include <vector>
#include <chrono>
#include <cmath>

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

// -------- Přepínač obtížnosti (Normal/Heroic) --------
static inline bool ThranokHeroic()
{
    return sConfigMgr->GetOption<bool>("GuildVillage.Thranok.Heroic", false);
}

enum ThranokSpells : uint32
{
    SPELL_BERSERK            = 62555,
    SPELL_MASSIVE_STOMP      = 71114,
    SPELL_VIOLENT_EARTH      = 63149,
    SPELL_EARTHQUAKE         = 64697,
    SPELL_EARTHQUAKE_AURA_H  = 46932 
};

enum ThranokEvents : uint32
{
    EVENT_MASSIVE_STOMP = 1,
    EVENT_VIOLENT_EARTH,
    EVENT_EARTHQUAKE,
    EVENT_HEROIC_AURA_FROM_STOMP,
    EVENT_HEROIC_AURA_FROM_QUAKE,
    EVENT_BERSERK
};

// Časování
static constexpr uint32 MS_FIRST_DELAY_MS        = 5000;  // 5s od začátku boje
static constexpr uint32 VE_AFTER_MS_MS           = 15000; // 15s po Massive Stomp
static constexpr uint32 VE_CAST_MS               = 1140;  // 1.14 s cast (orientačně)
static constexpr uint32 QUAKE_AFTER_VE_END_MS    = 15000; // 15s po skončení Violent Earth
static constexpr uint32 MS_AFTER_QUAKE_END_MS    = 5000;  // 5s po skončení Earthquake

// Heroic aura – malé „posuny“, aby šla tesně po/vedle hlavních kouzel (nebrzdí plán)
static constexpr uint32 AURA_AFTER_STOMP_MS      = 50;    // cca „hned po dupnutí“
static constexpr uint32 AURA_WITH_QUAKE_MS       = 0;     // společně s Earthquake

// Výběr cíle pro Violent Earth – parametry
static constexpr float  VE_MAX_RANGE             = 60.0f;
static constexpr float  VE_SAFE_RADIUS           = 4.0f;  // chceme cíl, který má co nejméně spojenců v 4yd
static constexpr float  LOS_RANGE                = 70.0f; // bezpečný horní limit pro LOS check

struct boss_thranok_the_unyielding : public ScriptedAI
{
    boss_thranok_the_unyielding(Creature* c) : ScriptedAI(c) { }

    EventMap events;

    // -------- Hlášky --------
    void YellAggro()
    {
        me->Yell(GuildVillageLoc::T(
            "Skála se probouzí... Váš konec se otřese v základech!",
            "Stone awakens... Your end will crumble beneath my steps!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellMassiveStomp()
    {
        me->Yell(GuildVillageLoc::T(
            "Země se prohne pod mým krokem!",
            "The earth bends beneath my step!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellViolentEarth(Unit* t)
    {
        if (t)
        {
            std::string cs = "Z hlubin se zvedá hrot pro " + t->GetName() + "!";
            std::string en = "From the depths, a spike rises for " + t->GetName() + "!";
            me->Yell(GuildVillageLoc::T(cs.c_str(), en.c_str()), LANG_UNIVERSAL, nullptr);
        }
        else
        {
            me->Yell(GuildVillageLoc::T(
                "Z hlubin se zvedají kamenné hroty!",
                "From the depths, stone spikes rise!"
            ), LANG_UNIVERSAL, nullptr);
        }
    }

    void YellEarthquake()
    {
        me->Yell(GuildVillageLoc::T(
            "Třeste se! Země vás pohltí!",
            "Tremble! The earth will swallow you!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellBerserk()
    {
        me->Yell(GuildVillageLoc::T(
            "Hněv hor nezná hranic!",
            "The mountains’ wrath knows no bounds!"
        ), LANG_UNIVERSAL, nullptr);
    }

    void YellDeath()
    {
        me->Yell(GuildVillageLoc::T(
            "Praskliny... se uzavírají...",
            "The cracks... grow still..."
        ), LANG_UNIVERSAL, nullptr);
    }

    void CollectPlayers(std::vector<Player*>& out, float radius = LOS_RANGE)
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

    uint32 CountAlliesWithin(Player* candidate, std::vector<Player*> const& all, float r)
    {
        uint32 n = 0;
        for (Player* other : all)
        {
            if (other == candidate) continue;
            if (!other->IsAlive())  continue;
            if (candidate->GetDistance(other) <= r)
                ++n;
        }
        return n;
    }

    Player* PickViolentEarthTarget()
    {
        std::vector<Player*> cands;
        CollectPlayers(cands, VE_MAX_RANGE);
        if (cands.empty())
            return nullptr;

        struct Scored { Player* p; uint32 neighbors; };
        std::vector<Scored> scored;
        scored.reserve(cands.size());

        for (Player* p : cands)
        {
            uint32 neigh = CountAlliesWithin(p, cands, VE_SAFE_RADIUS);
            scored.push_back({ p, neigh });
        }

        auto best = std::min_element(scored.begin(), scored.end(),
            [](Scored const& a, Scored const& b){ return a.neighbors < b.neighbors; });

        if (best == scored.end())
            return nullptr;

        uint32 minNeigh = best->neighbors;

        std::vector<Player*> shortlist;
        for (auto const& s : scored)
            if (s.neighbors == (minNeigh == 0 ? 0u : minNeigh))
                shortlist.push_back(s.p);

        if (shortlist.empty())
            return nullptr;

        uint32 i = urand(0u, (uint32)shortlist.size() - 1u);
        return shortlist[i];
    }

    void Reset() override
    {
        events.Reset();
        me->RemoveAllAuras();
        me->SetReactState(REACT_AGGRESSIVE);
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

        events.ScheduleEvent(EVENT_MASSIVE_STOMP, milliseconds(MS_FIRST_DELAY_MS));

        events.ScheduleEvent(EVENT_BERSERK, 5min);
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    // -------- Jednotlivé akce --------
    void DoMassiveStomp()
    {
        using namespace std::chrono;

        YellMassiveStomp();
        me->CastSpell(me, SPELL_MASSIVE_STOMP, true);

        if (ThranokHeroic())
            events.ScheduleEvent(EVENT_HEROIC_AURA_FROM_STOMP, milliseconds(AURA_AFTER_STOMP_MS));
    }

    void StartViolentEarth()
    {
        if (Unit* t = PickViolentEarthTarget())
        {
            YellViolentEarth(t);
            me->CastSpell(t, SPELL_VIOLENT_EARTH, false);
        }
        else
        {
            YellViolentEarth(nullptr);
            if (Unit* v = me->GetVictim())
                me->CastSpell(v, SPELL_VIOLENT_EARTH, false);
            else
                me->CastSpell(me, SPELL_VIOLENT_EARTH, false);
        }
    }

    void DoEarthquake()
    {
        using namespace std::chrono;

        YellEarthquake();
        me->CastSpell(me, SPELL_EARTHQUAKE, true);

        if (ThranokHeroic())
            events.ScheduleEvent(EVENT_HEROIC_AURA_FROM_QUAKE, milliseconds(AURA_WITH_QUAKE_MS));
    }

    void CastHeroicEarthquakeAura()
    {
        me->CastSpell(me, SPELL_EARTHQUAKE_AURA_H, true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        using namespace std::chrono;

        uint32 ev;
        while ((ev = events.ExecuteEvent()))
        {
            switch (ev)
            {
                case EVENT_MASSIVE_STOMP:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_MASSIVE_STOMP, 500ms);
                        break;
                    }

                    DoMassiveStomp();

                    events.ScheduleEvent(EVENT_VIOLENT_EARTH, milliseconds(VE_AFTER_MS_MS));
                    break;
                }

                case EVENT_VIOLENT_EARTH:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_VIOLENT_EARTH, 500ms);
                        break;
                    }

                    StartViolentEarth();

                    events.ScheduleEvent(EVENT_EARTHQUAKE, milliseconds(VE_CAST_MS + QUAKE_AFTER_VE_END_MS));
                    break;
                }

                case EVENT_EARTHQUAKE:
                {
                    if (me->HasUnitState(UNIT_STATE_CASTING))
                    {
                        events.ScheduleEvent(EVENT_EARTHQUAKE, 200ms);
                        break;
                    }

                    DoEarthquake();

                    events.ScheduleEvent(EVENT_MASSIVE_STOMP, milliseconds(MS_AFTER_QUAKE_END_MS));
                    break;
                }

                case EVENT_HEROIC_AURA_FROM_STOMP:
                {
                    if (!ThranokHeroic())
                        break;
                    CastHeroicEarthquakeAura();
                    break;
                }

                case EVENT_HEROIC_AURA_FROM_QUAKE:
                {
                    if (!ThranokHeroic())
                        break;
                    CastHeroicEarthquakeAura();
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
void RegisterGuildVillageThranok()
{
    RegisterCreatureAI(boss_thranok_the_unyielding);
}
