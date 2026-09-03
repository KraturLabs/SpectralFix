#include "selector_policy.hpp"
#include "selector_validation.hpp"

#include <iostream>
#include <initializer_list>
#include <iterator>
#include <string>

int main()
{
    using namespace spectralfix;

    if (!spectralfix::resource_marker_dimensions_valid(256, 2048, 256)
        || !spectralfix::resource_marker_dimensions_valid(256, 256, 256)
        || spectralfix::resource_marker_dimensions_valid(128, 2048, 256)
        || spectralfix::resource_marker_dimensions_valid(256, 128, 256)
        || spectralfix::resource_marker_dimensions_valid(256, 2048, 0))
    {
        std::cerr << "invalid resource marker dimensions were accepted\n";
        return 1;
    }

    if (evaluate_candidate_context(DeviceIdentityResult::exactPointer, 0x1000, 0x2000)
            != CandidateContextDecision::trustedStrong
        || evaluate_candidate_context(DeviceIdentityResult::exactPointer, 0x1000, 0)
            != CandidateContextDecision::trustedFallback
        || evaluate_candidate_context(DeviceIdentityResult::exactPointer, 0, 0x2000)
            != CandidateContextDecision::trustedFallback
        || evaluate_candidate_context(DeviceIdentityResult::exactPointer, 0, 0)
            != CandidateContextDecision::rejectedNoFFXiMainIdentity)
    {
        std::cerr << "allocation identity evidence was classified incorrectly\n";
        return 2;
    }

    if (evaluate_candidate_context(DeviceIdentityResult::canonicalComIdentity, 0x1000, 0)
            != CandidateContextDecision::trustedFallback
        || evaluate_candidate_context(DeviceIdentityResult::mismatch, 0x1000, 0x2000)
            != CandidateContextDecision::rejectedWrongDevice
        || evaluate_candidate_context(DeviceIdentityResult::unavailable, 0x1000, 0x2000)
            != CandidateContextDecision::rejectedDeviceIdentityUnavailable)
    {
        std::cerr << "logical device identity was classified incorrectly\n";
        return 3;
    }

    if (!candidate_context_is_trusted(CandidateContextDecision::trustedStrong)
        || !candidate_context_is_trusted(CandidateContextDecision::trustedFallback)
        || candidate_context_is_trusted(CandidateContextDecision::rejectedNoFFXiMainIdentity)
        || std::string(candidate_context_name(CandidateContextDecision::rejectedWrongDevice))
            == std::string(candidate_context_name(CandidateContextDecision::rejectedNoFFXiMainIdentity)))
    {
        std::cerr << "candidate trust or diagnostic reason mapping is incorrect\n";
        return 4;
    }

    if (!spectralfix::should_arm_ordinal_one_default(false, 1, 1))
    {
        std::cerr << "fresh candidate 1 did not arm the startup default\n";
        return 5;
    }
    if (spectralfix::should_arm_ordinal_one_default(true, 1, 1)
        || spectralfix::should_arm_ordinal_one_default(false, 2, 1)
        || spectralfix::should_arm_ordinal_one_default(false, 1, 2))
    {
        std::cerr << "startup default armed outside the first ordinal-1 candidate\n";
        return 6;
    }

    const auto exactResult = evaluate_selector_match(
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 1,
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 1);
    if (exactResult != SelectorMatchResult::stackFallback
        || !selector_match_is_compatible(exactResult)
        || evaluate_selector_activity(true, true) != SelectorActivityDecision::confirmed)
    {
        std::cerr << "matching activity did not confirm the selected allocation\n";
        return 7;
    }

    if (evaluate_selector_match(
            1, 2, 3, 4, 5, 1, 2, 3, 4, 5) != SelectorMatchResult::exact
        || evaluate_selector_match(
            1, 2, 3, 0, 5, 1, 2, 3, 9, 5) != SelectorMatchResult::strongerEvidenceRequiresLearning
        || evaluate_selector_match(
            1, 2, 0, 4, 5, 1, 2, 9, 4, 5) != SelectorMatchResult::strongerEvidenceRequiresLearning)
    {
        std::cerr << "selector exact and compatibility matching is incorrect\n";
        return 8;
    }

    if (evaluate_selector_match(
            1, 2, 3, 4, 5, 9, 2, 3, 4, 5) != SelectorMatchResult::moduleMismatch
        || evaluate_selector_match(
            1, 2, 3, 4, 5, 1, 9, 3, 4, 5) != SelectorMatchResult::moduleMismatch
        || evaluate_selector_match(
            1, 2, 3, 4, 5, 1, 2, 3, 4, 6) != SelectorMatchResult::ordinalMismatch
        || evaluate_selector_match(
            1, 2, 3, 4, 5, 1, 2, 9, 4, 5) != SelectorMatchResult::conflictingIdentity
        || evaluate_selector_match(
            1, 2, 3, 4, 5, 1, 2, 3, 9, 5) != SelectorMatchResult::conflictingIdentity
        || evaluate_selector_match(
            1, 2, 3, 0, 5, 1, 2, 0, 4, 5) != SelectorMatchResult::insufficientEvidence)
    {
        std::cerr << "selector conflicts and rejection reasons are incorrect\n";
        return 9;
    }


    if (selector_match_is_compatible(SelectorMatchResult::strongerEvidenceRequiresLearning)
        || !selector_match_requires_learning(SelectorMatchResult::strongerEvidenceRequiresLearning)
        || evaluate_session_candidate(SelectorMatchResult::callerFallback, 0, 10)
            != SessionCandidateDecision::select
        || evaluate_session_candidate(SelectorMatchResult::callerFallback, 10, 10)
            != SessionCandidateDecision::selected
        || evaluate_session_candidate(SelectorMatchResult::callerFallback, 10, 11)
            != SessionCandidateDecision::rejectDifferentCandidate
        || evaluate_session_candidate(SelectorMatchResult::strongerEvidenceRequiresLearning, 0, 11)
            != SessionCandidateDecision::learnStrongerIdentity
        || evaluate_selector_activity_for_candidate(true, true, 10, 11)
            != SelectorActivityDecision::mismatch
        || evaluate_selector_activity_for_candidate(true, true, 10, 10)
            != SelectorActivityDecision::confirmed)
    {
        std::cerr << "weak-selector ambiguity or candidate binding was accepted\n";
        return 16;
    }

    const auto callerOnlyToStackX = evaluate_selector_match(
        1, 2, 0xA, 0, 1, 1, 2, 0xA, 0x10, 1);
    const auto callerOnlyToStackY = evaluate_selector_match(
        1, 2, 0xA, 0, 1, 1, 2, 0xA, 0x20, 1);
    const auto stackOnlyToCallerX = evaluate_selector_match(
        1, 2, 0, 0xB, 1, 1, 2, 0x10, 0xB, 1);
    const auto stackOnlyToCallerY = evaluate_selector_match(
        1, 2, 0, 0xB, 1, 1, 2, 0x20, 0xB, 1);
    if (callerOnlyToStackX != SelectorMatchResult::strongerEvidenceRequiresLearning
        || callerOnlyToStackY != SelectorMatchResult::strongerEvidenceRequiresLearning
        || stackOnlyToCallerX != SelectorMatchResult::strongerEvidenceRequiresLearning
        || stackOnlyToCallerY != SelectorMatchResult::strongerEvidenceRequiresLearning
        || selector_match_is_compatible(callerOnlyToStackX)
        || selector_match_is_compatible(callerOnlyToStackY)
        || selector_match_is_compatible(stackOnlyToCallerX)
        || selector_match_is_compatible(stackOnlyToCallerY))
    {
        std::cerr << "weak selectors trusted multiple stronger signatures\n";
        return 17;
    }

    if (!selector_fields_valid(kSettingsVersion, 1, 2, 3, 4, 1, kDefaultTargetSize)
        || !selector_fields_valid(kSettingsVersion, 1, 2, 3, 0, 1, kDefaultTargetSize)
        || !selector_fields_valid(kSettingsVersion, 1, 2, 0, 4, 1, kDefaultTargetSize)
        || selector_fields_valid(kSettingsVersion, 1, 2, 0, 0, 1, kDefaultTargetSize)
        || selector_fields_valid(kSettingsVersion, 1, 0, 3, 4, 1, kDefaultTargetSize))
    {
        std::cerr << "selector field compatibility validation is incorrect\n";
        return 10;
    }

    const bool differentOrdinal = selector_identity_matches(
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 1,
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 2);
    if (differentOrdinal || spectralfix::evaluate_selector_activity(true, differentOrdinal)
        != spectralfix::SelectorActivityDecision::mismatch)
    {
        std::cerr << "different active ordinal did not trip the mismatch policy\n";
        return 11;
    }

    if (spectralfix::evaluate_selector_activity(false, false)
        != spectralfix::SelectorActivityDecision::pending)
    {
        std::cerr << "completed verification was evaluated again\n";
        return 12;
    }
    if (!spectralfix::selected_aura_marker_is_trackable(true, true, 2048, 256)
        || !spectralfix::selected_aura_marker_is_trackable(true, true, 1024, 256)
        || !spectralfix::selected_aura_marker_is_trackable(true, true, 4096, 256)
        || spectralfix::selected_aura_marker_is_trackable(true, false, 256, 256)
        || spectralfix::selected_aura_marker_is_trackable(false, true, 256, 256))
    {
        std::cerr << "selected medium/high/ultra marker tracking policy is incorrect\n";
        return 13;
    }

    const CandidateContextDecision decisions[]{
        CandidateContextDecision::trustedStrong,
        CandidateContextDecision::trustedFallback,
        CandidateContextDecision::rejectedWrongDevice,
        CandidateContextDecision::rejectedDeviceIdentityUnavailable,
        CandidateContextDecision::rejectedNoFFXiMainIdentity,
    };
    for (size_t i = 0; i < std::size(decisions); ++i)
    {
        for (size_t j = i + 1; j < std::size(decisions); ++j)
        {
            if (std::string(candidate_context_name(decisions[i]))
                == candidate_context_name(decisions[j]))
            {
                std::cerr << "candidate diagnostic reasons are not distinct\n";
                return 14;
            }
        }
    }

    // Environment detection is diagnostic-only; the policy has no Windows/Wine
    // input and must return the same safety decision for either reported host.
    for (const bool wineDetected : {false, true})
    {
        (void)wineDetected;
        if (evaluate_candidate_context(DeviceIdentityResult::exactPointer, 0, 0)
            != CandidateContextDecision::rejectedNoFFXiMainIdentity)
        {
            std::cerr << "environment diagnostics changed candidate safety policy\n";
            return 15;
        }
    }

    return 0;
}
