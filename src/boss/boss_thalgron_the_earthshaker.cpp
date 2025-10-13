#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Config.h"
#include "Random.h"

// =====================================
// Thalgron the Earthshaker (open-world boss)
// Vychází z Krystalluse (Halls of Stone), ale BEZ instance logiky.
// - Hlášky jsou přímo v kódu (bez creature_text).
// - Heroic chování přepíná config: Thalgron.Heroic = 1
//   (zapne H spelly a přidá Ground Spike event)
// =====================================

// ---------- Config přepínač ----------
static bool ThalgronHeroic()
{
    return sConfigMgr->GetOption<bool>("Thalgron.Heroic", false);
}

// ---------- Spelly (z Krystalluse) ----------
enum Spells
{
    GROUND_SPIKE_H              = 59750, // jen heroic
    BOULDER_TOSS                = 50843,
    BOULDER_TOSS_H              = 59742,
    SHATTER                     = 50810,
    SHATTER_H                   = 61546,
    STOMP                       = 50868,
    STOMP_H                     = 59744,
    GROUND_SLAM                 = 50827,
    GROUND_SLAM_STONED_EFFECT   = 50812,
    SPELL_SHATTER_EFFECT        = 50811, // používá SpellScript v core
};

// ---------- Eventy ----------
enum Events
{
    EVENT_NONE = 0,
    EVENT_BOULDER,
    EVENT_STOMP,
    EVENT_GROUND_SLAM,
    EVENT_GROUND_SPIKE,   // jen pro heroic
    EVENT_SHATTER,
    EVENT_REMOVE_STONED,
};

// ---------- Hlášky (in-code) ----------
enum Yells
{
    SAY_AGGRO  = 0, // "Crush...."
    SAY_KILL   = 1, // "Ha.... ha... hah...."
    SAY_DEATH  = 2, // "Uuuuhhhhhhhhhh......"
    SAY_SHATTER= 3  // "Break.... you...."
};

// Pomocná volba spellu podle heroic přepínače
static uint32 HD(uint32 normalId, uint32 heroicId)
{
    return ThalgronHeroic() ? heroicId : normalId;
}

// =====================
// Boss AI
// =====================
struct boss_thalgron_the_earthshaker : public ScriptedAI
{
    boss_thalgron_the_earthshaker(Creature* creature) : ScriptedAI(creature) { }

    EventMap events;

    // ---- in-code hlášky (bez creature_text) ----
    void YellAggro()     { me->Yell("Crush....", LANG_UNIVERSAL, nullptr); }
    void YellKill()      { me->Yell("Ha.... ha... hah....", LANG_UNIVERSAL, nullptr); }
    void YellDeath()     { me->Yell("Uuuuhhhhhhhhhh......", LANG_UNIVERSAL, nullptr); }
    void YellShatter()   { me->Yell("Break.... you....", LANG_UNIVERSAL, nullptr); }

    void Reset() override
    {
        events.Reset();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        events.Reset();
        YellAggro();

        events.RescheduleEvent(EVENT_BOULDER, 8s);
        events.RescheduleEvent(EVENT_STOMP, 5s);
        events.RescheduleEvent(EVENT_GROUND_SLAM, 15s);

        if (ThalgronHeroic())
            events.RescheduleEvent(EVENT_GROUND_SPIKE, 10s);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        YellKill();
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    // Odebrání „stoned“ efektu po Shatteru všem hráčům v oblasti
    void RemoveStonedEffect()
    {
        if (Map* map = me->GetMap())
        {
            Map::PlayerList const& players = map->GetPlayers();
            for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
                if (Player* plr = it->GetSource())
                    if (plr->IsAlive())
                        plr->RemoveAura(GROUND_SLAM_STONED_EFFECT);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        switch (events.ExecuteEvent())
        {
            case EVENT_BOULDER:
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50.0f, true))
                    me->CastSpell(target, HD(BOULDER_TOSS, BOULDER_TOSS_H), false);

                events.Repeat(5s, 7s);
                break;
            }
            case EVENT_GROUND_SPIKE:
            {
                // jen heroic; útočí na aktuálního protivníka
                if (Unit* v = me->GetVictim())
                    me->CastSpell(v, GROUND_SPIKE_H, false);

                events.Repeat(8s, 11s);
                break;
            }
            case EVENT_STOMP:
            {
                me->CastSpell(me, HD(STOMP, STOMP_H), false);
                events.Repeat(13s, 18s);
                break;
            }
            case EVENT_GROUND_SLAM:
            {
                // Ground Slam -> po 8s přejde do Shatter
                if (Unit* v = me->GetVictim())
                    me->CastSpell(v, GROUND_SLAM, true);

                events.DelayEvents(10s);               // krátká pauza ostatních eventů
                events.RescheduleEvent(EVENT_SHATTER, 8s);
                events.Repeat(10s, 13s);
                break;
            }
            case EVENT_SHATTER:
            {
                me->CastSpell((Unit*)nullptr, HD(SHATTER, SHATTER_H), false);
                YellShatter();
                events.RescheduleEvent(EVENT_REMOVE_STONED, 1500ms);
                break;
            }
            case EVENT_REMOVE_STONED:
            {
                RemoveStonedEffect();
                break;
            }
        }

        DoMeleeAttackIfReady();
    }
};

// ===============================
// Lokální registrátor pro loader
// ===============================
void RegisterGuildVillageThalgron()
{
    RegisterCreatureAI(boss_thalgron_the_earthshaker);
}
