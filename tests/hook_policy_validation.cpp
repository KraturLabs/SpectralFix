#include "hook_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

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

    RuntimeCapabilityInput capability{};
    capability.createCallbackActive = true;
    capability.drawSlotOwned = true;
    capability.setTextureSlotOwned = true;
    capability.baseEnlargementAllowed = true;
    auto runtime = evaluate_runtime_capabilities(capability);
    if (!runtime.publishNewEnlargement || !runtime.correctEnlargedDownsample
        || !runtime.observeStageZeroIdentity || !runtime.applyOptionalAppearance)
    {
        std::cerr << "directly owned hooks did not expose full capabilities\n";
        return 13;
    }

    // SetTexture loss only switches stage-zero identity to the narrow query path;
    // allocation observation and required draw correction remain independent.
    capability.setTextureSlotOwned = false;
    capability.stageZeroQueryFallback = true;
    runtime = evaluate_runtime_capabilities(capability);
    if (!runtime.publishNewEnlargement || !runtime.observeStageZeroIdentity
        || !runtime.applyOptionalAppearance)
    {
        std::cerr << "SetTexture query fallback disabled unrelated capabilities\n";
        return 14;
    }

    capability.drawSlotOwned = false;
    capability.drawForwardingObserved = false;
    runtime = evaluate_runtime_capabilities(capability);
    if (runtime.publishNewEnlargement || runtime.correctEnlargedDownsample)
    {
        std::cerr << "unknown DrawPrimitiveUP forwarding permitted enlargement\n";
        return 15;
    }

    capability.currentFrame = 100;
    capability.lastDrawForwardingFrame = 99;
    capability.drawForwardingObserved = true;
    runtime = evaluate_runtime_capabilities(capability);
    if (!runtime.drawForwardingRecent || !runtime.publishNewEnlargement)
    {
        std::cerr << "recent DrawPrimitiveUP forwarding did not recover enlargement\n";
        return 16;
    }

    capability.drawForwardingLost = true;
    runtime = evaluate_runtime_capabilities(capability);
    if (runtime.publishNewEnlargement || runtime.correctEnlargedDownsample)
    {
        std::cerr << "lost DrawPrimitiveUP forwarding still permitted enlargement\n";
        return 17;
    }

    capability.drawForwardingLost = false;
    capability.currentFrame = 101;
    capability.lastDrawForwardingFrame = 101;
    runtime = evaluate_runtime_capabilities(capability);
    if (!runtime.publishNewEnlargement || !runtime.correctEnlargedDownsample)
    {
        std::cerr << "renewed DrawPrimitiveUP evidence did not recover capabilities\n";
        return 18;
    }

    capability.currentFrame = kDrawForwardingFreshFrames + 200;
    capability.lastDrawForwardingFrame = 1;
    runtime = evaluate_runtime_capabilities(capability);
    if (runtime.drawForwardingRecent || runtime.publishNewEnlargement)
    {
        std::cerr << "stale DrawPrimitiveUP evidence still permitted enlargement\n";
        return 19;
    }

    capability.drawSlotOwned = true;
    capability.createCallbackActive = false;
    runtime = evaluate_runtime_capabilities(capability);
    if (runtime.observeNewAllocations || runtime.publishNewEnlargement
        || !runtime.correctEnlargedDownsample)
    {
        std::cerr << "CreateTexture loss incorrectly disabled live correction\n";
        return 20;
    }


    DrawForwardingEvidence evidence{};
    evidence.correctionLost = true;
    if (record_draw_callback(evidence, DeviceIdentityResult::mismatch, false, 100)
        || evidence.trustedRuntimeDraws != 0
        || evidence.forwardingObserved
        || !evidence.correctionLost
        || evidence.recoveryPending)
    {
        std::cerr << "wrong-device traffic changed trusted forwarding evidence\n";
        return 21;
    }
    capability.drawSlotOwned = false;
    capability.drawForwardingObserved = evidence.forwardingObserved;
    capability.drawForwardingLost = evidence.correctionLost;
    capability.createCallbackActive = true;
    capability.baseEnlargementAllowed = true;
    runtime = evaluate_runtime_capabilities(capability);
    if (runtime.publishNewEnlargement || runtime.correctEnlargedDownsample)
    {
        std::cerr << "wrong-device traffic unlocked enlargement capability\n";
        return 22;
    }
    if (!record_draw_callback(
            evidence, DeviceIdentityResult::canonicalComIdentity, false, 101)
        || evidence.trustedRuntimeDraws != 1
        || !evidence.forwardingObserved
        || evidence.correctionLost
        || !evidence.recoveryPending
        || evidence.lastForwardingFrame != 101)
    {
        std::cerr << "trusted-device traffic did not recover forwarding evidence\n";
        return 23;
    }
    begin_draw_owner_epoch(evidence, true);
    if (evidence.forwardingObserved || !evidence.correctionLost
        || evidence.trustedAtLastSample != evidence.trustedRuntimeDraws
        || evidence.consecutiveMisses != 0)
    {
        std::cerr << "draw-owner epoch did not reset trusted sampling state\n";
        return 24;
    }
    if (evaluate_draw_chain_sample(false, evidence.trustedRuntimeDraws,
            evidence.trustedAtLastSample, 2, 3).health != DrawChainHealth::lost)
    {
        std::cerr << "wrong-device callbacks kept the trusted watchdog alive\n";
        return 25;
    }

    if (!stage_zero_query_activation_succeeded(StageZeroQueryActivationResult::activated)
        || !stage_zero_query_activation_succeeded(StageZeroQueryActivationResult::alreadyActive)
        || stage_zero_query_activation_succeeded(StageZeroQueryActivationResult::ownerUnavailable)
        || std::string(stage_zero_query_activation_name(
            StageZeroQueryActivationResult::ownerStillOurs)) != "owner-still-spectralfix")
    {
        std::cerr << "stage-zero query activation diagnostics are incorrect\n";
        return 26;
    }

    // If neither SetTexture observation nor the stage-zero query fallback is
    // available, marked-target activity and required downsample correction must
    // still reach the core draw path. Optional tap/composite work remains blocked.
    capability = RuntimeCapabilityInput{};
    capability.createCallbackActive = true;
    capability.drawSlotOwned = true;
    capability.baseEnlargementAllowed = true;
    runtime = evaluate_runtime_capabilities(capability);
    const auto drawProcessing = evaluate_draw_processing(false, true, runtime);
    const auto disabledWithoutEnlargement = evaluate_draw_processing(false, false, runtime);
    const auto disabledWithEnlargement = evaluate_draw_processing(true, false, runtime);
    const auto strongerIdentity = evaluate_selector_match(
        1, 2, 3, 0, 1, 1, 2, 3, 4, 1);
    if (!runtime.correctEnlargedDownsample
        || runtime.observeStageZeroIdentity
        || runtime.applyOptionalAppearance
        || !drawProcessing.processMarkedTargetCore
        || drawProcessing.applyOptionalAppearance
        || disabledWithoutEnlargement.processMarkedTargetCore
        || disabledWithoutEnlargement.applyOptionalAppearance
        || !disabledWithEnlargement.processMarkedTargetCore
        || disabledWithEnlargement.applyOptionalAppearance
        || !selector_match_requires_learning(strongerIdentity))
    {
        std::cerr << "missing stage-zero identity blocked core draw processing or allowed optional appearance\n";
        return 27;
    }

    return 0;
}
