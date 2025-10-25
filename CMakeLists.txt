cmake_minimum_required(VERSION 3.5)
project(mod-guild-village)

set(scripts_STAT_SRCS
  src/loader.cpp
  src/guild_village_commands.cpp
  src/guild_village_create.cpp
  src/guild_village_upgrade.cpp
  src/guild_village_respawn.cpp
  src/guild_village_loot.cpp
  src/guild_village_gm.cpp
  src/guild_village_rest.cpp
  src/guild_village_disband.cpp
  src/guild_village_where.cpp
  src/guild_village_bot.cpp
  src/guild_village_teleporter.cpp
  src/gv_names.cpp
  src/boss/boss_voltrix_the_unbound.cpp
  src/boss/boss_thranok_the_unyielding.cpp
  src/boss/boss_thalgron_the_earthshaker.cpp
  src/boss/boss_thalor_the_lifebinder.cpp
)

AC_ADD_SCRIPT("${scripts_STAT_SRCS}")
AC_ADD_CONFIG_FILE(conf/mod_guild_village.conf.dist)

message(STATUS "-> module 'mod-guild-village' loaded.")
