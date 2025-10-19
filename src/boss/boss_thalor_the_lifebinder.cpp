#include "CreatureScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "Config.h"
#include "Random.h"

// =====================================
// Thalor the Lifebinder
// - Flux (stacky) -> Solar Flare (škáluje dle stacků)
// - Unstable Sun Beams: 2x paprsek s aurami, po chvíli výbuch a despawn
// - Hlášky jsou přímo v kódu (bez creature_text)
// - Přepínač 10/25 přes config: Thalor.Heroic = 0/1
// =====================================

// ---------- Config přepínač ----------
static bool ThalorHeroic()
{
    return sConfigMgr->GetOption<bool>("Thalor.Heroic", false);
}

// ---------- Spelly ----------
enum Spells
{
    SPELL_BRIGHTLEAF_FLUX            = 62239,
    SPELL_SOLAR_FLARE_10             = 62240,
    SPELL_SOLAR_FLARE_25             = 64087,
    SPELL_UNSTABLE_SUN_BEAM_AURA     = 62211,
    SPELL_PHOTOSYNTHESIS             = 62209,
    SPELL_UNSTABLE_SUN_DAMAGE_10     = 62217,
    SPELL_UNSTABLE_SUN_DAMAGE_25     = 62922
};

// ---------- NPC ----------
enum Npcs
{
    NPC_UNSTABLE_SUN_BRIGHTLEAF      = 33050
};

// ---------- Eventy ----------
enum Events
{
    EVENT_NONE = 0,
    EVENT_FLUX,
    EVENT_SOLAR_FLARE,
    EVENT_UNSTABLE_SUN_BEAM,
    EVENT_DESPAWN_SUN_BEAM
};

// ---------- Hlášky (in-code) ----------
enum Yells
{
    SAY_AGGRO = 0, 
    SAY_SLAY  = 1,
    SAY_DEATH = 2
};

// Pomocná volba 10/25 verze
static uint32 R10_25(uint32 id10, uint32 id25)
{
    return ThalorHeroic() ? id25 : id10;
}

// =====================
// Boss AI
// =====================
struct boss_thalor_the_lifebinder : public ScriptedAI
{
    boss_thalor_the_lifebinder(Creature* creature) : ScriptedAI(creature), summons(me) { }

    EventMap events;
    SummonList summons;

    // ---- in-code hlášky ----
    void YellAggro() { me->Yell("Matron, the Conservatory has been breached!", LANG_UNIVERSAL, nullptr); }
    void YellSlay()
    {
        if (urand(0, 1) == 0)
            me->Yell("Fertilizer.", LANG_UNIVERSAL, nullptr);
        else
            me->Yell("Your corpse will nourish the soil.", LANG_UNIVERSAL, nullptr);
    }
    void YellDeath() { me->Yell("Matron, one has fallen!", LANG_UNIVERSAL, nullptr); }

    void Reset() override
    {
        events.Reset();
        summons.DespawnAll();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        events.Reset();
        YellAggro();

        // Timery
        events.ScheduleEvent(EVENT_FLUX,               10s);
        events.ScheduleEvent(EVENT_SOLAR_FLARE,         5s);
        events.ScheduleEvent(EVENT_UNSTABLE_SUN_BEAM,   8s);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->IsPlayer())
            YellSlay();
    }

    void JustDied(Unit* /*killer*/) override
    {
        YellDeath();
        summons.DespawnAll();
    }

    void JustSummoned(Creature* cr) override
    {
        if (cr->GetEntry() == NPC_UNSTABLE_SUN_BRIGHTLEAF)
        {
            cr->CastSpell(cr, SPELL_UNSTABLE_SUN_BEAM_AURA, true);
            cr->CastSpell(cr, SPELL_PHOTOSYNTHESIS, true);
        }
        summons.Summon(cr);
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
            case EVENT_FLUX:
            {
                if (Aura* aur = me->AddAura(SPELL_BRIGHTLEAF_FLUX, me))
                    aur->SetStackAmount(urand(1, 10));
                events.Repeat(10s);
                break;
            }
            case EVENT_SOLAR_FLARE:
            {
                if (Aura* aur = me->GetAura(SPELL_BRIGHTLEAF_FLUX))
                {
                    me->CastCustomSpell(R10_25(SPELL_SOLAR_FLARE_10, SPELL_SOLAR_FLARE_25),
                                        SPELLVALUE_MAX_TARGETS, aur->GetStackAmount(), me, false);
                    me->RemoveAura(aur);
                }
                events.Repeat(15s);
                break;
            }
            case EVENT_UNSTABLE_SUN_BEAM:
            {
                events.ScheduleEvent(EVENT_DESPAWN_SUN_BEAM, 15s);

                float x = me->GetPositionX();
                float y = me->GetPositionY();
                float z = me->GetPositionZ();

                auto spawn = [&](float ox, float oy)
                {
                    me->SummonCreature(NPC_UNSTABLE_SUN_BRIGHTLEAF,
                                       x + ox, y + oy, me->GetMapHeight(x + ox, y + oy, z),
                                       0.0f, TEMPSUMMON_TIMED_DESPAWN, 16000);
                };

                spawn(0.0f, 0.0f);
                spawn(8.0f, 8.0f);

                events.Repeat(20s);
                break;
            }
            case EVENT_DESPAWN_SUN_BEAM:
            {
                for (SummonList::iterator i = summons.begin(); i != summons.end();)
                {
                    Creature* s = ObjectAccessor::GetCreature(*me, *i);
                    ++i;
                    if (s && s->GetEntry() == NPC_UNSTABLE_SUN_BRIGHTLEAF)
                        s->CastSpell(s, R10_25(SPELL_UNSTABLE_SUN_DAMAGE_10, SPELL_UNSTABLE_SUN_DAMAGE_25), false);
                }
                summons.DespawnEntry(NPC_UNSTABLE_SUN_BRIGHTLEAF);
                break;
            }
        }

        DoMeleeAttackIfReady();
    }
};

// ===============================
// Lokální registrátor pro loader
// ===============================
void RegisterGuildVillageThalor()
{
    RegisterCreatureAI(boss_thalor_the_lifebinder);
}
