// SPDX-License-Identifier: GPL-3.0-only
// Transactional publication and rollback for a small vtable hook set.

#pragma once

#include <cstddef>

namespace spectralfix
{
    enum class HookTransactionResult
    {
        installed,
        rolledBack,
        rollbackIncomplete,
    };

    // Slot must expose target, hook, previous, and tracked fields. Writer must
    // attempt one slot write and return whether the requested value is currently
    // present. The failed write is included in rollback because a writer can
    // publish the value and still lose the final verification race.
    template<typename Slot, typename Writer>
    HookTransactionResult install_hook_transaction(
        Slot* slots,
        const size_t count,
        Writer&& writer)
    {
        if (slots == nullptr)
            return HookTransactionResult::rolledBack;

        size_t attempted = 0;
        for (size_t i = 0; i < count; ++i)
        {
            attempted = i + 1;
            if (!writer(slots[i].target, slots[i].hook))
                break;
            slots[i].tracked = true;
        }

        if (attempted == count && (count == 0 || slots[count - 1].tracked))
            return HookTransactionResult::installed;

        bool rollbackComplete = true;
        for (size_t i = attempted; i > 0; --i)
        {
            auto& slot = slots[i - 1];
            if (slot.target == nullptr || slot.hook == nullptr || slot.previous == nullptr)
            {
                rollbackComplete = false;
                continue;
            }

            if (*slot.target == slot.hook)
            {
                slot.tracked = true;
                (void)writer(slot.target, slot.previous);
            }

            if (*slot.target == slot.previous)
            {
                slot.tracked = false;
            }
            else
            {
                // If another owner won the slot, SpectralFix is no longer at the
                // top and must not claim ownership. The transaction is still not
                // restored to its original state, so restart remains required.
                if (*slot.target != slot.hook)
                    slot.tracked = false;
                rollbackComplete = false;
            }
        }

        return rollbackComplete
            ? HookTransactionResult::rolledBack
            : HookTransactionResult::rollbackIncomplete;
    }
}
