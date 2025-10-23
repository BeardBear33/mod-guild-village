#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "Config.h"

// ============================
// Thalgron the Earthshaker — Koralon-style (open-world)
// ============================
//
// Schopnosti:
//  - Burning Breath (periodicky; boss se při tom „točí“ kolem dokola)
//  - Meteor Fists (proc aura -> dodatečné zásahy při mele)
//  - Flaming Cinder (náhodné cíle; PŘÍMO vystřelený missile, bez SpellScriptu)
//  - (HC) Enrage po 5 minutách, zapíná se přes Thalgron.Heroic = 1
// Bez instance logiky, bez SetInCombatWithZone.
//

// ============================
// Heroic toggle (config)
// ============================
static bool ThalgronHeroic()
{
    return sConfigMgr->GetOption<bool>("Thalgron.Heroic", false);
}

// --- Spelly (z Koralona) ---
enum Spells
{
    SPELL_BURNING_FURY               = 68168, // pasivní stacking aura
    SPELL_BURNING_BREATH             = 66665, // handled by spell_difficulty
    // SPELL_FLAMING_CINDER           = 66681,   // NEPOUŽÍVÁME (nahrazeno přímým missile)
    SPELL_FLAMING_CINDER_MISSILE     = 66682,  // přímo castneme z AI
    SPELL_METEOR_FISTS               = 66725,  // aura (dmg řeší aura script níže)
    SPELL_METEOR_FISTS_DAMAGE        = 66765,  // proc dmg
    SPELL_ENRAGE                     = 26662   // HC only
};

// --- Eventy ---
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
    void YellAggro()   { me->Yell("The flames will consume you!", LANG_UNIVERSAL, nullptr); }
    void YellBreath()  { me->Yell("Burn!", LANG_UNIVERSAL, nullptr); }
    void YellFists()   { me->Yell("My fists are meteors!", LANG_UNIVERSAL, nullptr); }
    void YellCinder()  { me->Yell("Cinders to ashes!", LANG_UNIVERSAL, nullptr); }
    void YellEnrage()  { me->Yell("Enough! Burn to ash!", LANG_UNIVERSAL, nullptr); }
    void YellDeath()   { me->Yell("The fire... fades...", LANG_UNIVERSAL, nullptr); }

    void Reset() override
    {
        events.Reset();
        rotateTimer = 0;
        me->RemoveAllAuras();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
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
        // „otáčení“ během Burning Breath (stejná finta jako u Koralona)
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
                rotateTimer = 1500; // rozběh rotace (jako v originálu)
                me->CastSpell(me, SPELL_BURNING_BREATH, false);
                events.Repeat(45s);
                break;

            case EVENT_METEOR_FISTS:
                YellFists();
                me->CastSpell(me, SPELL_METEOR_FISTS, true);
                events.Repeat(45s);
                break;

            case EVENT_FLAME_CINDER:
            {
                YellCinder();
                if (Unit* t = SelectTarget(SelectTargetMethod::Random, 0))
                    me->CastSpell(t->GetPositionX(), t->GetPositionY(), t->GetPositionZ(),
                                  SPELL_FLAMING_CINDER_MISSILE, true);
                events.Repeat(30s);
                break;
            }

            case EVENT_ENRAGE:
                YellEnrage();
                me->CastSpell(me, SPELL_ENRAGE, true);
                break;

            default:
                break;
        }

        DoMeleeAttackIfReady();
    }
};

// ============================
// AuraScript: Meteor Fists (ponecháno)
// ============================
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
    // Flaming Cinder SpellScript NEregistrovat (není potřeba)
    RegisterSpellScript(spell_thalgron_meteor_fists_aura);
}
