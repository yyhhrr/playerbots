cmake_minimum_required(VERSION 3.16)
project(playerbots_tests LANGUAGES CXX)

enable_testing()

add_executable(dungeon_movement_policy_test dungeon_movement_policy_test.cpp)
target_include_directories(dungeon_movement_policy_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(dungeon_movement_policy_test PRIVATE cxx_std_20)
add_test(NAME dungeon_movement_policy COMMAND dungeon_movement_policy_test)

# Compile the complete shared dungeon-safety implementation with an in-memory
# map and navmesh driver, retaining production policy and caching behavior.
set(dungeon_source_dir "${CMAKE_CURRENT_BINARY_DIR}/dungeon-movement-source")
foreach(dungeon_file IN ITEMS DungeonMovement.cpp DungeonMovement.h DungeonMovementPolicy.h)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/${dungeon_file}" "${dungeon_source_dir}/${dungeon_file}" COPYONLY)
endforeach()

# Execute live target perception and the cached/explicit pursuit entry points.
# Overrides permit the same fixture to demonstrate the pre-fix combat bypasses.
set(STEALTH_POSSIBLE_TARGET_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/PossibleTargetsValue.cpp" CACHE FILEPATH "Possible-target perception source under test")
set(STEALTH_INVALID_TARGET_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/InvalidTargetValue.cpp" CACHE FILEPATH "Invalid-target perception source under test")
set(STEALTH_ENEMY_PLAYER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/EnemyPlayerValue.cpp" CACHE FILEPATH "Enemy-player perception source under test")
set(STEALTH_POSSIBLE_ATTACK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/PossibleAttackTargetsValue.cpp" CACHE FILEPATH "Possible-attack perception source under test")
set(STEALTH_ATTACK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/AttackAction.cpp" CACHE FILEPATH "Explicit attack perception source under test")
set(STEALTH_MOVEMENT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/MovementActions.cpp" CACHE FILEPATH "Pursuit perception source under test")
function(extract_stealth_source source marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" stealth_source)
    string(FIND "${stealth_source}" "${marker}" stealth_begin)
    if(stealth_begin LESS 0)
        message(FATAL_ERROR "Stealth perception extraction marker is missing in ${source}")
    endif()
    if(end_marker STREQUAL "")
        string(LENGTH "${stealth_source}" stealth_end)
    else()
        string(SUBSTRING "${stealth_source}" ${stealth_begin} -1 stealth_remainder)
        string(FIND "${stealth_remainder}" "${end_marker}" stealth_relative_end)
        if(stealth_relative_end LESS 0)
            message(FATAL_ERROR "Stealth perception extraction end marker is missing in ${source}")
        endif()
        math(EXPR stealth_end "${stealth_begin} + ${stealth_relative_end}")
    endif()
    if(stealth_begin LESS 0 OR stealth_end LESS stealth_begin)
        message(FATAL_ERROR "Stealth perception extraction markers are missing in ${source}")
    endif()
    math(EXPR stealth_length "${stealth_end} - ${stealth_begin}")
    string(SUBSTRING "${stealth_source}" ${stealth_begin} ${stealth_length} stealth_function)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${stealth_function}")
endfunction()
extract_stealth_source("${STEALTH_POSSIBLE_TARGET_SOURCE}" "bool PossibleTargetsValue::IsFriendly(" "" stealth_possible_targets.inc)
extract_stealth_source("${STEALTH_INVALID_TARGET_SOURCE}" "bool InvalidTargetValue::Calculate(" "" stealth_invalid_target.inc)
extract_stealth_source("${STEALTH_ENEMY_PLAYER_SOURCE}" "bool EnemyPlayersValue::IsValid(" "bool EnemyPlayersValue::IsAllowedArathiDefenderTarget(" stealth_enemy_player.inc)
extract_stealth_source("${STEALTH_POSSIBLE_ATTACK_SOURCE}" "bool PossibleAttackTargetsValue::IsPossibleTarget(" "bool PossibleAddsValue::Calculate(" stealth_possible_attack.inc)
extract_stealth_source("${STEALTH_ATTACK_SOURCE}" "bool AttackAction::IsTargetValid(" "bool AttackDuelOpponentAction::isUseful(" stealth_attack_target.inc)
extract_stealth_source("${STEALTH_ATTACK_SOURCE}" "bool AttackAction::PetAttack(" "    // If we're done waiting to attack" stealth_pet_attack_guard.inc)
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/stealth_pet_attack_guard.inc" "    ++petOrders;\n    return true;\n}\n")
extract_stealth_source("${STEALTH_MOVEMENT_SOURCE}" "bool MovementAction::ChaseTo(" "    if (dungeon::IsActive(ai) && !dungeon::CanTarget(ai, obj))" stealth_chase_guard.inc)
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/stealth_chase_guard.inc" "    ++chaseOrders;\n    return true;\n}\n")
add_executable(stealth_perception_test stealth_perception_test.cpp)
target_include_directories(stealth_perception_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(stealth_perception_test PRIVATE cxx_std_20)
target_compile_definitions(stealth_perception_test PRIVATE MANGOSBOT_ZERO)
file(READ "${STEALTH_POSSIBLE_TARGET_SOURCE}" stealth_perception_probe)
string(FIND "${stealth_perception_probe}" "bool PossibleTargetsValue::CanPerceive(" stealth_helper_offset)
if(NOT stealth_helper_offset LESS 0)
    target_compile_definitions(stealth_perception_test PRIVATE STEALTH_HAS_CAN_PERCEIVE)
    extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/TargetPerception.cpp"
        "bool ai::perception::CanUseTarget(" "" stealth_cleanup.inc)
    extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
        "    // Loss of a target must also stop native chase" "    // Banner casts can suspend" stealth_outer_cleanup.inc)
    extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
        "bool PlayerbotAI::CanCastSpell(uint32 spellid, Unit* target," "    if (!IsSpellSafeForWarsong(spellid, target))" stealth_can_cast_guard.inc)
    extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
        "bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target," "    if (!IsSpellSafeForWarsong(spellId, target))" stealth_cast_guard.inc)
    extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
        "bool PlayerbotAI::CastPetSpell(uint32 spellId, Unit* target)" "    Pet* pet = bot->GetPet();" stealth_pet_cast_guard.inc)
    foreach(stealth_cast_guard IN ITEMS stealth_can_cast_guard.inc stealth_cast_guard.inc stealth_pet_cast_guard.inc)
        file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/${stealth_cast_guard}" "    ++castStages;\n    return true;\n}\n")
    endforeach()
endif()
add_test(NAME stealth_perception COMMAND stealth_perception_test)
add_executable(dungeon_movement_runtime_test dungeon_movement_runtime_test.cpp "${dungeon_source_dir}/DungeonMovement.cpp")
target_include_directories(dungeon_movement_runtime_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/dungeon_movement_stubs" "${dungeon_source_dir}")
target_compile_features(dungeon_movement_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(dungeon_movement_runtime_test PRIVATE MANGOSBOT_ZERO CMANGOS)
add_test(NAME dungeon_movement_runtime COMMAND dungeon_movement_runtime_test)

# Execute the production spline submission and movement-state lifecycle with a
# deterministic clock and the complete shared dungeon route implementation.
function(extract_dungeon_source source marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" dungeon_source)
    string(FIND "${dungeon_source}" "${marker}" dungeon_begin)
    if(end_marker STREQUAL "")
        string(LENGTH "${dungeon_source}" dungeon_end)
    else()
        string(FIND "${dungeon_source}" "${end_marker}" dungeon_end)
    endif()
    if(dungeon_begin LESS 0 OR dungeon_end LESS dungeon_begin)
        message(FATAL_ERROR "Dungeon movement extraction markers are missing in ${source}")
    endif()
    math(EXPR dungeon_length "${dungeon_end} - ${dungeon_begin}")
    string(SUBSTRING "${dungeon_source}" ${dungeon_begin} ${dungeon_length} dungeon_function)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${dungeon_function}")
endfunction()
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/MovementActions.cpp"
    "bool MovementAction::MoveAlongDungeonPath(" "void MovementAction::CreateWp(" dungeon_motion_source.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/LastMovementValue.h"
    "    class LastMovement\n" "    class LastMovementValue" dungeon_last_movement.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/triggers/RangeTriggers.h"
    "    class UpdateFollowTrigger" "    class StopFollowTrigger" dungeon_follow_trigger.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/generic/DungeonSafeStrategy.cpp"
    "void DungeonSafeStrategy::OnStrategyAdded(" "" dungeon_strategy_hooks.inc)
add_executable(dungeon_motion_test dungeon_motion_test.cpp "${dungeon_source_dir}/DungeonMovement.cpp")
target_include_directories(dungeon_motion_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/dungeon_movement_stubs" "${dungeon_source_dir}")
target_compile_features(dungeon_motion_test PRIVATE cxx_std_20)
target_compile_definitions(dungeon_motion_test PRIVATE MANGOSBOT_ZERO CMANGOS DUNGEON_MOTION_TEST)
add_test(NAME dungeon_motion COMMAND dungeon_motion_test)

foreach(test_name IN ITEMS mage_aoe_geometry mage_aoe_policy enchanting_policy need_roll_command quest_item_exchange quest_item_exchange_command guild_bot_login arathi_basin_policy arathi_basin_combat arathi_capture_policy alterac_valley_policy warsong_policy warsong_combat warsong_carrier_policy warsong_carrier_abilities warsong_spell_policy)
    add_executable(${test_name}_test ${test_name}_test.cpp)
    target_include_directories(${test_name}_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_features(${test_name}_test PRIVATE cxx_std_20)
    add_test(NAME ${test_name} COMMAND ${test_name}_test)
endforeach()

# Execute the production threat and retreat decisions, plus Blizzard readiness.
function(extract_courage_source source marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" courage_source)
    string(FIND "${courage_source}" "${marker}" courage_begin)
    if(courage_begin LESS 0)
        message(FATAL_ERROR "Mage AOE courage extraction marker is missing in ${source}")
    endif()
    if(end_marker STREQUAL "")
        string(SUBSTRING "${courage_source}" ${courage_begin} -1 courage_function)
    else()
        string(FIND "${courage_source}" "${end_marker}" courage_end)
        if(courage_end LESS courage_begin)
            message(FATAL_ERROR "Mage AOE courage extraction end marker is missing in ${source}")
        endif()
        math(EXPR courage_length "${courage_end} - ${courage_begin}")
        string(SUBSTRING "${courage_source}" ${courage_begin} ${courage_length} courage_function)
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${courage_function}")
endfunction()
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/generic/ThreatStrategy.cpp" "float ThreatMultiplier::GetValue(" "void ThreatStrategy::InitCombatMultipliers(" mage_aoe_courage_threat.inc)
set(MAGE_AOE_COMMIT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageMultipliers.cpp" CACHE FILEPATH "Mage AOE commitment source under test")
extract_courage_source("${MAGE_AOE_COMMIT_SOURCE}" "float MageAoeCommitMultiplier::GetValue(" "" mage_aoe_courage_multiplier.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageActions.cpp" "bool CastBlizzardAction::IsAoeReady(" "" mage_aoe_courage_blizzard.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageStrategy.cpp" "void MageAoeStrategy::InitCombatMultipliers(" "#ifdef MANGOSBOT_ZERO" mage_aoe_courage_registration.inc)
add_executable(mage_aoe_courage_test mage_aoe_courage_test.cpp)
target_include_directories(mage_aoe_courage_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(mage_aoe_courage_test PRIVATE cxx_std_20)
add_test(NAME mage_aoe_courage COMMAND mage_aoe_courage_test)

# Execute the actual PvP root-opportunity trigger and its classic strategy priority.
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageTriggers.cpp" "bool MagePvpRetreatTrigger::IsActive(" "Unit* LevelingArcaneExplosionTrigger::GetTarget(" mage_pvp_retreat_trigger.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageStrategy.cpp" "void MagePvpStrategy::InitCombatTriggers(" "void MagePvpStrategy::InitNonCombatTriggers(" mage_pvp_retreat_registration.inc)
add_executable(mage_pvp_retreat_test mage_pvp_retreat_test.cpp)
target_include_directories(mage_pvp_retreat_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(mage_pvp_retreat_test PRIVATE cxx_std_20)
add_test(NAME mage_pvp_retreat COMMAND mage_pvp_retreat_test)

# Execute the mage-specific nearby-threat selector and movement delegation.
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageAoe.cpp"
    "Unit* ai::FindMagePveRetreatTarget(PlayerbotAI* ai)" "bool ai::FindMageAoeTarget(" mage_spacing_selector.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageTriggers.cpp"
    "bool MagePveRetreatTrigger::IsActive()" "Unit* MageOtArcaneExplosionTrigger::GetTarget()" mage_spacing_trigger.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageActions.cpp"
    "bool MagePveRetreatAction::isUseful()" "bool CastMageGroundAoeAction::SelectAoeTarget(" mage_spacing_action.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageActions.h"
    "    class MagePveRetreatAction" "    BUFF_ACTION(CastFireWardAction" mage_spacing_action_class.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/MovementActions.h"
    "        bool ShouldReactionInterruptCast() const override { return true; }" "    private:" mage_spacing_base_reaction_flags.inc)
set(spacing_strategy_path "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageStrategy.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${spacing_strategy_path}")
foreach(spacing_phase IN ITEMS Combat Reaction)
    file(READ "${spacing_strategy_path}" spacing_strategy_remainder)
    if(spacing_phase STREQUAL "Combat")
        set(spacing_end_marker "void MageStrategy::InitNonCombatTriggers(")
        set(spacing_registration_prefix "mage_spacing_registration")
    else()
        set(spacing_end_marker "void MageStrategy::InitDeadTriggers(")
        set(spacing_registration_prefix "mage_spacing_reaction_registration")
    endif()
    foreach(spacing_edition RANGE 0 2)
        string(FIND "${spacing_strategy_remainder}" "void MageStrategy::Init${spacing_phase}Triggers(" spacing_begin)
        if(spacing_begin LESS 0)
            message(FATAL_ERROR "Mage ${spacing_phase} registration ${spacing_edition} is missing")
        endif()
        string(SUBSTRING "${spacing_strategy_remainder}" ${spacing_begin} -1 spacing_strategy_remainder)
        string(FIND "${spacing_strategy_remainder}" "${spacing_end_marker}" spacing_end)
        if(spacing_end LESS 0)
            message(FATAL_ERROR "Mage ${spacing_phase} registration end marker is missing")
        endif()
        string(SUBSTRING "${spacing_strategy_remainder}" 0 ${spacing_end} spacing_registration)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${spacing_registration_prefix}_${spacing_edition}.inc" "${spacing_registration}")
        string(SUBSTRING "${spacing_strategy_remainder}" ${spacing_end} -1 spacing_strategy_remainder)
    endforeach()
endforeach()
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageStrategy.cpp"
    "void MageAoeStrategy::InitReactionTriggers(" "void MageAoeStrategy::InitCombatMultipliers(" mage_spacing_aoe_reaction.inc)
# Validate all expansion-specific AOE priorities and existing control responses.
foreach(spacing_kind IN ITEMS Arcane Fire Frost Cc BaseAoe)
    string(TOLOWER "${spacing_kind}" spacing_kind_lower)
    if(spacing_kind STREQUAL "Cc" OR spacing_kind STREQUAL "BaseAoe")
        set(spacing_priority_path "${spacing_strategy_path}")
        if(spacing_kind STREQUAL "Cc")
            set(spacing_priority_class "MageCcStrategy")
        else()
            set(spacing_priority_class "MageAoeStrategy")
        endif()
    else()
        set(spacing_priority_path "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/${spacing_kind}MageStrategy.cpp")
        set(spacing_priority_class "${spacing_kind}MageAoeStrategy")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${spacing_priority_path}")
    file(READ "${spacing_priority_path}" spacing_priority_remainder)
    foreach(spacing_edition RANGE 0 2)
        string(FIND "${spacing_priority_remainder}" "void ${spacing_priority_class}::InitCombatTriggers(" spacing_begin)
        if(spacing_begin LESS 0)
            message(FATAL_ERROR "Mage ${spacing_kind} priority registration ${spacing_edition} is missing")
        endif()
        string(SUBSTRING "${spacing_priority_remainder}" ${spacing_begin} -1 spacing_priority_remainder)
        string(FIND "${spacing_priority_remainder}" "void ${spacing_priority_class}::InitNonCombatTriggers(" spacing_end)
        if(spacing_end LESS 0)
            message(FATAL_ERROR "Mage ${spacing_kind} priority registration end marker is missing")
        endif()
        string(SUBSTRING "${spacing_priority_remainder}" 0 ${spacing_end} spacing_priority_registration)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mage_spacing_${spacing_kind_lower}_${spacing_edition}.inc" "${spacing_priority_registration}")
        string(SUBSTRING "${spacing_priority_remainder}" ${spacing_end} -1 spacing_priority_remainder)
    endforeach()
endforeach()
set(spacing_context_path "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageAiObjectContext.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${spacing_context_path}")
file(STRINGS "${spacing_context_path}" spacing_factory_lines REGEX "creators\\[\"mage pve retreat\"\\]")
foreach(spacing_kind IN ITEMS Trigger Action)
    string(TOLOWER "${spacing_kind}" spacing_kind_lower)
    set(spacing_factory "")
    foreach(spacing_line IN LISTS spacing_factory_lines)
        if(spacing_line MATCHES "new MagePveRetreat${spacing_kind}\\(ai\\)")
            string(APPEND spacing_factory "${spacing_line}\n")
        endif()
    endforeach()
    if(spacing_factory STREQUAL "")
        message(FATAL_ERROR "Mage PvE retreat ${spacing_kind} factory is missing")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mage_spacing_${spacing_kind_lower}_factory.inc" "${spacing_factory}")
endforeach()
add_executable(mage_spacing_runtime_test mage_spacing_runtime_test.cpp)
target_include_directories(mage_spacing_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(mage_spacing_runtime_test PRIVATE cxx_std_20)
add_test(NAME mage_spacing_runtime COMMAND mage_spacing_runtime_test)

# Exercise the live Arathi threat scan, capture interruption/backoff, outer AI
# wakeup and defender return action against a deterministic small battlefield.
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/ArathiBasinTactics.cpp"
    "    void DelayCapture(" "\n}\n\nbool IsCapturing(" arathi_capture_delay.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/ArathiBasinTactics.cpp"
    "bool IsCapturing(" "bool IsCoveringCapture(" arathi_capture_response.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
    "    // Banner casts can suspend the internal engine for their full cast time." "    // Update facing" arathi_outer_response.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/BattleGroundTactics.cpp"
    "bool BGTactics::returnToArathiObjective(" "bool BGTactics::flagTaken(" arathi_return_objective.inc)
add_executable(arathi_response_runtime_test arathi_response_runtime_test.cpp)
target_include_directories(arathi_response_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(arathi_response_runtime_test PRIVATE cxx_std_20)
add_test(NAME arathi_response_runtime COMMAND arathi_response_runtime_test)

# Run the production self-centred explosion selector and both action classes.
set(MAGE_ARCANE_AOE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageAoe.cpp" CACHE FILEPATH "Mage Arcane Explosion selector source under test")
set(MAGE_ARCANE_ACTION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageActions.h" CACHE FILEPATH "Mage Arcane Explosion actions under test")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MAGE_ARCANE_AOE_SOURCE}" "${MAGE_ARCANE_ACTION_SOURCE}")
file(READ "${MAGE_ARCANE_AOE_SOURCE}" arcane_selector_source)
string(FIND "${arcane_selector_source}" "Unit* ai::FindMageArcaneExplosionTarget(" arcane_selector_begin)
if(arcane_selector_begin LESS 0)
    message(FATAL_ERROR "Arcane Explosion selector extraction marker is missing")
endif()
string(SUBSTRING "${arcane_selector_source}" ${arcane_selector_begin} -1 arcane_selector)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mage_arcane_explosion_target.inc" "${arcane_selector}")
string(FIND "${arcane_selector_source}" "bool ai::IsMageAoeAreaSafe(" arcane_area_begin)
if(arcane_area_begin LESS 0 OR arcane_selector_begin LESS arcane_area_begin)
    message(FATAL_ERROR "Mage fixed-area protection extraction markers are missing")
endif()
math(EXPR arcane_area_length "${arcane_selector_begin} - ${arcane_area_begin}")
string(SUBSTRING "${arcane_selector_source}" ${arcane_area_begin} ${arcane_area_length} arcane_area_source)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mage_aoe_area_safe.inc" "${arcane_area_source}")
file(READ "${MAGE_ARCANE_ACTION_SOURCE}" arcane_action_source)
string(FIND "${arcane_action_source}" "    class CastArcaneExplosionAction" arcane_action_begin)
string(FIND "${arcane_action_source}" "    class CastConeOfColdAction" arcane_action_end)
if(arcane_action_begin LESS 0 OR arcane_action_end LESS arcane_action_begin)
    message(FATAL_ERROR "Arcane Explosion action extraction markers are missing")
endif()
math(EXPR arcane_action_length "${arcane_action_end} - ${arcane_action_begin}")
string(SUBSTRING "${arcane_action_source}" ${arcane_action_begin} ${arcane_action_length} arcane_actions)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mage_arcane_explosion_actions.inc" "${arcane_actions}")
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageActions.cpp"
    "bool CastMageOtArcaneExplosionAction::CanReplaceCurrentCast(" "bool MagePveRetreatAction::isUseful()" mage_ot_arcane_explosion_action.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageTriggers.h"
    "    class MageOtArcaneExplosionTrigger" "    class MageGroundAoeTrigger" mage_ot_arcane_explosion_trigger_class.inc)
extract_courage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageTriggers.cpp"
    "Unit* MageOtArcaneExplosionTrigger::GetTarget()" "bool MageGroundAoeTrigger::IsActive()" mage_ot_arcane_explosion_trigger.inc)
add_executable(mage_arcane_explosion_test mage_arcane_explosion_test.cpp)
target_include_directories(mage_arcane_explosion_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(mage_arcane_explosion_test PRIVATE cxx_std_20)
add_test(NAME mage_arcane_explosion COMMAND mage_arcane_explosion_test)

# Exercise the actual hard-coded WSG routes with a deterministic movement driver.
set(warsong_route_source "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/BattleGroundTactics.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${warsong_route_source}")
file(READ "${warsong_route_source}" warsong_source)
string(FIND "${warsong_source}" "Position const WS_WAITING_POS_HORDE_1" positions_begin)
string(FIND "${warsong_source}" "Position const AB_WAITING_POS_HORDE" positions_end)
string(FIND "${warsong_source}" "bool BGTactics::wsgPaths()" routes_begin)
string(FIND "${warsong_source}" "// eots jump logic" routes_end)
if(positions_begin LESS 0 OR positions_end LESS positions_begin OR routes_begin LESS 0 OR routes_end LESS routes_begin)
    message(FATAL_ERROR "WSG route extraction markers are missing")
endif()
math(EXPR positions_length "${positions_end} - ${positions_begin}")
math(EXPR routes_length "${routes_end} - ${routes_begin}")
string(SUBSTRING "${warsong_source}" ${positions_begin} ${positions_length} warsong_positions)
string(SUBSTRING "${warsong_source}" ${routes_begin} ${routes_length} warsong_routes)
string(FIND "${warsong_routes}" "bool BGTactics::moveWarsongGraveyardDrop(" graveyard_drop_begin)
if(NOT graveyard_drop_begin LESS 0)
    string(FIND "${warsong_routes}" "bool BGTactics::wsgRoofJump()" roof_routes_begin)
    string(SUBSTRING "${warsong_routes}" 0 ${graveyard_drop_begin} graveyard_approach_routes)
    string(SUBSTRING "${warsong_routes}" ${roof_routes_begin} -1 roof_routes)
    set(warsong_routes "${graveyard_approach_routes}\n${roof_routes}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/warsong_movement_source.inc" "${warsong_positions}\n${warsong_routes}")
add_executable(warsong_movement_test warsong_movement_test.cpp)
target_include_directories(warsong_movement_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(warsong_movement_test PRIVATE cxx_std_20)
add_test(NAME warsong_movement COMMAND warsong_movement_test)

# Execute the checked WSG descent through the native motion submission boundary.
extract_dungeon_source("${warsong_route_source}" "bool BGTactics::moveWarsongGraveyardDrop(" "bool BGTactics::wsgRoofJump()" warsong_graveyard_drop.inc)
add_executable(warsong_graveyard_runtime_test warsong_graveyard_runtime_test.cpp)
target_include_directories(warsong_graveyard_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(warsong_graveyard_runtime_test PRIVATE cxx_std_20)
add_test(NAME warsong_graveyard_runtime COMMAND warsong_graveyard_runtime_test)

# Execute the complete WSG progress monitor and its outer-AI wakeup.
set(WARSONG_COMBAT_PROGRESS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/WarsongCombatProgress.cpp" CACHE FILEPATH "Warsong pursuit progress source under test")
extract_stealth_source("${WARSONG_COMBAT_PROGRESS_SOURCE}" "namespace ai::warsong" "" warsong_combat_progress_source.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
    "    // A failed native chase" "    // Loss of a target must also stop native chase" warsong_combat_progress_outer.inc)
add_executable(warsong_combat_progress_test warsong_combat_progress_test.cpp)
target_include_directories(warsong_combat_progress_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(warsong_combat_progress_test PRIVATE cxx_std_20)
add_test(NAME warsong_combat_progress COMMAND warsong_combat_progress_test)

# Exercise the actual role-to-destination block and Warsong objective movement.
# Source overrides preserve reproducible regression checks against a saved baseline.
set(WARSONG_TACTICS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/WarsongTactics.cpp" CACHE FILEPATH "Warsong objective source under test")
set(WARSONG_OBJECTIVE_ACTION_SOURCE "${warsong_route_source}" CACHE FILEPATH "Warsong objective movement source under test")
extract_dungeon_source("${WARSONG_TACTICS_SOURCE}" "Task GetTask(" "bool SelectObjective(" warsong_task_source.inc)
extract_dungeon_source("${WARSONG_TACTICS_SOURCE}" "bool SelectObjective(" "bool CanUseFlag(" warsong_select_objective_source.inc)
extract_dungeon_source("${WARSONG_OBJECTIVE_ACTION_SOURCE}" "bool BGTactics::refreshWarsongObjective()" "bool BGTactics::wsgPaths()" warsong_objective_actions.inc)
extract_dungeon_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/generic/BattlegroundStrategy.cpp"
    "void WarsongStrategy::InitNonCombatTriggers(" "void AlteracStrategy::InitNonCombatTriggers(" warsong_graveyard_registration.inc)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/warsong_objective_positions.inc" "${warsong_positions}")
add_executable(warsong_objective_test warsong_objective_test.cpp)
target_include_directories(warsong_objective_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(warsong_objective_test PRIVATE cxx_std_20)
file(READ "${WARSONG_TACTICS_SOURCE}" warsong_objective_source)
string(FIND "${warsong_objective_source}" "bool IsPressuring(" warsong_pressure_query)
if(warsong_pressure_query LESS 0)
    target_compile_definitions(warsong_objective_test PRIVATE WARSONG_LEGACY_NO_PRESSURE_QUERY)
endif()
add_test(NAME warsong_objective COMMAND warsong_objective_test)

# Run the production carrier skill state, execution and generic druid-form gate.
# Source overrides replay the same regression against the saved pre-fix code.
set(WARSONG_CARRIER_RUNTIME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/WarsongCarrierActions.cpp" CACHE FILEPATH "Carrier runtime source under test")
set(DRUID_TRAVEL_RUNTIME_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/druid/DruidActions.h" CACHE FILEPATH "Druid travel action source under test")
extract_stealth_source("${WARSONG_CARRIER_RUNTIME_SOURCE}" "    uint32 KnownSpell(" "CarrierAbilityState InspectCarrierAbilities(" warsong_carrier_helpers.inc)
extract_stealth_source("${WARSONG_CARRIER_RUNTIME_SOURCE}" "bool WarsongCarrierMobilityAction::isUseful(" "" warsong_carrier_execution.inc)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/warsong_carrier_execution.inc" carrier_execution_source)
string(REGEX REPLACE "\n}[ \r\n]*$" "\n" carrier_execution_source "${carrier_execution_source}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/warsong_carrier_execution.inc" "${carrier_execution_source}")
extract_stealth_source("${DRUID_TRAVEL_RUNTIME_SOURCE}" "class CastTravelFormAction" "class CastCasterFormAction" druid_travel_action.inc)
add_executable(warsong_carrier_runtime_test warsong_carrier_runtime_test.cpp)
target_include_directories(warsong_carrier_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(warsong_carrier_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(warsong_carrier_runtime_test PRIVATE MANGOSBOT_ZERO)
add_test(NAME warsong_carrier_runtime COMMAND warsong_carrier_runtime_test)

# Compile the production recipe extractor with a minimal in-memory world store.
# Copying preserves the source bytes while resolving core-only headers to test stubs.
set(recipe_source_dir "${CMAKE_CURRENT_BINARY_DIR}/quest-exchange-recipe-source")
foreach(recipe_file IN ITEMS QuestItemExchangeRecipes.cpp QuestItemExchangeRecipes.h QuestItemExchange.h QuestItemExchangePolicy.h)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/${recipe_file}" "${recipe_source_dir}/${recipe_file}" COPYONLY)
endforeach()
add_executable(quest_item_exchange_recipes_test quest_item_exchange_recipes_test.cpp "${recipe_source_dir}/QuestItemExchangeRecipes.cpp")
target_include_directories(quest_item_exchange_recipes_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/quest_item_exchange_recipe_stubs" "${recipe_source_dir}")
target_compile_features(quest_item_exchange_recipes_test PRIVATE cxx_std_20)
add_test(NAME quest_item_exchange_recipes COMMAND quest_item_exchange_recipes_test)

# Exercise the production AOE candidate and crowd-control filters with a small
# in-memory combat fixture. Allow an old source file for regression comparisons.
set(MAGE_AOE_TARGET_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/mage/MageAoe.cpp" CACHE FILEPATH "Mage AOE source under test")
function(extract_mage_source source begin_marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_text)
    string(FIND "${source_text}" "${begin_marker}" begin_offset)
    string(FIND "${source_text}" "${end_marker}" end_offset)
    if(begin_offset LESS 0 OR end_offset LESS begin_offset)
        message(FATAL_ERROR "Mage AOE test extraction markers are missing in ${source}")
    endif()
    math(EXPR source_length "${end_offset} - ${begin_offset}")
    string(SUBSTRING "${source_text}" ${begin_offset} ${source_length} extracted)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${extracted}")
endfunction()
extract_mage_source("${MAGE_AOE_TARGET_SOURCE}" "    const std::list<ObjectGuid> attackers =" "    if (targets.size() < MAGE_GROUND_AOE_MIN_TARGETS)" mage_aoe_candidates.inc)
extract_mage_source("${MAGE_AOE_TARGET_SOURCE}" "    std::vector<Unit*> protectedTargets;" "    std::list<DynamicObject*> existing;" mage_aoe_protected_targets.inc)
extract_mage_source("${MAGE_AOE_TARGET_SOURCE}" "        const bool breaksControl =" "        const float dx = center.x" mage_aoe_protection.inc)
extract_mage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/PossibleAttackTargetsValue.cpp" "bool PossibleAttackTargetsValue::IsValid(" "bool PossibleAttackTargetsValue::IsPossibleTarget(" mage_possible_attack_targets.inc)
extract_mage_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/AttackersValue.cpp" "bool AttackersValue::InCombat(" "bool AttackersValue::IsValid(" mage_attackers_in_combat.inc)
add_executable(mage_aoe_targeting_test mage_aoe_targeting_test.cpp)
target_include_directories(mage_aoe_targeting_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(mage_aoe_targeting_test PRIVATE cxx_std_20)
add_test(NAME mage_aoe_targeting COMMAND mage_aoe_targeting_test)

# Execute the actual post-commit exchange flow against an in-memory event log.
set(ENCHANTING_EXCHANGE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/AutoQuestItemExchangeAction.cpp" CACHE FILEPATH "Exchange source under test")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ENCHANTING_EXCHANGE_SOURCE}")
file(READ "${ENCHANTING_EXCHANGE_SOURCE}" enchanting_exchange_source)
string(FIND "${enchanting_exchange_source}" "    // Every reward is equipped at its planned slot before any key is consumed." exchange_commit_begin)
string(FIND "${enchanting_exchange_source}" "    sLog.outDetail(\"Bot #%u <%s> exchanged quest item keys" exchange_commit_end)
if(exchange_commit_begin LESS 0 OR exchange_commit_end LESS exchange_commit_begin)
    message(FATAL_ERROR "Enchanting exchange extraction markers are missing")
endif()
math(EXPR exchange_commit_length "${exchange_commit_end} - ${exchange_commit_begin}")
string(SUBSTRING "${enchanting_exchange_source}" ${exchange_commit_begin} ${exchange_commit_length} exchange_commit_source)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/enchanting_exchange_commit.inc" "${exchange_commit_source}")
add_executable(enchanting_exchange_test enchanting_exchange_test.cpp)
target_include_directories(enchanting_exchange_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(enchanting_exchange_test PRIVATE cxx_std_20)
add_test(NAME enchanting_exchange COMMAND enchanting_exchange_test)

# Compile the actual shared selector and requirement catalog with small world-store stubs.
set(enchanting_source_dir "${CMAKE_CURRENT_BINARY_DIR}/enchanting-source")
foreach(enchanting_file IN ITEMS Enchanting.cpp Enchanting.h EnchantingPolicy.h)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/${enchanting_file}" "${enchanting_source_dir}/${enchanting_file}" COPYONLY)
endforeach()
add_executable(enchanting_runtime_test enchanting_runtime_test.cpp "${enchanting_source_dir}/Enchanting.cpp")
target_include_directories(enchanting_runtime_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/enchanting_stubs" "${enchanting_source_dir}")
target_compile_features(enchanting_runtime_test PRIVATE cxx_std_20)
add_test(NAME enchanting_runtime COMMAND enchanting_runtime_test)

# Run both production upgrade scorers, including the Item* forwarding overload.
# Source overrides let the same fixture demonstrate the old enchanted-item bug.
set(ITEM_UPGRADE_WEIGHT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/RandomItemMgr.cpp" CACHE FILEPATH "Item upgrade weight source under test")
set(ITEM_EXCHANGE_WEIGHT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/QuestItemExchange.cpp" CACHE FILEPATH "Exchange item weight source under test")
function(extract_item_weight_source source begin_marker end_marker output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" source_text)
    string(FIND "${source_text}" "${begin_marker}" begin_offset)
    string(FIND "${source_text}" "${end_marker}" end_offset)
    if(begin_offset LESS 0 OR end_offset LESS begin_offset)
        message(FATAL_ERROR "Item weight test extraction markers are missing in ${source}")
    endif()
    math(EXPR source_length "${end_offset} - ${begin_offset}")
    string(SUBSTRING "${source_text}" ${begin_offset} ${source_length} extracted)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${extracted}")
endfunction()
extract_item_weight_source("${ITEM_UPGRADE_WEIGHT_SOURCE}" "uint32 RandomItemMgr::ItemStatWeight(Player* player, ItemQualifier& qualifier)" "uint32 RandomItemMgr::CalculateSingleStatWeight(" item_upgrade_weight_source.inc)
extract_item_weight_source("${ITEM_EXCHANGE_WEIGHT_SOURCE}" "    uint64 ItemWeight(Player* bot, uint32 spec, ItemQualifier& item)" "    struct SlotState" item_exchange_weight_source.inc)
foreach(weight_variant IN ITEMS classic expansion)
    add_executable(item_upgrade_weight_${weight_variant}_test item_upgrade_weight_test.cpp)
    target_include_directories(item_upgrade_weight_${weight_variant}_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_features(item_upgrade_weight_${weight_variant}_test PRIVATE cxx_std_20)
    if(weight_variant STREQUAL "classic")
        target_compile_definitions(item_upgrade_weight_${weight_variant}_test PRIVATE MANGOSBOT_ZERO)
    endif()
    add_test(NAME item_upgrade_weight_${weight_variant} COMMAND item_upgrade_weight_${weight_variant}_test)
endforeach()

# Execute the actual Classic Alterac opening, live-threat and interruption flow.
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/AlteracCapture.cpp"
    "namespace ai::alterac" "" alterac_capture_source.inc)
add_executable(alterac_capture_runtime_test alterac_capture_runtime_test.cpp)
target_include_directories(alterac_capture_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(alterac_capture_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(alterac_capture_runtime_test PRIVATE MANGOSBOT_ZERO)
add_test(NAME alterac_capture_runtime COMMAND alterac_capture_runtime_test)

# Execute the event-to-state mapping, team plan and node interaction boundaries.
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/AlteracValleyTactics.cpp"
    "namespace ai::alterac" "" alterac_tactics_source.inc)
add_executable(alterac_tactics_runtime_test alterac_tactics_runtime_test.cpp)
target_include_directories(alterac_tactics_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(alterac_tactics_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(alterac_tactics_runtime_test PRIVATE MANGOSBOT_ZERO)
add_test(NAME alterac_tactics_runtime COMMAND alterac_tactics_runtime_test)

# Exercise the actual AV route monitor, defender target gate and enemy selection.
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/AlteracCombat.cpp"
    "namespace ai::alterac" "" alterac_combat_source.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/PlayerbotAI.cpp"
    "    // Alterac capture interruption" "    // A failed native chase" alterac_combat_outer.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/EnemyPlayerValue.cpp"
    "bool EnemyPlayersValue::IsAllowedAlteracTarget(" "bool EnemyPlayersValue::IsReachable(" alterac_enemy_target_gate.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/EnemyPlayerValue.cpp"
    "ObjectGuid EnemyPlayerValue::Calculate(" "float EnemyPlayerValue::GetMaxAttackDistance(" alterac_enemy_selection.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/BattleGroundTactics.cpp"
    "bool BGTactics::moveAlteracObjective(" "bool BGTactics::Execute(" alterac_objective_source.inc)
add_executable(alterac_combat_runtime_test alterac_combat_runtime_test.cpp)
target_include_directories(alterac_combat_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(alterac_combat_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(alterac_combat_runtime_test PRIVATE MANGOSBOT_ZERO)
add_test(NAME alterac_combat_runtime COMMAND alterac_combat_runtime_test)

# Exercise the real clear-bags command, registrations and chat event parser.
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/DestroyItemAction.cpp"
    "bool ClearBagsAction::Execute(Event& event)" "" clear_bags_action_source.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/triggers/ChatCommandTrigger.h"
    "namespace ai" "" clear_bags_chat_trigger.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/ChatActionContext.h"
    "            creators[\"clear bags\"]" "            creators[\"load ai\"]" clear_bags_action_registration.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/triggers/ChatTriggerContext.h"
    "            creators[\"clear bags\"]" "            creators[\"emote\"]" clear_bags_trigger_registration.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp"
    "    supported.push_back(\"clear bags\");" "    supported.push_back(\"load ai\");" clear_bags_supported.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/ExternalEventHelper.h"
    "        bool ParseChatCommand(" "        bool HandlePacket(" clear_bags_parse_command.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/ExternalEventHelper.h"
    "        bool HandleCommand(" "    private:" clear_bags_handle_command.inc)
add_executable(clear_bags_runtime_test clear_bags_runtime_test.cpp)
target_include_directories(clear_bags_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(clear_bags_runtime_test PRIVATE cxx_std_20)
add_test(NAME clear_bags_runtime COMMAND clear_bags_runtime_test)

# Run actual loot admission and manual command aliases against controlled world objects.
set(LOOT_MASTER_ADMISSION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/AddLootAction.cpp" CACHE FILEPATH "Loot admission source under test")
extract_stealth_source("${LOOT_MASTER_ADMISSION_SOURCE}"
    "bool AddAllLootAction::Execute(Event& event)" "bool AddLootAction::isUseful()" loot_master_scan.inc)
extract_stealth_source("${LOOT_MASTER_ADMISSION_SOURCE}"
    "bool AddAllLootAction::AddLoot(Player* requester, ObjectGuid guid)" "bool AddGatheringLootAction::AddLoot(" loot_master_admission.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/triggers/ChatTriggerContext.h"
    "            creators[\"loot\"]" "            creators[\"release\"]" loot_master_trigger_registration.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/ChatActionContext.h"
    "            creators[\"loot\"]" "            creators[\"roll\"]" loot_master_action_registration.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp"
    "    // Manual loot aliases share the complete scan/select/move/open action chain."
    "    triggers.push_back(new TriggerNode(\n        \"u\","
    loot_master_command_chain.inc)
add_executable(loot_master_runtime_test loot_master_runtime_test.cpp)
target_include_directories(loot_master_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(loot_master_runtime_test PRIVATE cxx_std_20)
add_test(NAME loot_master_runtime COMMAND loot_master_runtime_test)

# Keep task ownership, real loot eligibility and the packet-to-inventory flow coherent.
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/LootObjectStack.h"
    "namespace ai" "" loot_task_declarations.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/LootObjectStack.cpp"
    "bool LootObject::IsWithinLootDistance(Player* bot)" "" loot_task_source.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/LootObjectStack.cpp"
    "    Creature* creature = ai->GetCreature(guid);" "    GameObject* go = ai->GetGameObject(guid);" loot_task_creature_refresh.inc)
add_executable(loot_task_runtime_test loot_task_runtime_test.cpp)
target_include_directories(loot_task_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(loot_task_runtime_test PRIVATE cxx_std_20)
add_test(NAME loot_task_runtime COMMAND loot_task_runtime_test)

extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/LootValues.cpp"
    "bool ShouldLootObject::Calculate()" "void ActiveRolls::CleanUp(" loot_permissions_source.inc)
foreach(loot_variant IN ITEMS classic expansion)
    add_executable(loot_permissions_${loot_variant}_test loot_permissions_runtime_test.cpp)
    target_include_directories(loot_permissions_${loot_variant}_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_features(loot_permissions_${loot_variant}_test PRIVATE cxx_std_20)
    if(loot_variant STREQUAL "classic")
        target_compile_definitions(loot_permissions_${loot_variant}_test PRIVATE MANGOSBOT_ZERO)
    endif()
    add_test(NAME loot_permissions_${loot_variant} COMMAND loot_permissions_${loot_variant}_test)
endforeach()

extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/LootObjectStack.cpp"
    "bool LootObject::IsWithinLootDistance(Player* bot)" "bool LootObjectStack::Add(ObjectGuid guid)" loot_flow_distance.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/LootAction.cpp"
    "bool LootAction::Execute(Event& event)" "enum ProfessionSpells" loot_flow_select.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/LootAction.cpp"
    "bool OpenLootAction::Execute(Event& event)" "bool OpenLootAction::DoLoot(" loot_flow_open.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/LootAction.cpp"
    "bool StoreLootAction::Execute(Event& event)" "bool StoreLootAction::IsLootAllowed(" loot_flow_store.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/actions/MovementActions.cpp"
    "bool MoveToLootAction::Execute(Event& event)" "bool MoveOutOfEnemyContactAction::Execute(" loot_flow_move.inc)
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/values/AvailableLootValue.h"
    "    class CanLootValue" "" loot_flow_can_loot.inc)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/loot_flow_can_loot.inc" loot_can_loot_class)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/loot_flow_can_loot.inc" "namespace ai\n{\n${loot_can_loot_class}")
extract_stealth_source("${CMAKE_CURRENT_SOURCE_DIR}/../playerbot/strategy/triggers/LootTriggers.cpp"
    "bool FarFromCurrentLootTrigger::IsActive()" "bool CanLootTrigger::IsActive()" loot_flow_far_trigger.inc)
add_executable(loot_flow_runtime_test loot_flow_runtime_test.cpp)
target_include_directories(loot_flow_runtime_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(loot_flow_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(loot_flow_runtime_test PRIVATE MANGOSBOT_ZERO)
add_test(NAME loot_flow_runtime COMMAND loot_flow_runtime_test)
