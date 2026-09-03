#include "failure_policy.hpp"
#include "hook_policy.hpp"
#include "hook_transaction.hpp"
#include "selector_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace
{
    struct FakeSlot
    {
        void** target{nullptr};
        void* hook{nullptr};
        void* previous{nullptr};
        bool tracked{false};
    };

    struct FakeDevice
    {
        void** vtable{nullptr};
    };

    struct FakeWriter
    {
        size_t calls{0};
        size_t failCall{static_cast<size_t>(-1)};
        bool publishOnFailure{false};

        bool operator()(void** target, void* value)
        {
            const auto call = calls++;
            if (call == failCall)
            {
                if (publishOnFailure)
                    *target = value;
                return false;
            }
            *target = value;
            return true;
        }
    };

    constexpr uintptr_t kBaseAddress = 0x1000;
    constexpr uintptr_t kHookAddress = 0x2000;
    constexpr uintptr_t kForeignAddress = 0x3000;

    void* address(const uintptr_t value)
    {
        return reinterpret_cast<void*>(value);
    }

    std::array<FakeSlot, 3> make_slots(std::array<void*, 3>& table)
    {
        std::array<FakeSlot, 3> slots{};
        for (size_t i = 0; i < slots.size(); ++i)
        {
            slots[i].target   = &table[i];
            slots[i].hook     = address(kHookAddress + (i * 0x10));
            slots[i].previous = table[i];
        }
        return slots;
    }
}

int main()
{
    using namespace spectralfix;

    // Repeated callback failures queue exactly one user-visible notice.
    OneShotNotice notice;
    notice.record();
    notice.record();
    if (!notice.consume() || notice.consume() || !notice.queued_or_reported())
    {
        std::cerr << "one-shot failure reporting re-armed or lost its first notice\n";
        return 1;
    }
    notice.record();
    if (notice.consume())
    {
        std::cerr << "a reported failure queued another notice\n";
        return 2;
    }

    // A complete fake-vtable publication owns all three slots.
    std::array<void*, 3> table{
        address(kBaseAddress), address(kBaseAddress + 0x10), address(kBaseAddress + 0x20)};
    auto slots = make_slots(table);
    FakeWriter writer{};
    if (install_hook_transaction(slots.data(), slots.size(), writer)
            != HookTransactionResult::installed
        || table[0] != slots[0].hook || table[1] != slots[1].hook
        || table[2] != slots[2].hook)
    {
        std::cerr << "the fake vtable did not publish transactionally\n";
        return 3;
    }

    // Failure on the second publication restores the first slot exactly.
    table = {address(kBaseAddress), address(kBaseAddress + 0x10), address(kBaseAddress + 0x20)};
    slots = make_slots(table);
    writer = FakeWriter{0, 1, false};
    if (install_hook_transaction(slots.data(), slots.size(), writer)
            != HookTransactionResult::rolledBack
        || table[0] != slots[0].previous || table[1] != slots[1].previous
        || slots[0].tracked || slots[1].tracked)
    {
        std::cerr << "a partial fake-vtable install was not rolled back\n";
        return 4;
    }

    // A failed write can still have published the hook. It must be included in
    // rollback instead of being forgotten.
    table = {address(kBaseAddress), address(kBaseAddress + 0x10), address(kBaseAddress + 0x20)};
    slots = make_slots(table);
    writer = FakeWriter{0, 1, true};
    if (install_hook_transaction(slots.data(), slots.size(), writer)
            != HookTransactionResult::rolledBack
        || table[1] != slots[1].previous)
    {
        std::cerr << "a published-on-failure slot escaped rollback\n";
        return 5;
    }

    // Install slot zero, fail slot one, then fail the rollback write. Ownership
    // of slot zero must remain explicit so production can stay resident.
    table = {address(kBaseAddress), address(kBaseAddress + 0x10), address(kBaseAddress + 0x20)};
    slots = make_slots(table);
    writer = FakeWriter{0, 1, false};
    size_t calls = 0;
    const auto incomplete = install_hook_transaction(
        slots.data(), slots.size(),
        [&calls](void** target, void* value)
        {
            const auto call = calls++;
            if (call == 1 || call == 2)
                return false;
            *target = value;
            return true;
        });
    if (incomplete != HookTransactionResult::rollbackIncomplete
        || table[0] != slots[0].hook || !slots[0].tracked)
    {
        std::cerr << "an incomplete rollback forgot a live SpectralFix hook\n";
        return 6;
    }

    // If a foreign owner wins the slot during rollback, the transaction is still
    // incomplete but SpectralFix must not claim that it owns the top-level slot.
    table = {address(kBaseAddress), address(kBaseAddress + 0x10), address(kBaseAddress + 0x20)};
    slots = make_slots(table);
    calls = 0;
    const auto foreignRollback = install_hook_transaction(
        slots.data(), slots.size(),
        [&calls](void** target, void* value)
        {
            const auto call = calls++;
            if (call == 1)
                return false;
            if (call == 2)
            {
                *target = address(kForeignAddress);
                return false;
            }
            *target = value;
            return true;
        });
    if (foreignRollback != HookTransactionResult::rollbackIncomplete
        || table[0] != address(kForeignAddress) || slots[0].tracked)
    {
        std::cerr << "rollback did not preserve accurate foreign slot ownership\n";
        return 7;
    }

    // A conventional late owner saves SpectralFix and forwards through it. The
    // slot is classified as displaced and retained in place; putting SpectralFix
    // back above that owner would create the recursive cycle fixed in v1.02.
    const void* savedByForeign = slots[0].hook;
    table[0] = address(kForeignAddress);
    const HookSlotView foreignView{slots[0].hook, table[0], true};
    if (!slot_is_displaced(foreignView) || savedByForeign != slots[0].hook)
    {
        std::cerr << "a conventional forwarding owner was not preserved\n";
        return 8;
    }

    // Two devices can share a vtable. Shared code is not canonical COM identity.
    std::array<void*, 3> sharedTable{
        address(kBaseAddress), address(kBaseAddress + 0x10), address(kBaseAddress + 0x20)};
    FakeDevice ashitaDevice{sharedTable.data()};
    FakeDevice secondaryDevice{sharedTable.data()};
    if (ashitaDevice.vtable != secondaryDevice.vtable
        || !candidate_context_is_trusted(evaluate_candidate_context(
            DeviceIdentityResult::exactPointer, 0x10, 0x1234))
        || candidate_context_is_trusted(evaluate_candidate_context(
            DeviceIdentityResult::mismatch, 0x10, 0x1234)))
    {
        std::cerr << "shared-vtable secondary device entered candidate selection\n";
        return 9;
    }

    // Release policy retains an enlarged allocation, but ordinary pre-enlargement
    // release is allowed to restore its hook table.
    if (!must_retain_hooks_on_release(true, true)
        || must_retain_hooks_on_release(true, false)
        || must_retain_hooks_on_release(false, true))
    {
        std::cerr << "release-before/after-enlargement policy is incorrect\n";
        return 10;
    }

    return 0;
}
