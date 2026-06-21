#include "HeroClass.h"
#include "Skills.h"
#include <algorithm>

void HeroClassRegistry::init()
{
    m_classes.clear();

    int id = 1;

    auto addClass = [&](FactionId f, const char* name, SpecialtyType spec,
                        const char* specDesc, bool atk, bool lp, bool bp,
                        bool dp, bool np, bool fp, bool flp,
                        std::vector<int> pool) {
        HeroClassDef c;
        c.id = id++; c.name = name; c.faction = f;
        c.specialty = spec; c.specialtyDesc = specDesc;
        c.scalesAttack = atk; c.scalesLightPower = lp;
        c.scalesBloodPower = bp; c.scalesDeathPower = dp;
        c.scalesNaturePower = np; c.scalesForgePower = fp;
        c.scalesFleshPower = flp;
        c.skillPool = pool;
        m_classes.push_back(c);
    };

    using F = FactionId;
    using S = SpecialtyType;

    // ── HOLY ORDER ─────────────────────────────────────────────────────────────
    addClass(F::HolyOrder, "Inquisitor", S::HeresyDetection,
        "Nullifies one enemy spell per battle",
        false, true, false, false, false, false, false,
        {SkillID::LIGHT_MAGIC, SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::DESPERATION, SkillID::LEADERSHIP});

    addClass(F::HolyOrder, "Confessor", S::LastRites,
        "Enemy kills charge nearby Holy allies' Desperation meter by +20",
        false, true, false, false, false, false, false,
        {SkillID::LIGHT_MAGIC, SkillID::FIRST_AID, SkillID::INSPIRATION, SkillID::LEADERSHIP, SkillID::SCOUTING});

    addClass(F::HolyOrder, "Crusader", S::Veteran,
        "Units gain +1 Attack and Defense per previous battle this campaign",
        true, false, false, false, false, false, false,
        {SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::LOGISTICS});

    addClass(F::HolyOrder, "Flagellant Marshal", S::BloodPenance,
        "All units +2 ATK at battle start; hero loses 5 HP per round from round 2",
        true, true, false, false, false, false, false,
        {SkillID::DESPERATION, SkillID::INSPIRATION, SkillID::OFFENSE, SkillID::LIGHT_MAGIC, SkillID::TACTICS});

    // ── BLOODSWORN ─────────────────────────────────────────────────────────────
    addClass(F::Bloodsworn, "Blood Prince", S::Feast,
        "Drain 2 HP from largest friendly unit each round to heal hero",
        false, false, true, false, false, false, false,
        {SkillID::BLOOD_MAGIC, SkillID::BLOOD_POOL, SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::SCOUTING});

    addClass(F::Bloodsworn, "Crimson Mage", S::Exsanguinate,
        "One Blood Power spell per battle costs no pool",
        false, false, true, false, false, false, false,
        {SkillID::BLOOD_MAGIC, SkillID::BLOOD_POOL, SkillID::OFFENSE, SkillID::LIGHT_MAGIC, SkillID::ARCHERY});

    addClass(F::Bloodsworn, "Thrall Master", S::Swarm,
        "All Fledglings start battle at ascension threshold",
        true, false, false, false, false, false, false,
        {SkillID::LEADERSHIP, SkillID::OFFENSE, SkillID::TACTICS, SkillID::BLOOD_POOL, SkillID::LOGISTICS});

    addClass(F::Bloodsworn, "Assassin Lord", S::Predator,
        "Permanent +1 Attack for each enemy hero killed this campaign",
        true, false, true, false, false, false, false,
        {SkillID::OFFENSE, SkillID::BLOOD_MAGIC, SkillID::TACTICS, SkillID::SCOUTING, SkillID::BLOOD_POOL});

    // ── THORNKIN ───────────────────────────────────────────────────────────────
    addClass(F::Thornkin, "Beastcaller", S::WildGrowth,
        "Dead companions respawn as spirit versions at half strength",
        false, false, false, false, true, false, false,
        {SkillID::NATURE_MAGIC, SkillID::SYMBIOSIS, SkillID::FIRST_AID, SkillID::LEADERSHIP, SkillID::SCOUTING});

    addClass(F::Thornkin, "Pathfinder", S::Overgrowth,
        "Place 3 forest tiles on combat map before battle starts",
        true, false, false, false, false, false, false,
        {SkillID::LOGISTICS, SkillID::SCOUTING, SkillID::TACTICS, SkillID::NATURE_MAGIC, SkillID::OFFENSE});

    addClass(F::Thornkin, "Stormbark", S::LightningRod,
        "First enemy spell each battle is redirected back at the caster",
        false, false, false, false, true, false, false,
        {SkillID::NATURE_MAGIC, SkillID::OFFENSE, SkillID::LIGHT_MAGIC, SkillID::TACTICS, SkillID::SYMBIOSIS});

    addClass(F::Thornkin, "Warsinger", S::Harmony,
        "All bonded pairs gain +1 Attack and Defense while hero is alive",
        true, false, false, false, true, false, false,
        {SkillID::LEADERSHIP, SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::NATURE_MAGIC, SkillID::SYMBIOSIS});

    // ── ETERNAL EMPIRE ─────────────────────────────────────────────────────────
    addClass(F::EternalEmpire, "Death Herald", S::SoulHarvest,
        "Enemy unit kills heal hero for 5 HP per unit killed",
        false, false, false, true, false, false, false,
        {SkillID::DEATH_MAGIC, SkillID::ETERNAL_CMD, SkillID::NECROMANCY, SkillID::LEADERSHIP, SkillID::FIRST_AID});

    addClass(F::EternalEmpire, "Iron General", S::EternalLegion,
        "Reraised units retain their formation bonuses",
        true, false, false, false, false, false, false,
        {SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::ETERNAL_CMD});

    addClass(F::EternalEmpire, "Lich", S::Phylactery,
        "Hero respawns next battle at half stats if killed — once per campaign",
        false, false, false, true, false, false, false,
        {SkillID::DEATH_MAGIC, SkillID::ETERNAL_CMD, SkillID::NECROMANCY, SkillID::TACTICS, SkillID::MYSTICISM});

    addClass(F::EternalEmpire, "Grave Diplomat", S::NegotiatedWeakness,
        "Reveals enemy hero specialty before battle begins",
        true, false, false, true, false, false, false,
        {SkillID::ETERNAL_CMD, SkillID::LEADERSHIP, SkillID::DEATH_MAGIC, SkillID::NECROMANCY, SkillID::SCOUTING});

    // ── CRIMSON WARDENS ────────────────────────────────────────────────────────
    addClass(F::CrimsonWardens, "Warden Captain", S::CoordinatedStrike,
        "Marked target takes bonus damage from every attacker same round",
        true, false, false, false, false, false, false,
        {SkillID::WARDEN_MARK, SkillID::OFFENSE, SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::NECROMANCY});

    addClass(F::CrimsonWardens, "Blood Sage", S::Elixir,
        "Once per battle fully heal one friendly unit",
        false, false, true, false, false, false, false,
        {SkillID::BLOOD_MAGIC, SkillID::FIRST_AID, SkillID::WARDEN_MARK, SkillID::MYSTICISM, SkillID::SCOUTING});

    addClass(F::CrimsonWardens, "Oathmaster", S::BloodWeb,
        "All allies heal 4 HP per enemy unit killed in battle",
        false, false, true, false, false, false, false,
        {SkillID::BLOOD_MAGIC, SkillID::WARDEN_MARK, SkillID::NECROMANCY, SkillID::FIRST_AID, SkillID::TACTICS});

    addClass(F::CrimsonWardens, "Inquisitor Hunter", S::BloodScent,
        "Always knows exact location of Bloodsworn heroes on world map",
        true, false, true, false, false, false, false,
        {SkillID::WARDEN_MARK, SkillID::OFFENSE, SkillID::SCOUTING, SkillID::TACTICS, SkillID::BLOOD_MAGIC});

    // ── VOIDKIN ────────────────────────────────────────────────────────────────
    addClass(F::Voidkin, "Void Weaver", S::VoidLink,
        "When a Void ally dies, adjacent enemies -1 ATK and nearby Void allies +1 ATK (2 rounds)",
        false, false, false, false, true, false, false,
        {SkillID::NATURE_MAGIC, SkillID::POSSESSION, SkillID::TACTICS, SkillID::SCOUTING, SkillID::LEADERSHIP});

    addClass(F::Voidkin, "Shadow Stalker", S::GhostWalk,
        "Hero is always invisible on world map",
        true, false, false, false, false, false, false,
        {SkillID::SCOUTING, SkillID::OFFENSE, SkillID::LOGISTICS, SkillID::POSSESSION, SkillID::TACTICS});

    addClass(F::Voidkin, "Blight Caller", S::BlightAura,
        "Sacred terrain tiles are passively corrupted each turn near hero",
        false, false, false, false, true, false, false,
        {SkillID::NATURE_MAGIC, SkillID::POSSESSION, SkillID::SCOUTING, SkillID::TACTICS, SkillID::LEADERSHIP});

    addClass(F::Voidkin, "Fell Druid", S::Wither,
        "Enemy units in aura lose 1 stat permanently each round",
        true, false, false, false, true, false, false,
        {SkillID::NATURE_MAGIC, SkillID::OFFENSE, SkillID::POSSESSION, SkillID::TACTICS, SkillID::DEFENSE_SKILL});

    // ── IRON ASSEMBLY ──────────────────────────────────────────────────────────
    addClass(F::IronAssembly, "Master Engineer", S::Efficient,
        "All units cost 20% fewer resources to craft",
        false, false, false, false, false, true, false,
        {SkillID::FORGE_MAGIC, SkillID::BLUEPRINT, SkillID::LOGISTICS, SkillID::TACTICS, SkillID::LEADERSHIP});

    addClass(F::IronAssembly, "Warlord Mechanic", S::IronDiscipline,
        "Constructs are immune to morale and fear effects",
        true, false, false, false, false, false, false,
        {SkillID::OFFENSE, SkillID::DEFENSE_SKILL, SkillID::LEADERSHIP, SkillID::TACTICS, SkillID::BLUEPRINT});

    addClass(F::IronAssembly, "Salvage Lord", S::Recycler,
        "All units gain permanent +1 ATK after each battle won (max +5)",
        true, false, false, false, false, true, false,
        {SkillID::FORGE_MAGIC, SkillID::BLUEPRINT, SkillID::OFFENSE, SkillID::TACTICS, SkillID::LOGISTICS});

    addClass(F::IronAssembly, "Runesmith", S::LivingRune,
        "Hero gains +1 ATK and +1 DEF permanently after each battle won (max +5 each)",
        false, false, false, false, false, true, false,
        {SkillID::FORGE_MAGIC, SkillID::BLUEPRINT, SkillID::DEFENSE_SKILL, SkillID::FIRST_AID, SkillID::LEADERSHIP});

    // ── AMALGAMATE ─────────────────────────────────────────────────────────────
    addClass(F::Amalgamate, "Evolver", S::RapidEvolution,
        "Units gain adaptations after taking only 1 hit of a damage type",
        false, false, false, false, false, false, true,
        {SkillID::FLESH_MAGIC, SkillID::ADAPTATION, SkillID::TACTICS, SkillID::LEADERSHIP, SkillID::SCOUTING});

    addClass(F::Amalgamate, "Hive Controller", S::Collective,
        "OrganicMech units share the best adaptation count at each round start",
        false, false, false, false, false, false, true,
        {SkillID::FLESH_MAGIC, SkillID::ADAPTATION, SkillID::LEADERSHIP, SkillID::FIRST_AID, SkillID::TACTICS});

    addClass(F::Amalgamate, "Flesh Architect", S::Infestation,
        "Flesh terrain spreads to adjacent tiles every round passively",
        true, false, false, false, false, false, true,
        {SkillID::FLESH_MAGIC, SkillID::ADAPTATION, SkillID::OFFENSE, SkillID::TACTICS, SkillID::LOGISTICS});

    addClass(F::Amalgamate, "Apex Hunter", S::Apex,
        "Hero starts battle with all adaptations The Evolved has accumulated",
        true, false, false, false, false, false, false,
        {SkillID::OFFENSE, SkillID::ADAPTATION, SkillID::TACTICS, SkillID::SCOUTING, SkillID::FLESH_MAGIC});

    // ── CONVERGENCE ────────────────────────────────────────────────────────────
    addClass(F::Convergence, "Lightbringer", S::Radiance,
        "Buff and debuff spells last 4 rounds instead of 2",
        false, true, false, false, false, false, false,
        {SkillID::LIGHT_MAGIC, SkillID::MIRRORING, SkillID::NATURE_MAGIC, SkillID::LEADERSHIP, SkillID::TACTICS});

    addClass(F::Convergence, "Oathbound", S::Covenant,
        "When a buff is cast, adjacent allies receive half the buff value",
        false, true, false, false, true, false, false,
        {SkillID::MIRRORING, SkillID::LEADERSHIP, SkillID::FIRST_AID, SkillID::LIGHT_MAGIC, SkillID::NATURE_MAGIC});

    addClass(F::Convergence, "Shadowlord", S::PredatorMirror,
        "First spell cast each battle costs no mana",
        false, false, false, true, false, false, false,
        {SkillID::MIRRORING, SkillID::DEATH_MAGIC, SkillID::BLOOD_MAGIC, SkillID::OFFENSE, SkillID::TACTICS});

    addClass(F::Convergence, "Voidcaller", S::Corruption,
        "Enemy units lose -1 DEF each round (starting round 2)",
        false, false, true, true, false, false, false,
        {SkillID::MIRRORING, SkillID::DEATH_MAGIC, SkillID::NATURE_MAGIC, SkillID::TACTICS, SkillID::SCOUTING});

    addClass(F::Convergence, "Ironweaver", S::Synthesis,
        "Hero regenerates +2 extra mana per round in combat",
        false, false, false, false, false, true, false,
        {SkillID::MIRRORING, SkillID::FORGE_MAGIC, SkillID::FLESH_MAGIC, SkillID::TACTICS, SkillID::LEADERSHIP});

    addClass(F::Convergence, "Fleshbinder", S::AdaptationMirror,
        "When any ally dies, all friendly OrganicMech units gain +1 ATK or DEF",
        false, false, false, false, false, false, true,
        {SkillID::MIRRORING, SkillID::FLESH_MAGIC, SkillID::ADAPTATION, SkillID::LEADERSHIP, SkillID::FIRST_AID});
}

const HeroClassDef* HeroClassRegistry::getClass(int id) const {
    for (auto& c : m_classes) if (c.id == id) return &c;
    return nullptr;
}

std::vector<const HeroClassDef*> HeroClassRegistry::getClassesForFaction(FactionId f) const {
    std::vector<const HeroClassDef*> result;
    for (auto& c : m_classes)
        if (c.faction == f) result.push_back(&c);
    return result;
}
