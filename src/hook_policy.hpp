// SPDX-License-Identifier: GPL-3.0-only
// Pure decision logic for the Direct3D8 vtable hook table. The actual memory
// writes stay in spectralfix.cpp; everything decided here is unit testable.

#pragma once

#include "selector_policy.hpp"

#include <cstddef>
#include <cstdint>

namespace spectralfix
{
    constexpr uint32_t kDrawChainMissThreshold = 3;
    constexpr uint64_t kDrawForwardingFreshFrames = 180;

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

    struct DrawForwardingEvidence
    {
        uint64_t interceptedCallbacks{0};
        uint64_t trustedRuntimeDraws{0};
        uint64_t trustedAtLastSample{0};
        bool forwardingObserved{false};
        uint64_t lastForwardingFrame{0};
        bool correctionLost{false};
        bool recoveryPending{false};
        uint32_t consecutiveMisses{0};
    };

    inline bool record_draw_callback(
        DrawForwardingEvidence& evidence,
        const DeviceIdentityResult deviceIdentity,
        const bool drawSlotOwned,
        const uint64_t currentFrame)
    {
        ++evidence.interceptedCallbacks;
        if (deviceIdentity != DeviceIdentityResult::exactPointer
            && deviceIdentity != DeviceIdentityResult::canonicalComIdentity)
            return false;

        ++evidence.trustedRuntimeDraws;
        if (!drawSlotOwned)
        {
            const bool recovering = evidence.correctionLost;
            evidence.forwardingObserved = true;
            evidence.lastForwardingFrame = currentFrame;
            evidence.correctionLost = false;
            if (recovering)
            {
                evidence.recoveryPending = true;
                evidence.consecutiveMisses = 0;
            }
        }
        return true;
    }

    inline void begin_draw_owner_epoch(
        DrawForwardingEvidence& evidence,
        const bool correctionLost)
    {
        evidence.forwardingObserved = false;
        evidence.correctionLost = correctionLost;
        evidence.recoveryPending = false;
        evidence.consecutiveMisses = 0;
        evidence.trustedAtLastSample = evidence.trustedRuntimeDraws;
    }

    enum class StageZeroQueryActivationResult
    {
        activated,
        alreadyActive,
        slotUnavailable,
        ownerUnavailable,
        ownerStillOurs,
    };

    constexpr bool stage_zero_query_activation_succeeded(
        const StageZeroQueryActivationResult result)
    {
        return result == StageZeroQueryActivationResult::activated
            || result == StageZeroQueryActivationResult::alreadyActive;
    }

    constexpr const char* stage_zero_query_activation_name(
        const StageZeroQueryActivationResult result)
    {
        switch (result)
        {
            case StageZeroQueryActivationResult::activated: return "activated";
            case StageZeroQueryActivationResult::alreadyActive: return "already-active";
            case StageZeroQueryActivationResult::slotUnavailable: return "slot-unavailable";
            case StageZeroQueryActivationResult::ownerUnavailable: return "owner-unavailable";
            default: return "owner-still-spectralfix";
        }
    }

    struct RuntimeCapabilityInput
    {
        bool createCallbackActive{false};
        bool drawSlotOwned{false};
        bool drawForwardingObserved{false};
        bool drawForwardingLost{false};
        bool setTextureSlotOwned{false};
        bool stageZeroQueryFallback{false};
        bool baseEnlargementAllowed{false};
        uint64_t currentFrame{0};
        uint64_t lastDrawForwardingFrame{0};
    };

    struct RuntimeCapabilities
    {
        bool observeNewAllocations{false};
        bool correctEnlargedDownsample{false};
        bool observeStageZeroIdentity{false};
        bool applyOptionalAppearance{false};
        bool publishNewEnlargement{false};
        bool drawForwardingRecent{false};
    };

    constexpr RuntimeCapabilities evaluate_runtime_capabilities(
        const RuntimeCapabilityInput& input)
    {
        const bool forwardingRecent = input.drawForwardingObserved
            && !input.drawForwardingLost
            && input.currentFrame >= input.lastDrawForwardingFrame
            && input.currentFrame - input.lastDrawForwardingFrame
                <= kDrawForwardingFreshFrames;
        const bool correction = input.drawSlotOwned || forwardingRecent;
        const bool stageZero = input.setTextureSlotOwned || input.stageZeroQueryFallback;
        return {
            input.createCallbackActive,
            correction,
            stageZero,
            correction && stageZero,
            input.createCallbackActive && correction && input.baseEnlargementAllowed,
            forwardingRecent,
        };
    }

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
