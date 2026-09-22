/*
 * mod-biome-effects
 *
 * Zone-based passive auras for horizontal progression: entering certain zones applies a themed,
 * hidden, passive resistance aura ("biome"); leaving it (or entering a different biome) removes it.
 *
 * How it works
 *  - Every WotLK zone is mapped to at most one Biome in GetBiomeForZone() (a hand-picked subset for v1,
 *    see the table below and conf/mod_biome_effects.conf.dist).
 *  - Each Biome is carried by one passive, hidden, server-side spell (200220-200223, created by
 *    data/sql/db-world/updates/mod_biome_effects_*.sql, same pattern as mod-group-buffs): a single
 *    SPELL_AURA_MOD_RESISTANCE effect for the matching school. The aura's amount is rewritten with
 *    AuraEffect::ChangeAmount to BiomeEffects.ResistanceAmount so it stays admin-tunable without
 *    touching the DB.
 *  - PlayerScript::OnPlayerUpdateZone applies/removes the aura on every zone change; OnPlayerLogin
 *    applies it once for the zone the character is already standing in when it logs in (UpdateZone is
 *    only fired on an actual change, not on login).
 *  - Per-player state (which biome's aura, if any, is currently applied) lives in Player::CustomData so
 *    repeated calls for the same biome (e.g. area changes inside one zone) are a no-op.
 *  - A couple of custom items (data/sql, entries 9000200+) amplify a biome by granting the matching
 *    resistance directly via the item's own frost_res/fire_res template columns; no extra code needed,
 *    it simply stacks with the biome aura through the core's normal per-school resistance total.
 */

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace
{
    // Server-side passive carrier spells, see data/sql/db-world/updates/mod_biome_effects_2026_09_22_00.sql
    constexpr uint32 SPELL_BIOME_FROZEN   = 200220; // Frost Resistance
    constexpr uint32 SPELL_BIOME_VOLCANIC = 200221; // Fire Resistance
    constexpr uint32 SPELL_BIOME_TOXIC    = 200222; // Nature Resistance
    constexpr uint32 SPELL_BIOME_ARCANE   = 200223; // Arcane Resistance

    enum class Biome : uint8
    {
        None     = 0,
        Frozen   = 1,
        Volcanic = 2,
        Toxic    = 3,
        Arcane   = 4
    };

    constexpr std::array<std::pair<Biome, uint32>, 4> BIOME_SPELLS = { {
        { Biome::Frozen,   SPELL_BIOME_FROZEN },
        { Biome::Volcanic, SPELL_BIOME_VOLCANIC },
        { Biome::Toxic,    SPELL_BIOME_TOXIC },
        { Biome::Arcane,   SPELL_BIOME_ARCANE },
    } };

    uint32 SpellForBiome(Biome biome)
    {
        for (auto const& [b, spellId] : BIOME_SPELLS)
            if (b == biome)
                return spellId;

        return 0;
    }

    // Hand-picked v1 zone -> biome mapping (WotLK zone IDs). See conf/mod_biome_effects.conf.dist for the
    // documented list. Extend this switch to add more zones/biomes later.
    Biome GetBiomeForZone(uint32 zoneId)
    {
        switch (zoneId)
        {
            // Frozen: frost resistance
            case 1:    // Dun Morogh
            case 617:  // Winterspring
                return Biome::Frozen;

            // Volcanic: fire resistance
            case 51:   // Searing Gorge
            case 46:   // Burning Steppes
                return Biome::Volcanic;

            // Toxic: nature resistance
            case 15:   // Swamp of Sorrows
            case 490:  // Un'Goro Crater
                return Biome::Toxic;

            // Arcane: arcane resistance
            case 3523: // Netherstorm
            case 4080: // Isle of Quel'Danas
                return Biome::Arcane;

            default:
                return Biome::None;
        }
    }

    struct BiomeEffectsConfig
    {
        bool enable = true;
        bool includeBots = true;
        int32 resistanceAmount = 15;
    };

    BiomeEffectsConfig sCfg;

    // False when the carrier spells are missing from spell_dbc (SQL not applied).
    bool sAuraSpellsAvailable = true;

    struct BiomeEffectsData : public DataMap::Base
    {
        Biome active = Biome::None; // biome whose aura is currently applied, if any
    };

    std::string const DATA_KEY = "BiomeEffects";

    void LoadConfig()
    {
        sCfg.enable = sConfigMgr->GetOption<bool>("BiomeEffects.Enable", true);
        sCfg.includeBots = sConfigMgr->GetOption<bool>("BiomeEffects.IncludeBots", true);
        sCfg.resistanceAmount = std::max(0, sConfigMgr->GetOption<int32>("BiomeEffects.ResistanceAmount", 15));
    }

    bool IsBotPlayer(Player* player)
    {
        WorldSession* session = player->GetSession();
        return session && session->IsBot();
    }

    // Make sure spellId is on the player as a self-aura with its single effect set to `amount`.
    void ApplyBiomeAura(Player* player, uint32 spellId, int32 amount)
    {
        Aura* aura = player->GetAura(spellId);
        if (!aura)
            aura = player->AddAura(spellId, player);

        if (!aura || aura->IsRemoved())
            return;

        AuraEffect* effect = aura->GetEffect(0);
        if (effect && effect->GetAmount() != amount)
            effect->ChangeAmount(amount);
    }

    // Recompute the biome for the player's current zone and bring the aura in line with it.
    void RefreshBiome(Player* player, BiomeEffectsData* data, uint32 zoneId)
    {
        Biome wanted = Biome::None;
        if (sCfg.enable && sAuraSpellsAvailable && (sCfg.includeBots || !IsBotPlayer(player)))
            wanted = GetBiomeForZone(zoneId);

        if (wanted == data->active)
            return;

        if (data->active != Biome::None)
            player->RemoveAurasDueToSpell(SpellForBiome(data->active));

        if (wanted != Biome::None)
            ApplyBiomeAura(player, SpellForBiome(wanted), sCfg.resistanceAmount);

        data->active = wanted;
    }
}

class BiomeEffectsPlayerScript : public PlayerScript
{
public:
    BiomeEffectsPlayerScript() : PlayerScript("BiomeEffectsPlayerScript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_UPDATE_ZONE }) { }

    // UpdateZone only fires on an actual zone change, so apply the starting biome once on login.
    void OnPlayerLogin(Player* player) override
    {
        BiomeEffectsData* data = player->CustomData.GetDefault<BiomeEffectsData>(DATA_KEY);
        RefreshBiome(player, data, player->GetZoneId());
    }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
    {
        BiomeEffectsData* data = player->CustomData.GetDefault<BiomeEffectsData>(DATA_KEY);
        RefreshBiome(player, data, newZone);
    }
};

class BiomeEffectsWorldScript : public WorldScript
{
public:
    BiomeEffectsWorldScript() : WorldScript("BiomeEffectsWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    // The spell store is loaded by now: verify the carrier spells created by the module's SQL exist.
    void OnStartup() override
    {
        uint32 missing = 0;
        for (auto const& [biome, spellId] : BIOME_SPELLS)
        {
            if (!sSpellMgr->GetSpellInfo(spellId))
            {
                ++missing;
                LOG_ERROR("server.loading", "mod-biome-effects: server-side spell {} is missing from spell_dbc "
                    "(data/sql/db-world/updates/mod_biome_effects_*.sql not applied?). Biome auras are disabled.",
                    spellId);
            }
        }

        sAuraSpellsAvailable = (missing == 0);

        if (sAuraSpellsAvailable)
            LOG_INFO("server.loading", "mod-biome-effects: loaded (enabled: {}, bots count: {}).",
                sCfg.enable, sCfg.includeBots);
    }
};

void AddBiomeEffectsScripts()
{
    LoadConfig();

    new BiomeEffectsWorldScript();
    new BiomeEffectsPlayerScript();
}
