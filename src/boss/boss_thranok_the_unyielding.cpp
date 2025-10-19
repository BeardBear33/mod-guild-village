#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "Config.h"

// =====================================
// Thranok the Unyielding
// =====================================
// - Heroic toggle: Thranok.Heroic = 1  (25-man hodnoty kouzel)
// - Schopnosti: Rock Shards, Stomp, Impale, Berserk (5m)

static bool ThranokHeroic()
{
    return sConfigMgr->GetOption<bool>("Thranok.Heroic", false);
}
static inline uint32 R10_25(uint32 id10, uint32 id25) { return ThranokHeroic() ? id25 : id10; }

// ---------- Spells ----------
enum Spells
{
    SPELL_ROCK_SHARDS             = 58678,

    SPELL_ROCK_SHARDS_DAMAGE_10   = 58695,
    SPELL_ROCK_SHARDS_DAMAGE_25   = 60883,

    SPELL_STOMP_10                = 58663,
    SPELL_STOMP_25                = 60880,
    SPELL_IMPALE_10               = 58666,
    SPELL_IMPALE_25               = 60882,

    SPELL_BERSERK                 = 47008
};

enum Events
{
    EVENT_ROCK_SHARDS = 1,
    EVENT_STOMP,
    EVENT_IMPALE,
    EVENT_BERSERK
};

// ---------- (volitelné) hlášky ----------
struct ThranokYellsA
{
    static void Aggro(Creature* me)   { me->Yell("Stone will break you!", LANG_UNIVERSAL, nullptr); }
    static void Stomp(Creature* me)   { me->Yell("Tremble!", LANG_UNIVERSAL, nullptr); }
    static void Berserk(Creature* me) { me->Yell("Crush them all!", LANG_UNIVERSAL, nullptr); }
    static void Slay(Creature* me)    { me->Yell("Ground to dust.", LANG_UNIVERSAL, nullptr); }
    static void Death(Creature* me)   { me->Yell("…stone cracks…", LANG_UNIVERSAL, nullptr); }
};

// =====================================
// Boss AI
// =====================================
struct boss_thranok_the_unyielding : public ScriptedAI
{
    boss_thranok_the_unyielding(Creature* c) : ScriptedAI(c) {}

    EventMap events;

    void Reset() override
    {
        events.Reset();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        ThranokYellsA::Aggro(me);

        // Timery
        events.ScheduleEvent(EVENT_ROCK_SHARDS, 15s);
        events.ScheduleEvent(EVENT_STOMP,       45s);
        events.ScheduleEvent(EVENT_BERSERK,      5min);

        me->setActive(true);
    }

    void KilledUnit(Unit* v) override
    {
        if (v->IsPlayer())
            ThranokYellsA::Slay(me);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ThranokYellsA::Death(me);
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
            case EVENT_ROCK_SHARDS:
            {
                // Vybrat náhodný cíl a cast trigger 58678 (viz SpellScript níže).
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    me->CastSpell(target, SPELL_ROCK_SHARDS, false);

                events.Repeat(15s);
                break;
            }

			case EVENT_STOMP:
			{
				if (Unit* v = me->GetVictim())
				{
					std::string msg = std::string(me->GetName()) + " stomps the ground at " + v->GetName() + "!";
					me->TextEmote(msg, v, /*isBossEmote=*/true);
			
					ThranokYellsA::Stomp(me);
					me->CastSpell(v, R10_25(SPELL_STOMP_10, SPELL_STOMP_25), false);
				}
			
				events.Repeat(45s);
				events.ScheduleEvent(EVENT_IMPALE, 3s);
				break;
			}


            case EVENT_IMPALE:
            {
                me->CastSpell(me->GetVictim(), R10_25(SPELL_IMPALE_10, SPELL_IMPALE_25), false);
                break;
            }

            case EVENT_BERSERK:
            {
                ThranokYellsA::Berserk(me);
                me->CastSpell(me, SPELL_BERSERK, true);
                break;
            }
        }

        DoMeleeAttackIfReady();
    }
};

// ===============================
// SpellScript: Rock Shards
// ===============================
class spell_thranok_rock_shards : public SpellScript
{
    PrepareSpellScript(spell_thranok_rock_shards);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ROCK_SHARDS_DAMAGE_10, SPELL_ROCK_SHARDS_DAMAGE_25 });
    }

    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);

        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        uint32 dmgId = R10_25(SPELL_ROCK_SHARDS_DAMAGE_10, SPELL_ROCK_SHARDS_DAMAGE_25);
        caster->CastSpell(target, dmgId, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_thranok_rock_shards::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// ===============================
// Registrátor
// ===============================
void RegisterGuildVillageThranok()
{
    RegisterCreatureAI(boss_thranok_the_unyielding);
    RegisterSpellScript(spell_thranok_rock_shards);
}
