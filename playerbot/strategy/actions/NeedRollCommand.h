#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace ai::need_roll
{
    struct ItemLink
    {
        std::string link;
        std::string qualifier;
    };

    namespace detail
    {
        inline std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && static_cast<unsigned char>(text.front()) < 128 &&
                std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && static_cast<unsigned char>(text.back()) < 128 &&
                std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return text;
        }

        inline bool IsDecimal(std::string_view text, bool allowNegative)
        {
            if (allowNegative && !text.empty() && text.front() == '-')
                text.remove_prefix(1);
            if (text.empty())
                return false;
            for (char ch : text)
                if (ch < '0' || ch > '9')
                    return false;
            return true;
        }

        inline bool IsQualifier(std::string_view qualifier)
        {
            const std::size_t separator = qualifier.find(':');
            const std::string_view id = qualifier.substr(0, separator);
            if (!IsDecimal(id, false) || id.find_first_not_of('0') == std::string_view::npos)
                return false;

            if (separator == std::string_view::npos)
                return true;

            qualifier.remove_prefix(separator + 1);
            while (true)
            {
                const std::size_t next = qualifier.find(':');
                const std::string_view field = qualifier.substr(0, next);
                if (!field.empty() && !IsDecimal(field, true))
                    return false;
                if (next == std::string_view::npos)
                    return true;
                qualifier.remove_prefix(next + 1);
            }
        }

        inline bool IsCommandWord(std::string_view text)
        {
            // UTF-8 for the Chinese command prefix, independent of the compiler code page.
            constexpr std::string_view prefix = "\xE8\xA6\x81\xE7\x9A\x84";
            constexpr std::string_view roll = "roll";
            if (text.size() != prefix.size() + roll.size() || text.substr(0, prefix.size()) != prefix)
                return false;
            text.remove_prefix(prefix.size());
            for (std::size_t i = 0; i < roll.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(text[i])) != roll[i])
                    return false;
            return true;
        }
    }

    inline bool ParseItemLink(std::string_view text, ItemLink& out)
    {
        out = {};
        text = detail::Trim(text);
        const std::string_view fullLink = text;
        const bool hasColor = text.substr(0, 2) == "|c";
        if (hasColor)
        {
            if (text.size() < 10)
                return false;
            for (char ch : text.substr(2, 8))
                if (!std::isxdigit(static_cast<unsigned char>(ch)))
                    return false;
            text.remove_prefix(10);
        }

        constexpr std::string_view itemPrefix = "|Hitem:";
        if (text.substr(0, itemPrefix.size()) != itemPrefix)
            return false;
        text.remove_prefix(itemPrefix.size());

        const std::size_t qualifierEnd = text.find("|h");
        if (qualifierEnd == std::string_view::npos)
            return false;
        const std::string_view qualifier = text.substr(0, qualifierEnd);
        if (!detail::IsQualifier(qualifier))
            return false;
        text.remove_prefix(qualifierEnd + 2);

        const std::size_t labelEnd = text.find("]|h");
        if (text.empty() || text.front() != '[' || labelEnd == std::string_view::npos || labelEnd <= 1)
            return false;
        for (unsigned char ch : text.substr(1, labelEnd - 1))
            if (ch < 32 || ch == 127 || ch == '|' || ch == '[' || ch == ']')
                return false;
        text.remove_prefix(labelEnd + 3);
        if (text != (hasColor ? "|r" : ""))
            return false;

        out.link.assign(fullLink);
        out.qualifier.assign(qualifier);
        return true;
    }

    inline bool ParseCommand(std::string_view text, ItemLink& out)
    {
        out = {};
        text = detail::Trim(text);
        constexpr std::size_t commandLength = 10; // Six UTF-8 bytes followed by "roll".
        if (text.size() <= commandLength)
            return false;
        if (detail::IsCommandWord(text.substr(0, commandLength)))
            return ParseItemLink(text.substr(commandLength), out);
        if (detail::IsCommandWord(text.substr(text.size() - commandLength)))
            return ParseItemLink(text.substr(0, text.size() - commandLength), out);
        return false;
    }
}
