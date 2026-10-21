#include "playerbot/GuildBotLogin.h"

#include <cstdlib>
#include <iostream>

using namespace ai::guild_bot_login;

namespace
{
    unsigned checks = 0;
    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    Check(Select(12, 12, true, false, false, false) == Candidate::Login, "offline guild bot is requested");
    Check(Select(0, 0, true, false, false, false) == Candidate::Skip, "no guild is rejected");
    Check(Select(12, 13, true, false, false, false) == Candidate::Skip, "another guild is excluded");
    Check(Select(12, 0, true, false, false, false) == Candidate::Skip, "departed guild member is excluded");
    Check(Select(12, 12, false, false, false, false) == Candidate::Skip, "offline human character is excluded");
    Check(Select(12, 12, true, true, false, false) == Candidate::Skip, "active human account is excluded");
    Check(Select(12, 12, true, true, true, true) == Candidate::Skip, "human safety precedes online state");
    Check(Select(12, 12, true, false, true, false) == Candidate::Online, "online bot is not logged in twice");
    Check(Select(12, 12, true, false, false, true) == Candidate::Pending, "pending login is not duplicated");
    Check(Select(12, 12, true, false, true, true) == Candidate::Online, "live character overrides stale pending marker");

    Protection protection;
    const auto start = Protection::Clock::time_point{} + std::chrono::hours(24);
    Check(!protection.Contains(42, start), "ordinary bots have no protection");
    protection.Keep(42, 1800, start);
    Check(protection.Contains(42, start), "requested bot is immediately protected");
    Check(!protection.Contains(43, start), "protection is per bot");
    Check(protection.Contains(42, start + std::chrono::seconds(1799)), "protection lasts for configured interval");
    Check(!protection.Contains(42, start + std::chrono::seconds(1800)), "normal scheduling resumes at expiry");
    protection.Keep(42, 1800, start);
    protection.Keep(42, 60, start + std::chrono::seconds(30));
    Check(protection.Contains(42, start + std::chrono::seconds(1799)), "repeat cannot shorten active protection");
    protection.Keep(42, 1800, start + std::chrono::seconds(1000));
    Check(protection.Contains(42, start + std::chrono::seconds(1800)), "repeat can extend a pending request");
    Check(!protection.Contains(42, start + std::chrono::seconds(2800)), "extended protection still expires");
    protection.Keep(42, 1800, start);
    protection.Clear(42);
    Check(!protection.Contains(42, start), "explicit logout or failed login cancels protection");
    protection.Clear(42);
    Check(!protection.Contains(42, start), "repeated cancellation is safe");

    std::cout << checks << " guild login checks passed\n";
}
