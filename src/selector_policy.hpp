#pragma once

#include <cstdint>

namespace spectralfix
{
    enum class DeviceIdentityResult
    {
        exactPointer,
        canonicalComIdentity,
        mismatch,
        unavailable,
    };

    enum class AllocationIdentityEvidence
    {
        callerAndStack,
        callerOnly,
        stackOnly,
        none,
    };

    enum class CandidateContextDecision
    {
        trustedStrong,
        trustedFallback,
        rejectedWrongDevice,
        rejectedDeviceIdentityUnavailable,
        rejectedNoFFXiMainIdentity,
    };

    constexpr AllocationIdentityEvidence allocation_identity_evidence(
        const uint32_t callerRva,
        const uint32_t stackHash)
    {
        if (callerRva != 0 && stackHash != 0)
            return AllocationIdentityEvidence::callerAndStack;
        if (callerRva != 0)
            return AllocationIdentityEvidence::callerOnly;
        if (stackHash != 0)
            return AllocationIdentityEvidence::stackOnly;
        return AllocationIdentityEvidence::none;
    }

    constexpr CandidateContextDecision evaluate_candidate_context(
        const DeviceIdentityResult deviceIdentity,
        const uint32_t callerRva,
        const uint32_t stackHash)
    {
        if (deviceIdentity == DeviceIdentityResult::mismatch)
            return CandidateContextDecision::rejectedWrongDevice;
        if (deviceIdentity == DeviceIdentityResult::unavailable)
            return CandidateContextDecision::rejectedDeviceIdentityUnavailable;

        const auto evidence = allocation_identity_evidence(callerRva, stackHash);
        if (evidence == AllocationIdentityEvidence::none)
            return CandidateContextDecision::rejectedNoFFXiMainIdentity;
        return deviceIdentity == DeviceIdentityResult::exactPointer
                && evidence == AllocationIdentityEvidence::callerAndStack
            ? CandidateContextDecision::trustedStrong
            : CandidateContextDecision::trustedFallback;
    }

    constexpr bool candidate_context_is_trusted(const CandidateContextDecision decision)
    {
        return decision == CandidateContextDecision::trustedStrong
            || decision == CandidateContextDecision::trustedFallback;
    }

    constexpr const char* candidate_context_name(const CandidateContextDecision decision)
    {
        switch (decision)
        {
            case CandidateContextDecision::trustedStrong: return "trusted-strong";
            case CandidateContextDecision::trustedFallback: return "trusted-fallback";
            case CandidateContextDecision::rejectedWrongDevice: return "rejected-wrong-device";
            case CandidateContextDecision::rejectedDeviceIdentityUnavailable: return "rejected-device-identity-unavailable";
            default: return "rejected-no-ffximain-identity";
        }
    }

    constexpr const char* device_identity_name(const DeviceIdentityResult result)
    {
        switch (result)
        {
            case DeviceIdentityResult::exactPointer: return "exact-pointer";
            case DeviceIdentityResult::canonicalComIdentity: return "canonical-com-identity";
            case DeviceIdentityResult::mismatch: return "mismatch";
            default: return "unavailable";
        }
    }

    enum class SelectorMatchResult
    {
        exact,
        callerFallback,
        stackFallback,
        strongerEvidenceRequiresLearning,
        pendingVerification,
        moduleMismatch,
        ordinalMismatch,
        conflictingIdentity,
        insufficientEvidence,
    };

    constexpr SelectorMatchResult evaluate_selector_match(
        const uint32_t selectedModuleTimestamp,
        const uint32_t selectedModuleSize,
        const uint32_t selectedCallerRva,
        const uint32_t selectedStackHash,
        const uint32_t selectedSignatureOrdinal,
        const uint32_t observedModuleTimestamp,
        const uint32_t observedModuleSize,
        const uint32_t observedCallerRva,
        const uint32_t observedStackHash,
        const uint32_t observedSignatureOrdinal)
    {
        if (selectedModuleTimestamp != observedModuleTimestamp
            || selectedModuleSize != observedModuleSize)
            return SelectorMatchResult::moduleMismatch;
        const bool callerComparable = selectedCallerRva != 0 && observedCallerRva != 0;
        const bool stackComparable = selectedStackHash != 0 && observedStackHash != 0;
        const bool callerConflict = callerComparable && selectedCallerRva != observedCallerRva;
        const bool stackConflict = stackComparable && selectedStackHash != observedStackHash;
        if (callerConflict || stackConflict)
            return SelectorMatchResult::conflictingIdentity;

        const bool selectedHasCaller = selectedCallerRva != 0;
        const bool selectedHasStack = selectedStackHash != 0;
        const bool observedHasCaller = observedCallerRva != 0;
        const bool observedHasStack = observedStackHash != 0;
        if ((selectedHasCaller != selectedHasStack)
            && observedHasCaller && observedHasStack)
            return SelectorMatchResult::strongerEvidenceRequiresLearning;

        if (selectedSignatureOrdinal != observedSignatureOrdinal)
            return SelectorMatchResult::ordinalMismatch;

        const bool callerMatch = callerComparable && selectedCallerRva == observedCallerRva;
        const bool stackMatch = stackComparable && selectedStackHash == observedStackHash;
        if (callerMatch && stackMatch)
            return SelectorMatchResult::exact;
        if (callerMatch)
            return SelectorMatchResult::callerFallback;
        if (stackMatch)
            return SelectorMatchResult::stackFallback;
        return SelectorMatchResult::insufficientEvidence;
    }

    constexpr bool selector_match_requires_learning(const SelectorMatchResult result)
    {
        return result == SelectorMatchResult::strongerEvidenceRequiresLearning;
    }

    constexpr bool selector_match_is_compatible(const SelectorMatchResult result)
    {
        return result == SelectorMatchResult::exact
            || result == SelectorMatchResult::callerFallback
            || result == SelectorMatchResult::stackFallback;
    }

    constexpr const char* selector_match_name(const SelectorMatchResult result)
    {
        switch (result)
        {
            case SelectorMatchResult::exact: return "exact";
            case SelectorMatchResult::callerFallback: return "caller-fallback";
            case SelectorMatchResult::stackFallback: return "stack-fallback";
            case SelectorMatchResult::strongerEvidenceRequiresLearning: return "stronger-evidence-requires-learning";
            case SelectorMatchResult::pendingVerification: return "pending-verification";
            case SelectorMatchResult::moduleMismatch: return "module-mismatch";
            case SelectorMatchResult::ordinalMismatch: return "ordinal-mismatch";
            case SelectorMatchResult::conflictingIdentity: return "conflicting-identity";
            default: return "insufficient-evidence";
        }
    }

    enum class SelectorActivityDecision
    {
        pending,
        confirmed,
        mismatch,
    };

    enum class SessionCandidateDecision
    {
        select,
        selected,
        rejectDifferentCandidate,
        learnStrongerIdentity,
        incompatible,
    };

    constexpr SessionCandidateDecision evaluate_session_candidate(
        const SelectorMatchResult selectorMatch,
        const uint32_t selectedCandidateId,
        const uint32_t observedCandidateId)
    {
        if (selector_match_requires_learning(selectorMatch))
            return SessionCandidateDecision::learnStrongerIdentity;
        if (!selector_match_is_compatible(selectorMatch) || observedCandidateId == 0)
            return SessionCandidateDecision::incompatible;
        if (selectedCandidateId == 0)
            return SessionCandidateDecision::select;
        return selectedCandidateId == observedCandidateId
            ? SessionCandidateDecision::selected
            : SessionCandidateDecision::rejectDifferentCandidate;
    }

    constexpr bool resource_marker_dimensions_valid(
        const uint32_t originalSize,
        const uint32_t actualSize,
        const uint32_t expectedOriginalSize)
    {
        return expectedOriginalSize != 0
            && originalSize == expectedOriginalSize
            && actualSize >= originalSize;
    }

    constexpr bool should_arm_ordinal_one_default(
        const bool selectorValid,
        const uint32_t candidateId,
        const uint32_t signatureOrdinal)
    {
        return !selectorValid && candidateId == 1 && signatureOrdinal == 1;
    }

    constexpr bool selector_identity_matches(
        const uint32_t selectedModuleTimestamp,
        const uint32_t selectedModuleSize,
        const uint32_t selectedCallerRva,
        const uint32_t selectedStackHash,
        const uint32_t selectedSignatureOrdinal,
        const uint32_t observedModuleTimestamp,
        const uint32_t observedModuleSize,
        const uint32_t observedCallerRva,
        const uint32_t observedStackHash,
        const uint32_t observedSignatureOrdinal)
    {
        return selector_match_is_compatible(evaluate_selector_match(
            selectedModuleTimestamp, selectedModuleSize, selectedCallerRva,
            selectedStackHash, selectedSignatureOrdinal, observedModuleTimestamp,
            observedModuleSize, observedCallerRva, observedStackHash,
            observedSignatureOrdinal));
    }

    constexpr SelectorActivityDecision evaluate_selector_activity(
        const bool verificationPending,
        const bool identityMatches)
    {
        if (!verificationPending)
            return SelectorActivityDecision::pending;
        return identityMatches
            ? SelectorActivityDecision::confirmed
            : SelectorActivityDecision::mismatch;
    }

    constexpr SelectorActivityDecision evaluate_selector_activity_for_candidate(
        const bool verificationPending,
        const bool identityMatches,
        const uint32_t selectedCandidateId,
        const uint32_t observedCandidateId)
    {
        return evaluate_selector_activity(
            verificationPending,
            identityMatches && selectedCandidateId != 0
                && selectedCandidateId == observedCandidateId);
    }

    constexpr bool selected_aura_marker_is_trackable(
        const bool selectorValid,
        const bool identityMatches,
        const uint32_t actualSize,
        const uint32_t originalSize)
    {
        return selectorValid && identityMatches
            && actualSize >= originalSize && originalSize != 0;
    }

}
