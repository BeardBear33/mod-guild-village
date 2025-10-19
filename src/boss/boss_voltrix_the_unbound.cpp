#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "Config.h"
#include "PassiveAI.h"
#include "Opcodes.h"

// ============================
// Config: 10 / 25 pomocná volba
// ============================
static bool VoltrixHeroic()
{
    return sConfigMgr->GetOption<bool>("Voltrix.Heroic", false);
}
static uint32 R10_25(uint32 id10, uint32 id25)
{
    return VoltrixHeroic() ? id25 : id10;
}

// ============================
// Spells
// ============================
enum Spells
{
    // BASIC
    SPELL_GRAVITY_BOMB_10   = 63024,
    SPELL_GRAVITY_BOMB_25   = 64234,
    SPELL_SEARING_LIGHT_10  = 63018,
    SPELL_SEARING_LIGHT_25  = 65121,
    SPELL_TYMPANIC_TANTARUM = 62776,
    SPELL_ENRAGE            = 26662
};

enum Events
{
    EVENT_GRAVITY_BOMB = 1,
    EVENT_SEARING_LIGHT,
    EVENT_ENRAGE,
    EVENT_TANTRUM
};

// ============================
// Boss Voltrix
// ============================
struct boss_voltrix_the_unbound : public ScriptedAI
{
    boss_voltrix_the_unbound(Creature* c) : ScriptedAI(c), summons(me) { }

    EventMap events;
    SummonList summons;

    // Yelly
    void YellAggro()   { me->Yell("New toys? For me? I promise I won't break them this time!", LANG_UNIVERSAL, nullptr); }
    void YellTantrum() { me->Yell("NO! NO! NO! NO! NO!", LANG_UNIVERSAL, nullptr); }
    void YellSlay()    { me->Yell(urand(0,1) ? "I... I think I broke it." : "I guess it doesn't bend that way.", LANG_UNIVERSAL, nullptr); }
    void YellBerserk() { me->Yell("I'm tired of these toys. I don't want to play anymore!", LANG_UNIVERSAL, nullptr); }
    void YellDeath()   { me->Yell("You are bad... Toys... Very... Baaaaad", LANG_UNIVERSAL, nullptr); }

    // helper: 10/25
    uint32 SPELL_GRAVITY_BOMB() const { return R10_25(SPELL_GRAVITY_BOMB_10,  SPELL_GRAVITY_BOMB_25); }
    uint32 SPELL_SEARING_LIGHT() const { return R10_25(SPELL_SEARING_LIGHT_10, SPELL_SEARING_LIGHT_25); }

    void Reset() override
    {
        events.Reset();
        summons.DespawnAll();
        me->RemoveAllAuras();
        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        me->SetControlled(false, UNIT_STATE_STUNNED);
    }

    void JustReachedHome() override { me->setActive(false); }

    void JustEngagedWith(Unit*) override
    {
        me->setActive(true);
        me->CallForHelp(175.0f);
        YellAggro();

        // core rota
        events.RescheduleEvent(EVENT_GRAVITY_BOMB, 1s);
        events.RescheduleEvent(EVENT_TANTRUM, 1min);
        events.RescheduleEvent(EVENT_ENRAGE, 10min);
    }

    void KilledUnit(Unit* v) override
    {
        if (v->IsPlayer() && !urand(0,2))
            YellSlay();
    }

    void JustDied(Unit*) override
    {
        YellDeath();
        summons.DespawnAll();
    }

    void JustSummoned(Creature* cr) override { summons.Summon(cr); }
    void SummonedCreatureDespawn(Creature* cr) override { summons.Despawn(cr); }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        switch (events.ExecuteEvent())
        {
            case EVENT_GRAVITY_BOMB:
                me->CastCustomSpell(SPELL_GRAVITY_BOMB(), SPELLVALUE_MAX_TARGETS, 1, me, true);
                events.ScheduleEvent(EVENT_SEARING_LIGHT, 10s);
                break;

            case EVENT_SEARING_LIGHT:
                me->CastCustomSpell(SPELL_SEARING_LIGHT(), SPELLVALUE_MAX_TARGETS, 1, me, true);
                events.ScheduleEvent(EVENT_GRAVITY_BOMB, 10s);
                break;

            case EVENT_TANTRUM:
                YellTantrum();
                me->CastSpell(me, SPELL_TYMPANIC_TANTARUM, true);
                events.Repeat(1min);
                break;

            case EVENT_ENRAGE:
                YellBerserk();
                me->CastSpell(me, SPELL_ENRAGE, true);
                break;
        }

        DoMeleeAttackIfReady();
    }
};

// ============================
// Spell skripty
// ============================

class spell_voltrix_tympanic_tantrum : public SpellScript
{
    PrepareSpellScript(spell_voltrix_tympanic_tantrum);

    void FilterTargets(std::list<WorldObject*>& targets) { targets.remove_if(PlayerOrPetCheck()); }
    void RecalculateDamage() { if (GetHitUnit()) SetHitDamage(GetHitUnit()->CountPctFromMaxHealth(GetHitDamage())); }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_voltrix_tympanic_tantrum::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENEMY);
        OnHit += SpellHitFn(spell_voltrix_tympanic_tantrum::RecalculateDamage);
    }
};

// Gravity Bomb – nezasáhne aktuálního tank target (bez Acore::ObjectGUIDCheck)
class spell_voltrix_gravity_bomb : public SpellScript
{
    PrepareSpellScript(spell_voltrix_gravity_bomb);

    void SelectTarget(std::list<WorldObject*>& targets)
    {
        if (Unit* victim = GetCaster()->GetVictim())
        {
            auto vg = victim->GetGUID();
            targets.remove_if([&](WorldObject* obj)
            {
                return obj && obj->GetGUID() == vg;
            });
        }
    }
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_voltrix_gravity_bomb::SelectTarget, EFFECT_ALL, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// 63018/65121 – Searing Light: jen target filtr (bez Acore::ObjectGUIDCheck)
class spell_voltrix_searing_light : public SpellScript
{
    PrepareSpellScript(spell_voltrix_searing_light);

    void SelectTarget(std::list<WorldObject*>& targets)
    {
        if (Unit* victim = GetCaster()->GetVictim())
        {
            auto vg = victim->GetGUID();
            targets.remove_if([&](WorldObject* obj)
            {
                return obj && obj->GetGUID() == vg;
            });
        }
    }
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_voltrix_searing_light::SelectTarget, EFFECT_ALL, TARGET_UNIT_DEST_AREA_ENEMY);
    }
};

// ============================
// Registrace
// ============================
void RegisterGuildVillageVoltrix()
{
    RegisterCreatureAI(boss_voltrix_the_unbound);

    RegisterSpellScript(spell_voltrix_tympanic_tantrum);
    RegisterSpellScript(spell_voltrix_gravity_bomb);
    RegisterSpellScript(spell_voltrix_searing_light);
}
