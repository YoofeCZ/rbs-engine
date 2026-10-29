-- RBS Engine - base game scripts entry point.
--
-- Everything gameplay related lives in Lua (or in native plugins, see sdk/).
-- A mod can override any of these files by shipping a file with the same path
-- in mods/<mod>/scripts/, or add new ones and require them from its own init.lua.

require "util"
require "entities.builtin"
require "entities.items"
require "entities.triggers"
require "entities.doors"
require "entities.story"
require "weapons.projectiles"
require "weapons.weapons"
require "behavior"
require "quests"
require "dialogue"
require "savegame"
require "monsters.base"
require "monsters.grunt"
require "monsters.fiend"
require "components.common"
require "game"
require "hud"
require "definitions"  -- loads entities/*.json and weapons/*.json once all scripts are in
