#pragma once

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
        (void)callerRva;
        return version == 1
            && moduleTimestamp != 0
            && moduleSize != 0
            && stackHash != 0
            && signatureOrdinal != 0
            && (targetSize == 1024 || targetSize == 2048 || targetSize == 4096);
    }
}
