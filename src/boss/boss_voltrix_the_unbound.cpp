#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "Config.h"
#include "Random.h"

// =====================================
// Voltrix the Unbound (Guild Village Boss)
// =====================================
// Konfigurační klíč: Voltrix.Heroic = 1
// -> zapne "heroic" styl (Polarity Shift místo štítů)

static bool VoltrixHeroic()
{
    return sConfigMgr->GetOption<bool>("Voltrix.Heroic", false);
}

// ---------- Kouzla ----------
enum Spells
{
    SPELL_HEADCRACK                 = 35161,
    SPELL_REFLECTIVE_MAGIC_SHIELD   = 35158,
    SPELL_REFLECTIVE_DAMAGE_SHIELD  = 35159,
    SPELL_POLARITY_SHIFT            = 39096,
    SPELL_BERSERK                   = 26662,

    SPELL_NETHER_CHARGE_PASSIVE     = 35150,
    SPELL_SUMMON_NETHER_CHARGE_NE   = 35153,
    SPELL_SUMMON_NETHER_CHARGE_NW   = 35904,
    SPELL_SUMMON_NETHER_CHARGE_SE   = 35905,
    SPELL_SUMMON_NETHER_CHARGE_SW   = 35906,
};

// (enum Yells ponechaný kvůli čitelnosti – texty bereme z kódu přes me->Yell())
enum Yells
{
    SAY_AGGRO                      = 0,
    SAY_REFLECTIVE_MAGIC_SHIELD    = 1,
    SAY_REFLECTIVE_DAMAGE_SHIELD   = 2,
    SAY_KILL                       = 3,
    SAY_DEATH                      = 4
};

// =====================================
// Boss AI
// =====================================
struct boss_voltrix_the_unbound : public ScriptedAI
{
    boss_voltrix_the_unbound(Creature* creature) : ScriptedAI(creature) { }

    // ---- in-code hlášky (bez creature_text) ----
    void YellAggro()                 { me->Yell("You should split while you can.", LANG_UNIVERSAL, nullptr); }
    void YellMagicShield()           { me->Yell("Go ahead, gimme your best shot.  I can take it!", LANG_UNIVERSAL, nullptr); }
    void YellDamageShield()          { me->Yell("Think you can hurt me, huh?  Think I'm afraid a' you?", LANG_UNIVERSAL, nullptr); }
    void YellKillRandom()
    {
        if (urand(0, 1) == 0) me->Yell("Can't say I didn't warn you....", LANG_UNIVERSAL, nullptr);
        else                  me->Yell("Damn, I'm good!", LANG_UNIVERSAL, nullptr);
    }
    void YellDeath()                 { me->Yell("Bully!", LANG_UNIVERSAL, nullptr); }

    void JustEngagedWith(Unit* /*who*/) override
    {
        YellAggro();

        // Headcrack
        scheduler.Schedule(6s, [this](TaskContext context)
        {
            DoCastVictim(SPELL_HEADCRACK);
            context.Repeat(20s);
        });

        // Nether Charges
        scheduler.Schedule(10s, [this](TaskContext context)
        {
            uint32 spellId = RAND(SPELL_SUMMON_NETHER_CHARGE_NE,
                                  SPELL_SUMMON_NETHER_CHARGE_NW,
                                  SPELL_SUMMON_NETHER_CHARGE_SE,
                                  SPELL_SUMMON_NETHER_CHARGE_SW);
            DoCastAOE(spellId);
            context.Repeat(2400ms, 3600ms);
        });

        // Berserk po 3 minutách
        scheduler.Schedule(3min, [this](TaskContext /*context*/)
        {
            DoCastSelf(SPELL_BERSERK, true);
        });

        // Heroic-like režim (Polarity Shift) – vlastní SpellScripty NEPOTŘEBUJEME,
        // používáme mechanarové 'spell_capacitus_*' z jádra (mapované v DB).
        if (VoltrixHeroic())
        {
            scheduler.Schedule(15s, [this](TaskContext context)
            {
                DoCastSelf(SPELL_POLARITY_SHIFT, true);
                context.Repeat(30s);
            });
        }
        else
        {
            scheduler.Schedule(15s, [this](TaskContext context)
            {
                if (IsEvenNumber(context.GetRepeatCounter()))
                {
                    YellDamageShield();
                    DoCastSelf(SPELL_REFLECTIVE_DAMAGE_SHIELD);
                }
                else
                {
                    YellMagicShield();
                    DoCastSelf(SPELL_REFLECTIVE_MAGIC_SHIELD);
                }
                context.Repeat(20s);
            });
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            YellKillRandom();
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
    }

    void JustSummoned(Creature* summon) override
    {
        // pojistky pro „Nether Charge“ i mimo instanci
        summon->GetMotionMaster()->MoveRandom(30.0f);
        summon->CastSpell(summon, SPELL_NETHER_CHARGE_PASSIVE, true);
        summon->DespawnOrUnsummon(10000); // 10s v ms
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;
        scheduler.Update(diff, [this]{ DoMeleeAttackIfReady(); });
    }
};

// =====================================
// Lokální registrátor pro loader.cpp
// =====================================
void RegisterGuildVillageVoltrix()
{
    RegisterCreatureAI(boss_voltrix_the_unbound);
    // ŽÁDNÉ RegisterSpellScript – používáme mechanarové spell_capacitus_* z jádra
}
