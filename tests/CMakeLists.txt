cmake_minimum_required(VERSION 3.16)
project(playerbots_tests LANGUAGES CXX)

enable_testing()

foreach(test_name IN ITEMS mage_aoe_geometry mage_aoe_policy need_roll_command quest_item_exchange quest_item_exchange_command guild_bot_login arathi_basin_policy arathi_basin_combat arathi_capture_policy)
    add_executable(${test_name}_test ${test_name}_test.cpp)
    target_include_directories(${test_name}_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_compile_features(${test_name}_test PRIVATE cxx_std_20)
    add_test(NAME ${test_name} COMMAND ${test_name}_test)
endforeach()

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
