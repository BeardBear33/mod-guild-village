#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "Config.h"

// ============================
// Thalgron the Earthshaker
// ============================
//
// Schopnosti:
//  - Burning Breath (periodicky; boss se při tom „točí“ kolem dokola)
//  - Meteor Fists (proc aura -> dodatečné zásahy při mele)
//  - Flaming Cinder (náhodné cíle; dummy -> missile)
//  - (HC) Enrage po 5 minutách, zapíná se přes Thalgron.Heroic = 1
//
//
// ============================
// Heroic toggle (config)
// ============================
static bool ThalgronHeroic()
{
    return sConfigMgr->GetOption<bool>("Thalgron.Heroic", false);
}

// --- Spelly (převzaté z Koralona) ---
enum Spells
{
    SPELL_BURNING_FURY               = 68168,
    SPELL_BURNING_BREATH             = 66665,
    SPELL_FLAMING_CINDER             = 66681,
    SPELL_FLAMING_CINDER_DUMMY       = 66690,
    SPELL_FLAMING_CINDER_MISSILE     = 66682,
    SPELL_METEOR_FISTS               = 66725,
    SPELL_METEOR_FISTS_DAMAGE        = 66765,
    SPELL_ENRAGE                     = 26662  // HC only
};

// --- Eventy
enum Events
{
    EVENT_BURNING_BREATH = 1,
    EVENT_METEOR_FISTS   = 2,
    EVENT_FLAME_CINDER   = 3,
    EVENT_ENRAGE         = 4 // HC only
};

struct boss_thalgron_the_earthshaker : public ScriptedAI
{
    boss_thalgron_the_earthshaker(Creature* c) : ScriptedAI(c) { }

    EventMap events;
    uint32 rotateTimer = 0;

    // volitelné hlášky (in-code)
    void YellAggro()    { me->Yell("The flames will consume you!", LANG_UNIVERSAL, nullptr); }
    void YellBreath()   { me->Yell("Burn!", LANG_UNIVERSAL, nullptr); }
    void YellFists()    { me->Yell("My fists are meteors!", LANG_UNIVERSAL, nullptr); }
    void YellCinder()   { me->Yell("Cinders to ashes!", LANG_UNIVERSAL, nullptr); }
    void YellEnrage()  { me->Yell("Enough! Burn to ash!", LANG_UNIVERSAL, nullptr); }
    void YellDeath()    { me->Yell("The fire... fades...", LANG_UNIVERSAL, nullptr); }

    void Reset() override
    {
        events.Reset();
        rotateTimer = 0;
        me->RemoveAllAuras();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        // pasivní „růst“
        me->CastSpell(me, SPELL_BURNING_FURY, true);

        YellAggro();

        // plánování schopností
        events.ScheduleEvent(EVENT_BURNING_BREATH, 10s);
        events.ScheduleEvent(EVENT_METEOR_FISTS,   30s);
        events.ScheduleEvent(EVENT_FLAME_CINDER,   20s);

        // HC: Enrage po 2 minutách
        if (ThalgronHeroic())
            events.ScheduleEvent(EVENT_ENRAGE, 2min);

        me->setActive(true);
        me->CallForHelp(175.0f);
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    void UpdateAI(uint32 diff) override
    {
        // „otáčení“ během Burning Breath
        if (rotateTimer)
        {
            me->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, 0);
            rotateTimer += diff;
            if (rotateTimer >= 3000)
            {
                if (!me->HasUnitMovementFlag(MOVEMENTFLAG_LEFT))
                {
                    me->SetUnitMovementFlags(MOVEMENTFLAG_LEFT);
                    me->SendMovementFlagUpdate();
                    rotateTimer = 1;
                    return;
                }
                else
                {
                    me->RemoveUnitMovementFlag(MOVEMENTFLAG_LEFT);
                    me->SendMovementFlagUpdate();
                    rotateTimer = 0;
                    return;
                }
            }
        }

        if (!UpdateVictim())
            return;

        events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        switch (events.ExecuteEvent())
        {
            case EVENT_BURNING_BREATH:
                YellBreath();
                rotateTimer = 1500;
                me->CastSpell(me, SPELL_BURNING_BREATH, false);
                events.Repeat(45s);
                break;

            case EVENT_METEOR_FISTS:
                YellFists();
                me->CastSpell(me, SPELL_METEOR_FISTS, true);
                events.Repeat(45s);
                break;

            case EVENT_FLAME_CINDER:
                YellCinder();
                me->CastSpell(me, SPELL_FLAMING_CINDER, true);
                events.Repeat(30s);
                break;

            case EVENT_ENRAGE:
                YellEnrage();
                me->CastSpell(me, SPELL_ENRAGE, true);
                // žádné Repeat – je to jednorázové
                break;

            default:
                break;
        }

        DoMeleeAttackIfReady();
    }
};

// ============================
// SpellScripty
// ============================

// Flaming Cinder: dummy -> vystřel missile na pozici cíle
class spell_thalgron_flaming_cinder : public SpellScript
{
    PrepareSpellScript(spell_thalgron_flaming_cinder);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FLAMING_CINDER_MISSILE });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            GetCaster()->CastSpell(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), SPELL_FLAMING_CINDER_MISSILE, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_thalgron_flaming_cinder::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// Meteor Fists aura: každý proc přidá dodatečný zásah
class spell_thalgron_meteor_fists_aura : public AuraScript
{
    PrepareAuraScript(spell_thalgron_meteor_fists_aura);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_METEOR_FISTS_DAMAGE });
    }

    void TriggerFists(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();
        GetTarget()->CastSpell(eventInfo.GetProcTarget(), SPELL_METEOR_FISTS_DAMAGE, true, nullptr, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_thalgron_meteor_fists_aura::TriggerFists, EFFECT_0, SPELL_AURA_DUMMY);
    }
};

// ===============================
// Lokální registrátor pro loader
// ===============================
void RegisterGuildVillageThalgron()
{
    RegisterCreatureAI(boss_thalgron_the_earthshaker);
    RegisterSpellScript(spell_thalgron_flaming_cinder);
    RegisterSpellScript(spell_thalgron_meteor_fists_aura);
}
