#include "CreatureScript.h"
#include "ScriptedCreature.h"
#include "PassiveAI.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "Config.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Random.h"
#include <cmath>

// =====================================
// Thranok the Unyielding (open-world)
// =====================================
//
// Texty jsou přímo v kódu – není potřeba creature_text!
// =====================================

static bool ThranokHeroic()
{
    return sConfigMgr->GetOption<bool>("Thranok.Heroic", false);
}

enum eEnums
{
    SPELL_CRYSTAL_SPIKES             = 47958,
    SPELL_CRYSTAL_SPIKE_DAMAGE       = 47944,
    SPELL_CRYSTAL_SPIKE_PREVISUAL    = 50442,
    SPELL_SPELL_REFLECTION           = 47981,
    SPELL_TRAMPLE                    = 48016,
    SPELL_FRENZY                     = 48017,
    SPELL_SUMMON_CRYSTALLINE_TANGLER = 61564,
    SPELL_CRYSTAL_CHAINS             = 47698
};

enum Events
{
    EVENT_THRANOK_CRYSTAL_SPIKES   = 1,
    EVENT_THRANOK_TRAMPLE          = 2,
    EVENT_THRANOK_SPELL_REFLECTION = 3,
    EVENT_THRANOK_SUMMON           = 4,
    EVENT_THRANOK_HEALTH           = 5,
    EVENT_THRANOK_SUMMON_SPIKES    = 6,
    EVENT_KILL_TALK                = 7
};

enum Misc
{
    NPC_CRYSTAL_SPIKE = 27099,
    GO_CRYSTAL_SPIKE  = 188537
};

// =====================
// Boss AI (open-world)
// =====================
struct boss_thranok_the_unyielding : public ScriptedAI
{
    boss_thranok_the_unyielding(Creature* creature) : ScriptedAI(creature), _summons(me) { }

    uint8 _spikesCount = 0;
    SummonList _summons;

    // ====== interní helpery pro texty ======
    void YellAggro()          { me->Yell("Noo!", LANG_UNIVERSAL, nullptr); }
    void YellDeath()          { me->Yell("Aaggh!", LANG_UNIVERSAL, nullptr); }
    void YellReflect()        { me->Yell("Back!", LANG_UNIVERSAL, nullptr); }
    void YellSpikes()         { me->Yell("Bleed!", LANG_UNIVERSAL, nullptr); }
    void YellKill()           { me->Yell("Kill!", LANG_UNIVERSAL, nullptr); }
    void EmoteFrenzy()        { me->TextEmote("%s goes into a frenzy!", me); }

    // =======================================
    void Reset() override
    {
        _spikesCount = 0;
        _summons.DespawnAll();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        YellAggro();

        events.ScheduleEvent(EVENT_THRANOK_CRYSTAL_SPIKES, 12s);
        events.ScheduleEvent(EVENT_THRANOK_TRAMPLE, 10s);
        events.ScheduleEvent(EVENT_THRANOK_SPELL_REFLECTION, 30s);
        events.ScheduleEvent(EVENT_THRANOK_HEALTH, 1s);

        if (ThranokHeroic())
            events.ScheduleEvent(EVENT_THRANOK_SUMMON, 17s);
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
        _summons.DespawnAll();
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        if (events.GetNextEventTime(EVENT_KILL_TALK) == 0)
        {
            YellKill();
            events.ScheduleEvent(EVENT_KILL_TALK, 6s);
        }
    }

    void JustSummoned(Creature* summon) override
    {
        _summons.Summon(summon);
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
                case EVENT_THRANOK_CRYSTAL_SPIKES:
                {
                    YellSpikes();
                    me->CastSpell(me, SPELL_CRYSTAL_SPIKES, false);
                    _spikesCount = 0;
                    events.ScheduleEvent(EVENT_THRANOK_SUMMON_SPIKES, 300ms);
                    events.ScheduleEvent(EVENT_THRANOK_CRYSTAL_SPIKES, 20s);
                    break;
                }
                case EVENT_THRANOK_SUMMON_SPIKES:
                {
                    if (++_spikesCount > 9)
                        break;

                    for (uint8 i = 0; i < 4; ++i)
                    {
                        float o = rand_norm() * 2.0f * static_cast<float>(M_PI);
                        float r = 5.0f * _spikesCount;
                        float x = me->GetPositionX() + r * std::cos(o);
                        float y = me->GetPositionY() + r * std::sin(o);
                        float h = me->GetMapHeight(x, y, me->GetPositionZ());

                        if (h != INVALID_HEIGHT)
                            me->SummonCreature(NPC_CRYSTAL_SPIKE, x, y, h, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 7000);
                    }
                    events.ScheduleEvent(EVENT_THRANOK_SUMMON_SPIKES, 200ms);
                    break;
                }
            }
        }

        DoMeleeAttackIfReady();
    }
};

// =======================
// Crystal Spike helper
// =======================
struct npc_crystal_spike : public NullCreatureAI
{
    npc_crystal_spike(Creature* c) : NullCreatureAI(c) { }

    int32 _damageTimer = 0;
    ObjectGuid _gameObjectGUID;

    void Reset() override
    {
        if (GameObject* go = me->SummonGameObject(GO_CRYSTAL_SPIKE, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(),
                                                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3500))
        {
            _gameObjectGUID = go->GetGUID();
        }
        _damageTimer = 1;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_damageTimer)
        {
            _damageTimer += diff;
            if (_damageTimer >= 2000)
            {
                if (GameObject* go = ObjectAccessor::GetGameObject(*me, _gameObjectGUID))
                    go->SetGoState(GO_STATE_ACTIVE);

                me->CastSpell(me, SPELL_CRYSTAL_SPIKE_DAMAGE, false);
                _damageTimer = 0;
            }
        }
    }
};

// ===============================
// Lokální registrátor pro loader
// ===============================
void RegisterGuildVillageThranok()
{
    RegisterCreatureAI(boss_thranok_the_unyielding);
    RegisterCreatureAI(npc_crystal_spike);
}
