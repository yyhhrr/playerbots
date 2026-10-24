-- Classic has no general spell-power/hit glove enchant. Use Healing Power
-- for Balance druids' supporting heals instead of the unavailable TBC spell.
UPDATE ai_playerbot_enchants
SET spellid = 25079, name = 'Enchant Gloves - Healing Power'
WHERE class = 11 AND spec = 110 AND slotid = 9 AND spellid = 33994;
