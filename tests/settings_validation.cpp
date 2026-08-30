#include "settings.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    bool near(const float left, const float right)
    {
        return std::fabs(left - right) < 0.001F;
    }
}

int main()
{
    using namespace spectralfix;

    // A file written by SpectralFix must read back as the same values.
    SettingsFile written{};
    written.selector.version          = kSettingsVersion;
    written.selector.moduleTimestamp  = 0x69144FB9U;
    written.selector.moduleSize       = 0x00BDF000U;
    written.selector.callerRva        = 0U;
    written.selector.stackHash        = 0x1E72400DU;
    written.selector.signatureOrdinal = 1U;
    written.selector.targetSize       = kDefaultTargetSize;
    written.visual.spreadOverride            = 4.25F;
    written.visual.opacityPercent            = 60.0F;
    written.visual.compositeOpacityPercent   = 25.0F;
    written.visual.compositeOpacityOverride  = true;

    SettingsFile roundTripped{};
    std::vector<std::string> warnings;
    parse_settings_text(serialize_settings_text(written), roundTripped, warnings);
    if (!warnings.empty())
    {
        std::cerr << "round trip produced warnings: " << warnings.front() << '\n';
        return 1;
    }
    if (roundTripped.selector.version != written.selector.version
        || roundTripped.selector.moduleTimestamp != written.selector.moduleTimestamp
        || roundTripped.selector.moduleSize != written.selector.moduleSize
        || roundTripped.selector.callerRva != written.selector.callerRva
        || roundTripped.selector.stackHash != written.selector.stackHash
        || roundTripped.selector.signatureOrdinal != written.selector.signatureOrdinal
        || roundTripped.selector.targetSize != written.selector.targetSize)
    {
        std::cerr << "selector fields did not survive a round trip\n";
        return 2;
    }
    if (!near(roundTripped.visual.spreadOverride, 4.25F)
        || !near(roundTripped.visual.opacityPercent, 60.0F)
        || !near(roundTripped.visual.compositeOpacityPercent, 25.0F)
        || !roundTripped.visual.compositeOpacityOverride)
    {
        std::cerr << "visual settings did not survive a round trip\n";
        return 3;
    }

    // "stock" composite opacity must round trip as stock, not as a percentage.
    written.visual.compositeOpacityOverride = false;
    written.visual.compositeOpacityPercent  = kDefaultOpacityPercent;
    SettingsFile stock{};
    warnings.clear();
    parse_settings_text(serialize_settings_text(written), stock, warnings);
    if (stock.visual.compositeOpacityOverride || !warnings.empty())
    {
        std::cerr << "stock composite opacity did not round trip\n";
        return 4;
    }

    // Existing v1.01 files, including CRLF line endings and inline comments.
    SettingsFile legacy{};
    warnings.clear();
    parse_settings_text(
        "# SpectralFix selector and visual settings.\r\n"
        "version=1\r\n"
        "module_timestamp=0x69144FB9\r\n"
        "module_size=0xBDF000\r\n"
        "caller_rva=0x0\r\n"
        "stack_hash=0x1E72400D\r\n"
        "signature_ordinal=1\r\n"
        "target_size=4096\r\n"
        "spread_override=2.00 # 0 = automatic\r\n"
        "opacity_percent=100.00 # 100 = stock tap opacity\r\n"
        "composite_opacity_percent=25.00 # stock = original hard-cutout center pass\r\n",
        legacy, warnings);
    if (!warnings.empty())
    {
        std::cerr << "a valid v1.01 file produced warnings: " << warnings.front() << '\n';
        return 5;
    }
    if (legacy.selector.moduleTimestamp != 0x69144FB9U || legacy.selector.moduleSize != 0xBDF000U
        || legacy.selector.stackHash != 0x1E72400DU || legacy.selector.signatureOrdinal != 1U
        || legacy.selector.targetSize != kUltraTargetSize
        || !near(legacy.visual.spreadOverride, 2.0F)
        || !near(legacy.visual.opacityPercent, 100.0F)
        || !near(legacy.visual.compositeOpacityPercent, 25.0F))
    {
        std::cerr << "a valid v1.01 file did not parse correctly\n";
        return 6;
    }

    // Out-of-range and unparsable values are rejected, and leave defaults in place.
    SettingsFile guarded{};
    warnings.clear();
    parse_settings_text(
        "spread_override=99\n"
        "opacity_percent=-5\n"
        "composite_opacity_percent=banana\n"
        "target_size=-1\n"
        "signature_ordinal=12abc\n",
        guarded, warnings);
    if (warnings.size() != 5)
    {
        std::cerr << "expected five rejections, saw " << warnings.size() << '\n';
        return 7;
    }
    if (!near(guarded.visual.spreadOverride, kDefaultSpread)
        || !near(guarded.visual.opacityPercent, kDefaultOpacityPercent)
        || !near(guarded.visual.compositeOpacityPercent, kDefaultCompositeOpacityPercent)
        || !guarded.visual.compositeOpacityOverride
        || guarded.selector.targetSize != kDefaultTargetSize
        || guarded.selector.signatureOrdinal != 0U)
    {
        std::cerr << "a rejected value overwrote an existing setting\n";
        return 8;
    }

    // Unknown keys are ignored silently rather than reported as malformed.
    SettingsFile unknown{};
    warnings.clear();
    parse_settings_text("future_option=whatever\n\n   \nnot a pair\n", unknown, warnings);
    if (!warnings.empty())
    {
        std::cerr << "an unknown key was reported as malformed\n";
        return 9;
    }

    // spread_override accepts the automatic sentinel but not values between it and
    // the manual minimum.
    SettingsFile spread{};
    warnings.clear();
    parse_settings_text("spread_override=0\n", spread, warnings);
    if (!warnings.empty() || !near(spread.visual.spreadOverride, 0.0F))
    {
        std::cerr << "automatic spread sentinel was rejected\n";
        return 10;
    }
    warnings.clear();
    parse_settings_text("spread_override=0.5\n", spread, warnings);
    if (warnings.size() != 1 || !near(spread.visual.spreadOverride, 0.0F))
    {
        std::cerr << "an out-of-range spread was accepted\n";
        return 11;
    }

    // Negative numbers must not wrap into large positive values.
    uint32_t parsed = 123U;
    if (parse_u32("-1", parsed) || parsed != 123U)
    {
        std::cerr << "a negative unsigned value was accepted\n";
        return 12;
    }
    if (!parse_u32("0x10", parsed) || parsed != 16U)
    {
        std::cerr << "hexadecimal parsing is broken\n";
        return 13;
    }
    if (parse_u32("16 ", parsed) || parse_u32("", parsed))
    {
        std::cerr << "trailing or empty input was accepted\n";
        return 14;
    }

    // Appearance settings are valid before an allocation selector has been
    // learned. They must survive a settings-only file without pretending that a
    // zeroed selector is usable.
    SettingsFile settingsOnly{};
    settingsOnly.selector.version    = kSettingsVersion;
    settingsOnly.selector.targetSize = kMediumTargetSize;
    settingsOnly.visual.spreadOverride = 3.5F;
    SettingsFile settingsOnlyRead{};
    warnings.clear();
    parse_settings_text(serialize_settings_text(settingsOnly), settingsOnlyRead, warnings);
    if (!warnings.empty() || selector_identity_present(settingsOnlyRead.selector)
        || settingsOnlyRead.selector.targetSize != kMediumTargetSize
        || !near(settingsOnlyRead.visual.spreadOverride, 3.5F))
    {
        std::cerr << "settings-only config did not round trip cleanly\n";
        return 15;
    }

    warnings.clear();
    parse_settings_text("target_size=999\n", settingsOnlyRead, warnings);
    if (warnings.size() != 1 || settingsOnlyRead.selector.targetSize != kMediumTargetSize)
    {
        std::cerr << "unsupported target size was accepted\n";
        return 16;
    }

    return 0;
}
