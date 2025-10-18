#include "CreatureScript.h"
#include "ScriptedCreature.h"
#include "Config.h"

// =====================================
// Thranok the Unyielding (open-world)
// =====================================
//
// Texty jsou přímo v kódu – není potřeba creature_text!
// Crystal Spikes kompletně odstraněny.
// =====================================

static bool ThranokHeroic()
{
    return sConfigMgr->GetOption<bool>("Thranok.Heroic", false);
}

enum Spells
{
    SPELL_SPELL_REFLECTION           = 47981,
    SPELL_TRAMPLE                    = 48016,
    SPELL_FRENZY                     = 48017,
    SPELL_SUMMON_CRYSTALLINE_TANGLER = 61564
};

enum Events
{
    EVENT_THRANOK_TRAMPLE = 1,
    EVENT_THRANOK_SPELL_REFLECTION,
    EVENT_THRANOK_SUMMON,
    EVENT_THRANOK_HEALTH,
    EVENT_KILL_TALK
};

// =====================
// Boss AI (open-world)
// =====================
struct boss_thranok_the_unyielding : public ScriptedAI
{
    boss_thranok_the_unyielding(Creature* creature) : ScriptedAI(creature) { }

    EventMap events;

    // ====== in-code hlášky ======
    void YellAggro()   { me->Yell("Noo!",  LANG_UNIVERSAL, nullptr); }
    void YellDeath()   { me->Yell("Aaggh!", LANG_UNIVERSAL, nullptr); }
    void YellReflect() { me->Yell("Back!", LANG_UNIVERSAL, nullptr); }
    void YellKill()    { me->Yell("Kill!", LANG_UNIVERSAL, nullptr); }
    void EmoteFrenzy() { me->TextEmote("%s goes into a frenzy!", me); }

    void Reset() override
    {
        events.Reset();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        YellAggro();

        events.ScheduleEvent(EVENT_THRANOK_TRAMPLE, 10s);
        events.ScheduleEvent(EVENT_THRANOK_SPELL_REFLECTION, 30s);
        events.ScheduleEvent(EVENT_THRANOK_HEALTH, 1s);

        if (ThranokHeroic())
            events.ScheduleEvent(EVENT_THRANOK_SUMMON, 17s);
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        // omezíme spam kill hlášky
        if (events.GetNextEventTime(EVENT_KILL_TALK) == 0)
        {
            YellKill();
            events.ScheduleEvent(EVENT_KILL_TALK, 6s);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_THRANOK_HEALTH:
                {
                    if (me->HealthBelowPct(26))
                    {
                        me->CastSpell(me, SPELL_FRENZY, true);
                        EmoteFrenzy();
                        // neschedulujeme znovu; jednorázově
                        break;
                    }
                    events.ScheduleEvent(EVENT_THRANOK_HEALTH, 1s);
                    break;
                }
                case EVENT_THRANOK_TRAMPLE:
                {
                    me->CastSpell(me, SPELL_TRAMPLE, false);
                    events.ScheduleEvent(EVENT_THRANOK_TRAMPLE, 10s);
                    break;
                }
                case EVENT_THRANOK_SPELL_REFLECTION:
                {
                    YellReflect();
                    me->CastSpell(me, SPELL_SPELL_REFLECTION, false);
                    events.ScheduleEvent(EVENT_THRANOK_SPELL_REFLECTION, 30s);
                    break;
                }
                case EVENT_THRANOK_SUMMON:
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::MinDistance, 0, 50.0f, true))
                        me->CastSpell(target, SPELL_SUMMON_CRYSTALLINE_TANGLER, true);
                    events.ScheduleEvent(EVENT_THRANOK_SUMMON, 17s);
                    break;
                }
            }
        }

        DoMeleeAttackIfReady();
    }
};

// ===============================
// Lokální registrátor pro loader
// ===============================
void RegisterGuildVillageThranok()
{
    RegisterCreatureAI(boss_thranok_the_unyielding);
}
