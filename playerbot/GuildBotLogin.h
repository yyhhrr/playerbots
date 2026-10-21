#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace ai
{
    namespace guild_bot_login
    {
        constexpr const char* ChineseCommand = "\xE5\x85\xAC\xE4\xBC\x9A\xE4\xB8\x8A\xE7\xBA\xBF"; // 公会上线
        constexpr const char* ChineseAlias = "\xE5\xB7\xA5\xE4\xBC\x9A\xE4\xB8\x8A\xE7\xBA\xBF"; // 工会上线

        enum class Candidate { Skip, Online, Pending, Login };

        inline Candidate Select(uint32_t requesterGuild, uint32_t memberGuild, bool randomAccount,
            bool realSession, bool online, bool pending)
        {
            if (!requesterGuild || requesterGuild != memberGuild || !randomAccount || realSession)
                return Candidate::Skip;
            if (online)
                return Candidate::Online;
            return pending ? Candidate::Pending : Candidate::Login;
        }

        // The asynchronous login planner reads these leases from its worker thread.
        // Keep them in memory: a manual login must not permanently change bot policy.
        class Protection
        {
        public:
            using Clock = std::chrono::steady_clock;

            void Keep(uint32_t guid, uint32_t seconds, Clock::time_point now = Clock::now())
            {
                std::lock_guard<std::mutex> lock(mutex);
                const auto until = now + std::chrono::seconds(seconds);
                auto& current = expiry[guid];
                if (until > current)
                    current = until;
            }

            bool Contains(uint32_t guid, Clock::time_point now = Clock::now())
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = expiry.find(guid);
                if (it == expiry.end())
                    return false;
                if (now < it->second)
                    return true;
                expiry.erase(it);
                return false;
            }

            void Clear(uint32_t guid)
            {
                std::lock_guard<std::mutex> lock(mutex);
                expiry.erase(guid);
            }

        private:
            std::mutex mutex;
            std::unordered_map<uint32_t, Clock::time_point> expiry;
        };
    }
}
