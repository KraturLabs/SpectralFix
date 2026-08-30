// SPDX-License-Identifier: GPL-3.0-only
// Pure decision logic for the Direct3D8 vtable hook table. The actual memory
// writes stay in spectralfix.cpp; everything decided here is unit testable.

#pragma once

#include <cstddef>
#include <cstdint>

namespace spectralfix
{
    constexpr uint32_t kDrawChainMissThreshold = 3;

    // A snapshot of one vtable slot at the moment a decision is being made.
    struct HookSlotView
    {
        const void* ours{nullptr};    // SpectralFix's hook function for this slot.
        const void* current{nullptr}; // The value observed in the vtable right now.
        bool tracked{false};          // False once SpectralFix deliberately released the slot.
    };

    inline bool slot_is_intact(const HookSlotView& slot)
    {
        return !slot.tracked || slot.current == slot.ours;
    }

    // A slot released on purpose, such as the SetTexture observer handed back to
    // native Windows D3D8, is skipped rather than treated as a failure. Skipping it
    // is what keeps the remaining slots recoverable instead of stranding them.
    inline bool tracked_hooks_intact(const HookSlotView* slots, const size_t count)
    {
        if (slots == nullptr)
            return false;
        for (size_t i = 0; i < count; ++i)
        {
            if (!slot_is_intact(slots[i]))
                return false;
        }
        return true;
    }

    inline bool slot_is_displaced(const HookSlotView& slot)
    {
        return slot.tracked && slot.current != slot.ours;
    }

    constexpr bool must_retain_hooks_on_release(
        const bool hooksPublished,
        const bool enlargementPublished)
    {
        return hooksPublished && enlargementPublished;
    }

    enum class DrawChainHealth
    {
        owned,
        active,
        inconclusive,
        lost,
    };

    struct DrawChainSample
    {
        DrawChainHealth health{DrawChainHealth::inconclusive};
        uint32_t consecutiveMisses{0};
    };

    // A foreign function at the top of the DrawPrimitiveUP slot can still be
    // safely forwarding to SpectralFix. Treat one quiet sample as inconclusive;
    // only consecutive windows with presented frames but no intercepted draws
    // justify declaring that the chain has been lost.
    inline DrawChainSample evaluate_draw_chain_sample(
        const bool drawSlotOwned,
        const uint64_t interceptedNow,
        const uint64_t interceptedAtLastSample,
        const uint32_t previousMisses,
        const uint32_t missesRequired)
    {
        if (drawSlotOwned)
            return {DrawChainHealth::owned, 0};
        if (interceptedNow != interceptedAtLastSample)
            return {DrawChainHealth::active, 0};

        const auto misses = previousMisses + 1;
        return {
            misses >= missesRequired ? DrawChainHealth::lost : DrawChainHealth::inconclusive,
            misses,
        };
    }
}
