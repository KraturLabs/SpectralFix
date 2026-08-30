#include "hook_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace
{
    // Stand-ins for real function addresses.
    const void* const kOurCreate = reinterpret_cast<const void*>(0x1000);
    const void* const kOurSet    = reinterpret_cast<const void*>(0x1100);
    const void* const kOurDraw   = reinterpret_cast<const void*>(0x1200);
    const void* const kForeign   = reinterpret_cast<const void*>(0x9000);

    using Table = std::array<spectralfix::HookSlotView, 3>;

    Table intact_table()
    {
        return Table{{
            {kOurCreate, kOurCreate, true},
            {kOurSet, kOurSet, true},
            {kOurDraw, kOurDraw, true},
        }};
    }
}

int main()
{
    using namespace spectralfix;

    auto table = intact_table();
    if (!tracked_hooks_intact(table.data(), table.size()))
    {
        std::cerr << "an untouched table was reported as needing work\n";
        return 1;
    }

    // A foreign component above one slot is displacement. SpectralFix deliberately
    // leaves that owner in place because it may have saved our hook as its previous
    // function; writing ourselves above it could create a forwarding cycle.
    table = intact_table();
    table[2].current = kForeign;
    if (tracked_hooks_intact(table.data(), table.size()))
    {
        std::cerr << "a displaced slot was reported as intact\n";
        return 2;
    }
    if (!slot_is_displaced(table[2]) || slot_is_displaced(table[0]))
    {
        std::cerr << "late foreign ownership was not classified as displacement\n";
        return 3;
    }

    // A null current target is also displacement; production never writes above
    // any displaced target, null or otherwise.
    table = intact_table();
    table[1].current = nullptr;
    if (!slot_is_displaced(table[1]))
    {
        std::cerr << "a null tracked slot was not reported as displaced\n";
        return 4;
    }

    // A slot holding a different SpectralFix hook is still displacement and is
    // likewise never overwritten.
    table = intact_table();
    table[0].current = kOurDraw;
    if (!slot_is_displaced(table[0]))
    {
        std::cerr << "a mismatched SpectralFix hook was not reported as displaced\n";
        return 5;
    }

    // The native stage-zero query mode case: the SetTexture slot is deliberately
    // released. It must not count as displaced and must not make the remaining
    // tracked slots look unhealthy.
    table            = intact_table();
    table[1].tracked = false;
    table[1].current = nullptr;
    if (!tracked_hooks_intact(table.data(), table.size()))
    {
        std::cerr << "a deliberately released slot was treated as displacement\n";
        return 6;
    }
    if (slot_is_displaced(table[1]))
    {
        std::cerr << "a released slot was considered displaced\n";
        return 7;
    }

    // Defensive: a null table is never considered intact.
    if (tracked_hooks_intact(nullptr, 3))
    {
        std::cerr << "a null table was accepted\n";
        return 8;
    }

    // A foreign draw hook may still forward to SpectralFix. Activity proves the
    // chain survived and resets missed windows; quiet windows must accumulate
    // before the watchdog reports a loss.
    auto drawSample = evaluate_draw_chain_sample(false, 101, 100, 2, 3);
    if (drawSample.health != DrawChainHealth::active || drawSample.consecutiveMisses != 0)
    {
        std::cerr << "draw-chain activity did not reset the watchdog\n";
        return 9;
    }

    drawSample = evaluate_draw_chain_sample(false, 100, 100, 0, 3);
    if (drawSample.health != DrawChainHealth::inconclusive || drawSample.consecutiveMisses != 1)
    {
        std::cerr << "one quiet draw-chain window was treated as a loss\n";
        return 10;
    }
    drawSample = evaluate_draw_chain_sample(false, 100, 100, 2, 3);
    if (drawSample.health != DrawChainHealth::lost || drawSample.consecutiveMisses != 3)
    {
        std::cerr << "repeated quiet draw-chain windows did not report a loss\n";
        return 11;
    }

    drawSample = evaluate_draw_chain_sample(true, 100, 100, 2, 3);
    if (drawSample.health != DrawChainHealth::owned || drawSample.consecutiveMisses != 0)
    {
        std::cerr << "an owned draw slot did not clear the watchdog\n";
        return 12;
    }

    return 0;
}
