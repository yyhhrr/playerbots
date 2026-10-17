-- Run with a Lua 5.0 interpreter against the installed, complete addon:
-- lua rollmaster_need_roll_test.lua "path/to/RollMaster.lua"
local addonPath = arg and arg[1]
if not addonPath then error("Pass the installed RollMaster.lua path") end

local checks = 0
local function check(condition, description)
    checks = checks + 1
    if not condition then error("FAIL: " .. description) end
end

local frames = {}
local methods = {}
local function noop() end
local noopNames = {
    "AddLine", "AddMessage", "Disable", "Enable", "EnableMouse", "EnableMouseWheel",
    "RegisterForClicks", "RegisterForDrag", "SetAllPoints", "SetBackdrop", "SetFrameStrata",
    "SetHighlightTexture", "SetJustifyH", "SetMovable", "SetOwner", "SetParent", "SetPoint",
    "SetScrollChild", "SetTextColor", "SetValueStep", "SetVerticalScroll", "StartMoving",
    "StopMovingOrSizing"
}
for _, name in ipairs(noopNames) do methods[name] = noop end

local function newFrame(name)
    local frame = { scripts = {}, events = {}, shown = false, height = 0, value = 0 }
    setmetatable(frame, { __index = methods })
    if name then _G[name] = frame; frames[name] = frame end
    return frame
end

function methods:SetScript(name, handler) self.scripts[name] = handler end
function methods:GetScript(name) return self.scripts[name] end
function methods:RegisterEvent(name) self.events[name] = true end
function methods:Show() self.shown = true end
function methods:Hide() self.shown = false end
function methods:IsShown() return self.shown end
function methods:SetHeight(height) self.height = height end
function methods:GetHeight() return self.height end
function methods:SetWidth(width) self.width = width end
function methods:SetValue(value) self.value = value end
function methods:GetValue() return self.value end
function methods:GetID() return self.id or 1 end
function methods:SetText(text) self.text = text end
function methods:SetTexture(texture) self.textureValue = texture end
function methods:SetMinMaxValues(minimum, maximum) self.minimum = minimum; self.maximum = maximum end
function methods:CreateFontString(name) return newFrame(name) end
function methods:CreateTexture(name) return newFrame(name) end

UIParent = newFrame("UIParent")
Minimap = newFrame("Minimap")
GameTooltip = newFrame("GameTooltip")
DEFAULT_CHAT_FRAME = newFrame("DEFAULT_CHAT_FRAME")
StaticPopupDialogs = {}
SlashCmdList = {}
CreateFrame = function(kind, name) return newFrame(name) end
getglobal = function(name) return _G[name] end
cos = math.cos
sin = math.sin

local ownName = "测试玩家"
local partyMembers = 1
local raidMembers = 0
local sent = {}
local popups = {}
local echoRequests = false
local function dispatch(eventName, first, second)
    event = eventName; arg1 = first; arg2 = second
    local handler = RollMasterFrame:GetScript("OnEvent")
    if handler then handler() end
end

GetNumPartyMembers = function() return partyMembers end
GetNumRaidMembers = function() return raidMembers end
UnitInRaid = function() return raidMembers > 0 end
UnitInParty = function() return partyMembers > 0 end
UnitName = function(unit) if unit == "player" then return ownName end end
UnitExists = function() return false end
UnitIsPlayer = function() return false end
GetRealZoneText = function() return "测试副本" end
GetZoneText = GetRealZoneText
GetLootMethod = function() return "master" end
IsRaidLeader = function() return true end
IsRaidOfficer = function() return false end
IsPartyLeader = function() return true end
date = function() return "2026-10-17" end
RequestRaidInfo = noop
StaticPopup_Hide = noop
StaticPopup_Show = function(name, first, second)
    table.insert(popups, { name = name, first = first, second = second })
end
SendChatMessage = function(text, channel)
    table.insert(sent, { text = text, channel = channel })
    if echoRequests and string.find(text, "^要的ROLL ") then
        dispatch("CHAT_MSG_" .. channel, text, ownName)
    end
end

-- Do not let a newer interpreter hide a dependency on the post-1.12 API.
string.match = nil
dofile(addonPath)
dispatch("ADDON_LOADED", "RollMaster")
check(RollMasterFrame.events.CHAT_MSG_PARTY, "party chat event is registered")
check(RollMasterFrame.events.CHAT_MSG_RAID, "raid chat event is registered")
check(RollMasterFrame.events.CHAT_MSG_RAID_WARNING, "raid warning chat event is registered")

local link = "|cffa335ee|Hitem:19019:0:0:0|h[测试装备]|h|r"
local otherLink = "|cff0070dd|Hitem:19364:0:0:0|h[另一件装备]|h|r"
local sameItemOtherColor = "|cff0070dd|Hitem:19019:0:0:0|h[测试装备]|h|r"
local material = "|cffffffff|Hitem:10940:0:0:0|h[奇异之尘]|h|r"

local function tick(seconds)
    for i = 1, seconds do
        local handler = RollMasterFrame:GetScript("OnUpdate")
        if handler then arg1 = 1; handler() end
    end
end

local function reset(raid)
    partyMembers = 0; raidMembers = 0
    dispatch("PARTY_MEMBERS_CHANGED")
    partyMembers = raid and 0 or 1
    raidMembers = raid and 5 or 0
    sent = {}; popups = {}; echoRequests = false
end

local function system(text) dispatch("CHAT_MSG_SYSTEM", text) end
local function active() return RollMasterFrame:GetScript("OnUpdate") end

reset(false)
dispatch("CHAT_MSG_PARTY", link .. "要的rOlL", ownName)
check(active(), "manual suffix starts a round")
check(table.getn(sent) == 0, "manual suffix does not resend a request")
check(RollMaster_MainFrame:IsShown(), "manual start displays the tracker")
check(RollMaster_StatusText.text == "正在Roll: 测试装备", "manual start derives item name from link")
check(RollMaster_CountdownText.text == "倒计时: 10秒", "manual start initializes countdown")

system("近战机器人掷出55 (1-100)")
system("|cff00ff00|Hplayer:治疗机器人:Realm|h[治疗机器人]|h|r掷出了88 （1-100）")
system("无效范围掷出199 (1-200)")
check(RollMaster_RollScrollChild.height == 48, "only standard 1-100 rolls are counted")
check(RollMaster_RollLine1.text.text == "近战机器人: 55点", "first roll refreshes visible rows")
check(RollMaster_RollLine2.text.text == "治疗机器人: 88点", "fullwidth brackets and formatted player names are parsed")
tick(3)
local firstTimer = active()
dispatch("CHAT_MSG_PARTY", "要的ROLL " .. sameItemOtherColor, ownName)
check(active() == firstTimer, "same item echo preserves the timer even if color differs")
check(RollMaster_CountdownText.text == "倒计时: 7秒", "duplicate echo does not restart countdown")
check(RollMaster_RollScrollChild.height == 48, "duplicate echo does not clear recorded rolls")
dispatch("CHAT_MSG_PARTY", otherLink .. "要的roll", "其他玩家")
check(active() == firstTimer, "another player's request does not replace local round")
tick(7)
check(not active(), "countdown finishes after original ten seconds")
check(table.getn(sent) == 1, "manual round sends only its completion announcement")
check(string.find(sent[1].text, "治疗机器人 以 88 点获得", 1, true), "highest valid roll wins")
check(sent[1].channel == "PARTY", "party round announces back to party")
check(RollMaster_StatusText.text == "就绪", "completion restores ready status")

reset(true)
RollMaster_CurrentLoot = { { link = otherLink, name = "另一件装备", index = 6, rarity = 3 } }
dispatch("CHAT_MSG_RAID", "  要的RoLl  " .. otherLink .. "  ", ownName)
check(active(), "manual prefix accepts mixed case and whitespace")
check(table.getn(sent) == 0, "manual prefix does not send a second request")
system("团队机器人掷出100 (1-100)")
tick(10)
check(sent[1].channel == "RAID", "raid round announces back to raid")
check(not RollMaster_PendingAssignSlot, "manual request does not inherit cached loot slot")
check(table.getn(popups) == 0, "manual request never opens assignment confirmation")

reset(true)
dispatch("CHAT_MSG_RAID_WARNING", otherLink .. "要的ROLL", ownName)
check(active(), "manual raid warning starts a round")
check(table.getn(sent) == 0, "manual raid warning does not resend the request")
system("警告频道机器人掷出64 (1-100)")
tick(10)
check(table.getn(sent) == 1 and sent[1].channel == "RAID", "raid warning result uses ordinary raid channel")
check(string.find(sent[1].text, "警告频道机器人 以 64 点获得", 1, true), "raid warning round records roll results")

reset(true)
echoRequests = true
AnnounceNeed(link, "测试装备", 3)
check(table.getn(sent) == 1 and sent[1].text == "要的ROLL " .. link, "button sends original prefix request exactly once")
local buttonTimer = active()
system("按钮机器人掷出79 (1-100)")
tick(2)
AnnounceNeed(link, "测试装备", 3)
check(active() == buttonTimer, "repeated button preserves active round")
check(table.getn(sent) == 1, "repeated button does not send another request")
check(RollMaster_CountdownText.text == "倒计时: 8秒", "button echo and repetition do not reset countdown")
check(RollMaster_RollScrollChild.height == 24, "button echo preserves existing roll")
tick(8)
check(table.getn(sent) == 2, "button round sends one request and one completion")
check(RollMaster_PendingAssignSlot == 3, "button keeps original assignment slot")
check(table.getn(popups) == 1 and popups[1].name == "ROLLMASTER_ASSIGN_CONFIRM", "button retains assignment confirmation behavior")

local invalidRequests = {
    { "CHAT_MSG_PARTY", link .. "要的roll", "其他玩家" },
    { "CHAT_MSG_WHISPER", link .. "要的roll", ownName },
    { "CHAT_MSG_PARTY", "讨论一下 " .. link .. "要的roll", ownName },
    { "CHAT_MSG_PARTY", link .. "要的roll 吗", ownName },
    { "CHAT_MSG_PARTY", link .. otherLink .. "要的roll", ownName },
    { "CHAT_MSG_PARTY", "要的roll " .. link .. otherLink, ownName },
    { "CHAT_MSG_PARTY", "要的roll", ownName },
    { "CHAT_MSG_PARTY", "[测试装备]要的roll", ownName },
    { "CHAT_MSG_PARTY", "|Hitem:19019:0:0:0|h[要的roll]|h", ownName },
    { "CHAT_MSG_PARTY", material .. "要的roll", ownName }
}
for i, request in ipairs(invalidRequests) do
    reset(false)
    dispatch(request[1], request[2], request[3])
    check(not active() and table.getn(sent) == 0, "unrelated or invalid message " .. i .. " does not start or send")
end

reset(false)
dispatch("CHAT_MSG_PARTY", "|Hitem:19019:0:0:0|h[测试装备]|h要的roll", ownName)
check(active(), "uncolored item link starts a round")
tick(10)
check(table.getn(sent) == 1 and string.find(sent[1].text, "无人Roll", 1, true), "empty round sends existing no-roll result")

reset(false)
partyMembers = 0
dispatch("CHAT_MSG_PARTY", link .. "要的roll", ownName)
check(not active() and table.getn(sent) == 0, "solo requests do not start a round")

print("PASS: " .. checks .. " RollMaster integration checks using " .. addonPath)
