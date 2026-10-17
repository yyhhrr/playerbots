#include "playerbot/strategy/actions/QuestItemExchangeCommand.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const std::string& message)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void Reject(const std::string& text, const std::string& message)
    {
        ai::need_roll::ItemLink output{"old link", "old qualifier"};
        Check(!ai::quest_exchange_command::ParseCommand(text, output), message);
        Check(output.link.empty() && output.qualifier.empty(), "rejected input clears the output");
    }
}

int main()
{
    const auto ParseCommand = &ai::quest_exchange_command::ParseCommand;
    const std::string command = "\xE5\x85\x91\xE6\x8D\xA2";
    const std::string qualifier = "22349:0:0:0";
    const std::string link = "|cffa335ee|Hitem:" + qualifier + "|h[Desecrated Breastplate]|h|r";
    const std::string plain = "|Hitem:5881|h[Head of Kelris]|h";
    ai::need_roll::ItemLink output;

    Check(ParseCommand(command + " " + link, output), "Chinese command accepts one complete item hyperlink");
    Check(output.link == link && output.qualifier == qualifier, "Chinese command preserves the exact link and qualifier");
    Check(ParseCommand(command + link, output), "Chinese command also accepts a directly attached item link");
    Check(ParseCommand(" \t" + command + "  " + plain + " \t", output), "surrounding ASCII whitespace and uncolored links are accepted");
    Check(output.link == plain && output.qualifier == "5881", "uncolored link is kept unchanged");
    Check(ParseCommand("exchange item " + link, output), "English alias accepts a complete hyperlink");
    Check(ParseCommand("ExChAnGe ItEm " + link, output), "English alias is case insensitive");
    Check(output.link == link, "English case folding never alters item text");

    const std::string chineseLabel = "\xE8\xA2\xAB\xE7\x8E\xB7\xE6\xB1\xA1\xE7\x9A\x84\xE8\x83\xB8\xE7\x94\xB2";
    const std::string randomized = "|cFF0070dD|Hitem:24624:1886:::::-15:123456|h[" + chineseLabel + "]|h|r";
    Check(ParseCommand(command + " " + randomized, output), "UTF-8 labels and signed random properties are accepted");
    Check(output.qualifier == "24624:1886:::::-15:123456", "all random property qualifier fields are preserved");
    const std::string longLink = "|cffa335ee|Hitem:22349:0:0:0|h[" + std::string(300, 'x') + chineseLabel + "]|h|r";
    Check(ParseCommand(command + " " + longLink, output), "links longer than 255 bytes parse correctly");
    Check(output.link == longLink, "long item labels are not truncated");

    Reject("", "empty message is rejected");
    Reject(command, "Chinese command requires an item");
    Reject("exchange item", "English alias requires an item");
    Reject(command + " 22349", "bare numeric item id is rejected");
    Reject(command + " [Desecrated Breastplate]", "display text without a hyperlink is rejected");
    Reject(command + " " + link + " " + plain, "multiple links are rejected");
    Reject(command + " " + link + " please", "trailing words are rejected");
    Reject(command + " " + link + " 1", "trailing counts are rejected");
    Reject(command + " " + link + "?", "discussion punctuation is rejected");
    Reject("please " + command + " " + link, "ordinary discussion before the command is rejected");
    Reject(link + command, "suffix exchange commands are rejected");
    Reject("exchange items " + link, "a longer English word is not the alias");
    Reject(command + " |Hspell:22349|h[Spell]|h", "non-item hyperlinks are rejected");
    Reject(command + " |Hitem:0:0|h[Zero]|h", "item zero is rejected");
    Reject(command + " |Hitem:-1:0|h[Negative]|h", "negative item id is rejected");
    Reject(command + " |Hitem:22349:abc|h[Bad qualifier]|h", "malformed item qualifiers are rejected");
    Reject(command + " |cffa335ee|Hitem:22349|h[Missing reset]|h", "unfinished color wrapper is rejected");
    Reject(command + " |cffZZ35ee|Hitem:22349|h[Bad color]|h|r", "malformed color wrapper is rejected");
    Reject(command + " ||Hitem:22349|h[Escaped]|h", "escaped link text is rejected");
    Reject(command + " |Hitem:22349|h[Bad|label]|h", "nested formatting in item names is rejected");
    Reject(command + " |Hitem:22349|h[Bad\nlabel]|h", "control characters in labels are rejected");
    Reject("!" + command + " " + link, "server command prefixes must be removed by the normal chat pipeline");

    std::cout << "Passed " << checks << " manual quest-item exchange command checks.\n";
}
