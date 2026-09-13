// SPDX-License-Identifier: GPL-3.0-only
// Bounded COM-style interface alias cache. The primary canonical identity and
// every cached raw interface pointer own one retained reference until clear().

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace spectralfix
{
    enum class DeviceMethod : uint32_t
    {
        createTexture = 1U << 0,
        setTexture = 1U << 1,
        drawPrimitiveUP = 1U << 2,
    };

    enum class AliasIdentityResult
    {
        exactPointer,
        canonicalIdentity,
        mismatch,
        unavailable,
        cacheFull,
    };

    template<typename Interface, typename Canonical, size_t Capacity>
    class DeviceAliasCache
    {
    public:
        using AcquireCanonical = Canonical* (*)(Interface*);

        DeviceAliasCache() = default;
        DeviceAliasCache(const DeviceAliasCache&) = delete;
        DeviceAliasCache& operator=(const DeviceAliasCache&) = delete;

        ~DeviceAliasCache()
        {
            clear();
        }

        bool initialize(Interface* primary, const AcquireCanonical acquire)
        {
            clear();
            primary_ = primary;
            acquire_ = acquire;
            if (primary_ == nullptr || acquire_ == nullptr)
                return false;
            primaryIdentity_ = acquire_(primary_);
            return primaryIdentity_ != nullptr;
        }

        AliasIdentityResult classify(Interface* candidate, const DeviceMethod method)
        {
            if (candidate == nullptr || primary_ == nullptr)
                return AliasIdentityResult::unavailable;
            if (candidate == primary_)
                return AliasIdentityResult::exactPointer;

            const auto methodBit = static_cast<uint32_t>(method);
            for (size_t i = 0; i < count_; ++i)
            {
                if (entries_[i].pointer == candidate)
                {
                    entries_[i].methodMask |= methodBit;
                    return entries_[i].result;
                }
            }

            if (count_ >= Capacity)
                return AliasIdentityResult::cacheFull;

            auto result = AliasIdentityResult::unavailable;
            Canonical* identity = acquire_ != nullptr ? acquire_(candidate) : nullptr;
            if (identity != nullptr && primaryIdentity_ != nullptr)
            {
                result = identity == primaryIdentity_
                    ? AliasIdentityResult::canonicalIdentity
                    : AliasIdentityResult::mismatch;
            }
            if (identity != nullptr)
                identity->Release();

            candidate->AddRef();
            entries_[count_++] = Entry{candidate, result, methodBit};
            return result;
        }

        bool is_cached_for(Interface* candidate, const DeviceMethod method) const
        {
            const auto methodBit = static_cast<uint32_t>(method);
            for (size_t i = 0; i < count_; ++i)
            {
                if (entries_[i].pointer == candidate
                    && (entries_[i].methodMask & methodBit) != 0)
                    return true;
            }
            return false;
        }

        size_t size() const { return count_; }
        constexpr size_t capacity() const { return Capacity; }

        void clear()
        {
            for (size_t i = 0; i < count_; ++i)
            {
                if (entries_[i].pointer != nullptr)
                    entries_[i].pointer->Release();
                entries_[i] = Entry{};
            }
            count_ = 0;
            if (primaryIdentity_ != nullptr)
                primaryIdentity_->Release();
            primaryIdentity_ = nullptr;
            primary_ = nullptr;
            acquire_ = nullptr;
        }

    private:
        struct Entry
        {
            Interface* pointer{nullptr};
            AliasIdentityResult result{AliasIdentityResult::unavailable};
            uint32_t methodMask{0};
        };

        Interface* primary_{nullptr};
        Canonical* primaryIdentity_{nullptr};
        AcquireCanonical acquire_{nullptr};
        std::array<Entry, Capacity> entries_{};
        size_t count_{0};
    };
}
