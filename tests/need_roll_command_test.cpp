#include "playerbot/strategy/actions/NeedRollCommand.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void RejectCommand(const std::string& text, const std::string& description)
    {
        ai::need_roll::ItemLink result{"previous link", "previous qualifier"};
        Check(!ai::need_roll::ParseCommand(text, result), description);
        Check(result.link.empty() && result.qualifier.empty(), "a rejected command clears its output");
    }
}

int main()
{
    using ai::need_roll::ItemLink;
    using ai::need_roll::ParseCommand;
    using ai::need_roll::ParseItemLink;

    const std::string command = "\xE8\xA6\x81\xE7\x9A\x84roll";
    const std::string uppercaseCommand = "\xE8\xA6\x81\xE7\x9A\x84ROLL";
    const std::string mixedCommand = "\xE8\xA6\x81\xE7\x9A\x84rOlL";
    const std::string qualifier = "19019:0:0:0";
    const std::string classicLink = "|cffff8000|Hitem:" + qualifier + "|h[Thunderfury, Blessed Blade of the Windseeker]|h|r";
    ItemLink result;

    Check(ParseCommand(classicLink + command, result), "a Classic item link directly followed by the command is accepted");
    Check(result.link == classicLink && result.qualifier == qualifier, "parsing keeps the exact item link and Classic qualifier");
    Check(ParseCommand("  " + classicLink + " \t" + mixedCommand + "  ", result), "surrounding whitespace and mixed ASCII case are accepted");
    Check(ParseCommand(uppercaseCommand + " " + classicLink, result), "the prefix form used by RollMaster buttons is accepted");
    Check(ParseCommand(command + classicLink, result), "the prefix form also permits no separating space");

    const std::string chineseName = "\xE9\x98\xB5\xE4\xBA\xA1\xE5\x8B\x87\xE5\xA3\xAB\xE7\x9A\x84\xE8\x83\xB8\xE7\x94\xB2";
    const std::string randomQualifier = "24624:1886:::::-15:123456";
    const std::string chineseLink = "|cFF0070dD|Hitem:" + randomQualifier + "|h[" + chineseName + "]|h|r";
    Check(ParseCommand(chineseLink + uppercaseCommand, result), "Chinese labels and hexadecimal color case are accepted");
    Check(result.qualifier == randomQualifier, "empty fields, enchantment and signed random properties remain intact");

    const std::string plainLink = "|Hitem:19019|h[Thunderfury]|h";
    Check(ParseItemLink(plainLink, result), "an uncolored item hyperlink is accepted");
    Check(result.link == plainLink && result.qualifier == "19019", "an item link without additional fields keeps its item id");
    Check(ParseCommand(plainLink + command, result), "an uncolored hyperlink works in a command");
    Check(ParseItemLink("|Hitem:19019:0:-7::|h[Test]|h", result), "empty trailing qualifier fields are valid");

    const std::string longLink = "|cffa335ee|Hitem:19019:0:0:0|h[" + std::string(300, 'x') + chineseName + "]|h|r";
    Check(longLink.size() > 255 && ParseCommand(longLink + command, result), "a hyperlink longer than 255 bytes parses without a narrow scan offset");
    Check(result.link == longLink && result.qualifier == qualifier, "a long UTF-8 item label does not alter the qualifier");

    RejectCommand("", "empty chat does not trigger");
    RejectCommand(command, "a command without an item does not trigger");
    RejectCommand("[Thunderfury]" + command, "a displayed name without a real item hyperlink is rejected");
    RejectCommand("19019 " + command, "a bare numeric item id is rejected");
    RejectCommand("|Hspell:19019|h[Spell]|h" + command, "a non-item hyperlink is rejected");
    RejectCommand("I said " + classicLink + command, "ordinary discussion before the link is rejected");
    RejectCommand(classicLink + command + "?", "a discussion suffix after the command is rejected");
    RejectCommand(classicLink + "do not " + command, "extra words between the link and command are rejected");
    RejectCommand(classicLink + command + " 1 100", "an arbitrary random roll range is rejected");
    RejectCommand(classicLink + " " + plainLink + command, "multiple item hyperlinks are rejected");
    RejectCommand(classicLink + " " + command + " " + classicLink, "links surrounding the command are rejected");
    RejectCommand("|cffff8000|Hitem:19019:0|h[Missing reset]|h" + command, "an unfinished colored hyperlink is rejected");
    RejectCommand("|cffff80ZZ|Hitem:19019|h[Bad color]|h|r" + command, "a malformed color is rejected");
    RejectCommand("|Hitem:0:0|h[Zero id]|h" + command, "item id zero is rejected");
    RejectCommand("|Hitem:-1:0|h[Negative id]|h" + command, "a negative item id is rejected");
    RejectCommand("|Hitem:19019:abc|h[Bad qualifier]|h" + command, "non-numeric qualifier fields are rejected");
    RejectCommand("|Hitem:19019:-|h[Bad signed field]|h" + command, "a sign without digits is rejected");
    RejectCommand("|Hitem:19019|h[]|h" + command, "an empty displayed item name is rejected");
    RejectCommand("|Hitem:19019|h[Outer |Hitem:1|h[Inner]|h]|h" + command, "a nested fake hyperlink is rejected");

    std::cout << "Passed " << checks << " need-roll command checks.\n";
    return EXIT_SUCCESS;
}
