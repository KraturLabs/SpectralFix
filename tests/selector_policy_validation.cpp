#include "selector_policy.hpp"

#include <iostream>

int main()
{
    if (!spectralfix::should_arm_ordinal_one_default(false, 1, 1))
    {
        std::cerr << "fresh candidate 1 did not arm the startup default\n";
        return 1;
    }
    if (spectralfix::should_arm_ordinal_one_default(true, 1, 1)
        || spectralfix::should_arm_ordinal_one_default(false, 2, 1)
        || spectralfix::should_arm_ordinal_one_default(false, 1, 2))
    {
        std::cerr << "startup default armed outside the first ordinal-1 candidate\n";
        return 2;
    }

    const bool exact = spectralfix::selector_identity_matches(
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 1,
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 1);
    if (!exact || spectralfix::evaluate_selector_activity(true, exact)
        != spectralfix::SelectorActivityDecision::confirmed)
    {
        std::cerr << "matching activity did not confirm the selected allocation\n";
        return 3;
    }

    const bool differentOrdinal = spectralfix::selector_identity_matches(
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 1,
        0x69144FB9U, 0xBDF000U, 0, 0x1E72400DU, 2);
    if (differentOrdinal || spectralfix::evaluate_selector_activity(true, differentOrdinal)
        != spectralfix::SelectorActivityDecision::mismatch)
    {
        std::cerr << "different active ordinal did not trip the mismatch policy\n";
        return 4;
    }

    if (spectralfix::evaluate_selector_activity(false, false)
        != spectralfix::SelectorActivityDecision::pending)
    {
        std::cerr << "completed verification was evaluated again\n";
        return 5;
    }
    if (!spectralfix::selected_aura_marker_is_trackable(true, true, 2048, 256)
        || !spectralfix::selected_aura_marker_is_trackable(true, true, 1024, 256)
        || !spectralfix::selected_aura_marker_is_trackable(true, true, 4096, 256)
        || spectralfix::selected_aura_marker_is_trackable(true, false, 256, 256)
        || spectralfix::selected_aura_marker_is_trackable(false, true, 256, 256))
    {
        std::cerr << "selected medium/high/ultra marker tracking policy is incorrect\n";
        return 6;
    }
    return 0;
}
