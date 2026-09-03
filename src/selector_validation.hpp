#pragma once

#include "settings.hpp"

#include <cstdint>

namespace spectralfix
{
    constexpr bool selector_fields_valid(
        const uint32_t version,
        const uint32_t moduleTimestamp,
        const uint32_t moduleSize,
        const uint32_t callerRva,
        const uint32_t stackHash,
        const uint32_t signatureOrdinal,
        const uint32_t targetSize)
    {
        return version == kSettingsVersion
            && moduleTimestamp != 0
            && moduleSize != 0
            && (callerRva != 0 || stackHash != 0)
            && signatureOrdinal != 0
            && target_size_supported(targetSize);
    }
}
