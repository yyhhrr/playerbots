#pragma once
#include <vector>
#include "playerbot/playerbot.h"
struct PlayerbotAIConfig { std::vector<uint32> questItemExchangeKeys; };
extern PlayerbotAIConfig sPlayerbotAIConfig;
