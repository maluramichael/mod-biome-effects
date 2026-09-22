-- mod-biome-effects: four server-side, passive, hidden "biome" carrier spells + two amplifier items.
--
-- Spells (same pattern as mod-group-buffs, data/sql/db-world/updates/mod_group_buffs_2026_09_20_00.sql):
--   * Attributes = PASSIVE (0x40) | DO_NOT_DISPLAY (0x80) | NO_IMMUNITIES (0x20000000)
--     -> never sent to the client (no buff icon), never saved to the character DB, not dispellable.
--   * The client does not need them in Spell.dbc (client-side patch NOT required).
--   * EquippedItemClass = -1 -> no weapon/item requirement.
--   * DurationIndex 21 = infinite, RangeIndex 1 = self, CastingTimeIndex 1 = instant.
--   * Effect_1 = 6 (SPELL_EFFECT_APPLY_AURA), ImplicitTargetA_1 = 1 (TARGET_UNIT_CASTER).
--   * EffectAura_1 = 22 (SPELL_AURA_MOD_RESISTANCE), EffectMiscValue_1 = school mask, base points 0
--     (the module rewrites the effect amount at runtime with AuraEffect::ChangeAmount, see BiomeEffects.cpp).
--
-- 200220 Biome: Frozen   -> frost resistance  (EffectMiscValue_1 = 16, SPELL_SCHOOL_MASK_FROST)
-- 200221 Biome: Volcanic -> fire resistance   (EffectMiscValue_1 =  4, SPELL_SCHOOL_MASK_FIRE)
-- 200222 Biome: Toxic    -> nature resistance (EffectMiscValue_1 =  8, SPELL_SCHOOL_MASK_NATURE)
-- 200223 Biome: Arcane   -> arcane resistance (EffectMiscValue_1 = 64, SPELL_SCHOOL_MASK_ARCANE)
--
-- Idempotent (DELETE + INSERT), only touches spell ids 200220-200223.

DELETE FROM `spell_dbc` WHERE `ID` IN (200220, 200221, 200222, 200223);

INSERT INTO `spell_dbc`
(`ID`, `Attributes`, `EquippedItemClass`, `CastingTimeIndex`, `DurationIndex`, `RangeIndex`, `SchoolMask`,
 `Effect_1`, `ImplicitTargetA_1`, `EffectAura_1`, `EffectMiscValue_1`,
 `Name_Lang_enUS`, `Name_Lang_Mask`)
VALUES
(200220, 536871104, -1, 1, 21, 1, 1, 6, 1, 22, 16, 'Biome: Frozen',   16712190),
(200221, 536871104, -1, 1, 21, 1, 1, 6, 1, 22,  4, 'Biome: Volcanic', 16712190),
(200222, 536871104, -1, 1, 21, 1, 1, 6, 1, 22,  8, 'Biome: Toxic',    16712190),
(200223, 536871104, -1, 1, 21, 1, 1, 6, 1, 22, 64, 'Biome: Arcane',   16712190);

-- Two minimal custom trinkets that amplify a biome by granting the matching resistance directly
-- (item_template's own frost_res / fire_res columns), stacking with the biome aura's resistance.
-- Idempotent (DELETE + INSERT), only touches item entries 9000200-9000201.

DELETE FROM `item_template` WHERE `entry` IN (9000200, 9000201);

INSERT INTO `item_template`
(`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `BuyCount`, `BuyPrice`, `SellPrice`,
 `InventoryType`, `AllowableClass`, `AllowableRace`, `ItemLevel`, `RequiredLevel`, `maxcount`, `stackable`,
 `frost_res`, `fire_res`, `bonding`, `description`, `Material`, `sheath`, `MaxDurability`, `VerifiedBuild`)
VALUES
(9000200, 4, 0, 'Glacial Ward Charm', 6513, 2, 1, 5000, 1250, 12, -1, -1, 20, 1, 0, 1,
 10, 0, 1, 'A cold, faintly humming trinket. Amplifies the resistance granted by frozen biomes.',
 3, 0, 0, 12340),
(9000201, 4, 0, 'Ember Ward Charm', 6513, 2, 1, 5000, 1250, 12, -1, -1, 20, 1, 0, 1,
 0, 10, 1, 'Warm to the touch. Amplifies the resistance granted by volcanic biomes.',
 3, 0, 0, 12340);
