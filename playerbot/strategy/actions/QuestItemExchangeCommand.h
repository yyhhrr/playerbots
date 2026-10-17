#pragma once

#include "NeedRollCommand.h"

namespace ai::quest_exchange_command
{
    namespace detail
    {
        inline bool HasAsciiPrefix(std::string_view text, std::string_view prefix)
        {
            if (text.size() < prefix.size())
                return false;
            for (std::size_t i = 0; i < prefix.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i])
                    return false;
            return true;
        }
    }

    inline bool ParseCommand(std::string_view text, need_roll::ItemLink& out)
    {
        out = {};
        text = need_roll::detail::Trim(text);
        // UTF-8 for the Chinese command, independent of the MSVC source code page.
        constexpr std::string_view chinese = "\xE5\x85\x91\xE6\x8D\xA2";
        constexpr std::string_view english = "exchange item";
        if (text.substr(0, chinese.size()) == chinese)
            return need_roll::ParseItemLink(text.substr(chinese.size()), out);
        if (detail::HasAsciiPrefix(text, english))
            return need_roll::ParseItemLink(text.substr(english.size()), out);
        return false;
    }
}
