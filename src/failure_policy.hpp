// SPDX-License-Identifier: GPL-3.0-only
// Small one-shot notification latch used by hot callback failure paths.

#pragma once

namespace spectralfix
{
    class OneShotNotice
    {
    public:
        constexpr void record()
        {
            if (!queuedOrReported_)
            {
                queuedOrReported_ = true;
                pending_          = true;
            }
        }

        constexpr bool consume()
        {
            if (!pending_)
                return false;
            pending_ = false;
            return true;
        }

        constexpr bool queued_or_reported() const
        {
            return queuedOrReported_;
        }

    private:
        bool pending_{false};
        bool queuedOrReported_{false};
    };
}
