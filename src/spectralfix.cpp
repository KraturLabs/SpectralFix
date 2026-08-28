// SPDX-License-Identifier: GPL-3.0-only
// Enlarges and corrects FFXI's client-owned actor-aura render path.

#include "Ashita.h"
#include "geometry_rewrite.hpp"
#include "selector_policy.hpp"
#include "selector_validation.hpp"
#include "version.hpp"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

namespace spectralfix
{
    constexpr uint32_t kMarkerMagic       = 0x58465053; // "SPFX" in little endian.
    constexpr uint32_t kMarkerVersion     = 1;
    constexpr uint32_t kOriginalSize      = 256;
    constexpr uint32_t kMediumTargetSize  = 1024;
    constexpr uint32_t kDefaultTargetSize = 2048;
    constexpr uint32_t kUltraTargetSize   = 4096;
    constexpr uint32_t kSpreadBase        = 1024;
    constexpr float kMinManualSpread      = 1.0F;
    constexpr float kMaxManualSpread      = 16.0F;
    constexpr float kDefaultSpread        = 2.0F;
    constexpr float kDefaultOpacityPercent = 100.0F;
    constexpr float kDefaultCompositeOpacityPercent = 25.0F;
    constexpr uint32_t kCreateTextureSlot = 20;
    constexpr uint32_t kSetTextureSlot     = 61;
    constexpr uint32_t kDrawPrimitiveUPSlot = 72;
    constexpr uint32_t kMaxHookRechains    = 3;
    constexpr size_t kMaxSignatureEntries = 64;
    constexpr size_t kMaxActivityEntries  = 256;
    constexpr uint64_t kMaxLogBytes       = 4ULL * 1024ULL * 1024ULL;
    constexpr uint32_t kMaxVertices       = 16;
    constexpr uint32_t kMaxStride         = 256;

    constexpr GUID kMarkerGuid = {
        0xa58e07d9, 0x907e, 0x45a1, {0x8a, 0x37, 0x2e, 0x96, 0x7a, 0x72, 0xf4, 0x11}
    };
    constexpr GUID kTexture8Guid = {
        0xe4cdd575, 0x2866, 0x4f01, {0xb1, 0x2e, 0x7e, 0xec, 0xe1, 0xec, 0x93, 0x58}
    };

    using CreateTextureFn = HRESULT(__stdcall*)(
        IDirect3DDevice8*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture8**);
    using DrawPrimitiveUPFn = HRESULT(__stdcall*)(
        IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    using SetTextureFn = HRESULT(__stdcall*)(
        IDirect3DDevice8*, DWORD, IDirect3DBaseTexture8*);

#pragma pack(push, 1)
    struct ResourceMarker
    {
        uint32_t magic;
        uint32_t version;
        uint32_t candidateId;
        uint32_t moduleTimestamp;
        uint32_t moduleSize;
        uint32_t callerRva;
        uint32_t stackHash;
        uint32_t signatureOrdinal;
        uint32_t originalSize;
        uint32_t actualSize;
    };
#pragma pack(pop)

    struct ModuleIdentity
    {
        uintptr_t base{0};
        uint32_t timestamp{0};
        uint32_t size{0};
        bool valid{false};
    };

    struct Selector
    {
        uint32_t moduleTimestamp{0};
        uint32_t moduleSize{0};
        uint32_t callerRva{0};
        uint32_t stackHash{0};
        uint32_t signatureOrdinal{0};
        uint32_t targetSize{kDefaultTargetSize};
        bool valid{false};
    };

    struct CreatePlan
    {
        bool candidate{false};
        bool resize{false};
        uint32_t candidateId{0};
        uint32_t signatureOrdinal{0};
        uint32_t callerRva{0};
        uint32_t stackHash{0};
        UINT requestedWidth{0};
        UINT requestedHeight{0};
        UINT actualWidth{0};
        UINT actualHeight{0};
    };

    struct CandidateActivity
    {
        uint32_t dsNullUpDraws{0};
        bool firstSeenPending{false};
    };

    struct StateValue
    {
        DWORD value{0};
        bool valid{false};
    };

    class Plugin;

    Plugin* gPlugin = nullptr;
    CreateTextureFn gOriginalCreateTexture = nullptr;
    SetTextureFn gOriginalSetTexture = nullptr;
    DrawPrimitiveUPFn gOriginalDrawPrimitiveUP = nullptr;
    thread_local bool gInsideCreateTexture  = false;
    thread_local bool gInsideSetTexture     = false;
    thread_local bool gInsideDrawPrimitiveUP = false;
    bool gReleased = false;

    static std::string lower_copy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static uint64_t file_size_or_zero(const std::string& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
            return 0;
        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        return size.QuadPart;
    }

    static std::string trim_copy(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    static bool parse_float(const std::string& text, float& value)
    {
        try
        {
            size_t used = 0;
            value       = std::stof(text, &used);
            return used == text.size() && std::isfinite(value);
        }
        catch (...)
        {
            return false;
        }
    }

    static uint32_t vertex_count(const D3DPRIMITIVETYPE type, const UINT primitiveCount)
    {
        switch (type)
        {
            case D3DPT_TRIANGLELIST:
                return primitiveCount * 3;
            case D3DPT_TRIANGLESTRIP:
            case D3DPT_TRIANGLEFAN:
                return primitiveCount + 2;
            default:
                return 0;
        }
    }

    static bool marker_valid(const ResourceMarker& marker, const DWORD size)
    {
        return size == sizeof(marker)
            && marker.magic == kMarkerMagic
            && marker.version == kMarkerVersion;
    }

    class Plugin final : public IPlugin
    {
    public:
        Plugin() = default;
        ~Plugin() override = default;

        const char* GetName() const override { return "SpectralFix"; }
        const char* GetAuthor() const override { return "KraturLabs"; }
        const char* GetDescription() const override
        {
            return "Enlarges and verifies FFXI's client-owned aura blur render target.";
        }
        const char* GetLink() const override { return ""; }
        double GetVersion() const override { return kPluginVersion; }
        double GetInterfaceVersion() const override { return ASHITA_INTERFACE_VERSION; }
        int32_t GetPriority() const override { return -1000; }
        uint32_t GetFlags() const override
        {
            return static_cast<uint32_t>(Ashita::PluginFlags::UseCommands)
                | static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D);
        }

        bool Initialize(IAshitaCore* core, ILogManager*, uint32_t) override
        {
            core_ = core;

            if (core_ == nullptr)
                return false;
            if (gReleased)
            {
                chat("SpectralFix cannot be reloaded safely. Exit and restart the client.");
                return false;
            }

            const auto install = core_->GetInstallPath();
            if (install == nullptr || install[0] == '\0')
                return false;

            installPath_ = install;
            if (!installPath_.empty() && installPath_.back() != '\\' && installPath_.back() != '/')
                installPath_ += '\\';

            const auto logDir = installPath_ + "logs\\spectralfix";
            ::CreateDirectoryA(logDir.c_str(), nullptr);
            logPath_    = logDir + "\\spectralfix.log";
            logArchivePath_ = logDir + "\\spectralfix.previous.log";
            configPath_ = installPath_ + "config\\spectralfix.ini";

            logBytesWritten_ = file_size_or_zero(logPath_);
            if (logBytesWritten_ >= kMaxLogBytes)
            {
                if (::MoveFileExA(logPath_.c_str(), logArchivePath_.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    logBytesWritten_ = 0;
                else
                    logRotationFailedThisRun_ = true;
            }

            log_.open(logPath_, std::ios::out | std::ios::app);
            if (!log_.is_open())
                return false;

            module_ = read_module_identity();
            signatureCounts_.reserve(kMaxSignatureEntries);
            activity_.reserve(kMaxActivityEntries);
            load_selector();

            log_line(std::string("=== SpectralFix v") + kVersionString + " initialized ===");
            if (logRotationFailedThisRun_)
                log_line("Log rotation failed; diagnostics will continue in the current file for this session.");
            log_line("Ashita install: " + installPath_);
            log_line(module_.valid
                ? hex_line("FFXiMain identity", module_.timestamp, module_.size)
                : "FFXiMain identity unavailable during plugin Initialize; will retry at Direct3DInitialize.");
            if (selector_.valid)
            {
                std::ostringstream out;
                out << "Learned selector loaded: timestamp=0x" << std::hex << std::uppercase
                    << selector_.moduleTimestamp << " size=0x" << selector_.moduleSize
                    << " caller=0x" << selector_.callerRva << " stack=0x" << selector_.stackHash
                    << std::dec << " ordinal=" << selector_.signatureOrdinal
                    << " target=" << selector_.targetSize << 'x' << selector_.targetSize;
                log_line(out.str());
            }
            else
            {
                log_line("No saved selector override: ordinal-1 startup default will arm on the first matching FFXiMain allocation.");
            }
            log_line(settings_text());

            gPlugin = this;
            return true;
        }

        void Release() override
        {
            enabled_ = false;
            restore_hooks();
            log_status("release");
            log_line("=== SpectralFix released ===");
            if (log_.is_open())
                log_.close();
            if (gPlugin == this)
                gPlugin = nullptr;
            gReleased = true;
        }

        bool Direct3DInitialize(IDirect3DDevice8* device) override
        {
            device_ = device;
            if (!module_.valid)
                module_ = read_module_identity();

            log_d3d8_module();
            if (device_ == nullptr || !module_.valid)
            {
                log_line("Direct3DInitialize refused: device or FFXiMain identity unavailable; stock rendering retained.");
                return false;
            }

            if (selector_.valid
                && (selector_.moduleTimestamp != module_.timestamp || selector_.moduleSize != module_.size))
            {
                selector_.valid = false;
                selectorLoadedFromConfig_ = false;
                log_line("Saved selector belongs to a different client build; ordinal-1 startup default will be used.");
            }

            if (selector_.valid)
                selectorVerificationPending_ = true;

            D3DCAPS8 caps{};
            if (SUCCEEDED(device_->GetDeviceCaps(&caps)))
            {
                targetCapsKnown_ = true;
                maxTextureWidth_ = caps.MaxTextureWidth;
                maxTextureHeight_ = caps.MaxTextureHeight;
                std::ostringstream capsLine;
                capsLine << "D3D8 texture caps: max=" << maxTextureWidth_
                    << 'x' << maxTextureHeight_;
                log_line(capsLine.str());

                const auto requestedTarget = selector_.valid
                    ? selector_.targetSize
                    : configuredTargetSize_;
                if (requestedTarget > maxTextureWidth_ || requestedTarget > maxTextureHeight_)
                {
                    targetUnsupported_ = true;
                    enabled_ = false;
                    std::ostringstream warning;
                    warning << "TARGET UNSUPPORTED: requested " << requestedTarget << 'x'
                        << requestedTarget << " exceeds the D3D8 device limit; stock rendering retained.";
                    log_line(warning.str());
                    chat("Requested aura target exceeds this D3D8 device's limit. Stock rendering retained; choose a smaller target and restart.");
                }
            }
            else
            {
                log_line("D3D8 texture caps query failed; CreateTexture failure fallback remains active.");
            }

            if (!pin_module())
            {
                enabled_ = false;
                log_line("Module pinning failed; D3D8 hooks were not installed and stock rendering was retained.");
                chat("SpectralFix could not secure its hook lifetime. Stock rendering retained; restart the client.");
                return false;
            }

            if (!install_hooks())
            {
                log_line("D3D8 hook transaction failed; stock rendering retained.");
                return false;
            }

            log_line("Direct3DInitialize complete; wrapper-neutral CreateTexture, SetTexture observation, and DrawPrimitiveUP interception active.");
            chat("SpectralFix loaded. Use /spectralfix help for settings.");
            return true;
        }

        void Direct3DPresent(const RECT*, const RECT*, HWND, const RGNDATA*) override
        {
            ++frames_;

            if ((frames_ % 60) == 0 && !hooks_intact())
            {
                if (activate_native_stage_zero_query_mode())
                {
                    log_line("SetTexture observer released; native stage-zero query mode is active.");
                }
                else if (hookRechains_ < kMaxHookRechains && rechain_displaced_hooks())
                {
                    ++hookRechains_;
                    log_line("D3D8 hook chain reacquired after startup displacement.");
                }
                else if (!hookDisplaced_)
                {
                    hookDisplaced_ = true;
                    enabled_       = false;
                    log_line("HOOK DISPLACED: SpectralFix could not safely re-chain a required D3D8 slot and disabled itself.");
                    chat("SpectralFix disabled: a required D3D8 hook could not be safely reattached. Restart the client.");
                }
            }

            if (pendingLearn_)
            {
                pendingLearn_ = false;
                if (save_selector(pendingMarker_))
                {
                    selectorLearnedThisRun_ = true;
                    log_line("SpectralFix allocation learned and saved. Restart the entire client to apply enlargement.");
                    chat("SpectralFix learned the aura allocation. Exit and relaunch the client once to apply the fix.");
                }
                else
                {
                    log_line("Failed to save the learned allocation selector; stock rendering retained.");
                    chat("SpectralFix found the aura allocation but could not save spectralfix.ini. See spectralfix.log.");
                }
            }

            if (pendingSelectorConfirmation_)
            {
                pendingSelectorConfirmation_ = false;
                selectorVerifiedThisRun_      = true;
                if (selectorFromOrdinalDefault_)
                {
                    const bool saved = save_settings();
                    log_line(saved
                        ? "Ordinal-1 startup default confirmed by aura activity and saved for this client."
                        : "Ordinal-1 startup default confirmed by aura activity; spectralfix.ini could not be saved, but correction remains active.");
                }
                else
                {
                    log_line("Saved selector confirmed by aura activity for this session.");
                }
            }

            if (pendingSelectorMismatchWarning_)
            {
                pendingSelectorMismatchWarning_ = false;
                const bool saved = save_selector(pendingMismatchMarker_);
                log_line(saved
                    ? "SELECTOR MISMATCH: another allocation produced the aura activity. The override was saved; immediate full client exit is required."
                    : "SELECTOR MISMATCH: another allocation produced the aura activity, but the override could not be saved. Immediate full client exit is required.");
                chat("WARNING: SpectralFix detected an unexpected aura allocation and disabled itself.");
                chat(saved
                    ? "Exit the entire client now. The corrected allocation was saved and will apply after relaunch."
                    : "Exit the entire client now. SpectralFix could not save the corrected allocation; see spectralfix.log.");
            }

            if (pendingFirstScale_)
            {
                pendingFirstScale_ = false;
                log_line("First enlarged downsample quad corrected successfully.");
            }
            if (pendingFirstSpread_)
            {
                pendingFirstSpread_ = false;
                log_line("First enlarged blur-tap offset corrected successfully.");
            }
            if (pendingFirstOpacity_)
            {
                pendingFirstOpacity_ = false;
                log_line("First blur-tap opacity adjustment applied successfully.");
            }
            if (pendingFirstComposite_)
            {
                pendingFirstComposite_ = false;
                log_line(compositeOpacityPercent_ <= 0.0001F
                    ? "First classified center composite omitted successfully."
                    : "First classified center composite opacity adjustment applied successfully.");
            }
            if (pendingCompositeStateFailure_)
            {
                pendingCompositeStateFailure_ = false;
                log_line("Center-composite state override or restoration failed; composite adjustment was disabled for this session.");
                chat("Center-composite opacity disabled after a Direct3D state failure; stock composite retained.");
            }
            if (pendingFirstFailure_)
            {
                pendingFirstFailure_ = false;
                log_line("A corrected DrawPrimitiveUP submission failed; the original stock draw was allowed through.");
                chat("SpectralFix draw correction failed and fell back to the original draw. See spectralfix.log.");
            }

            if ((frames_ % 600) == 0)
                log_status("periodic");
        }

        HRESULT handle_draw_primitive_up(
            IDirect3DDevice8* device,
            const DrawPrimitiveUPFn original,
            const D3DPRIMITIVETYPE primitiveType,
            const UINT primitiveCount,
            const void* vertexData,
            const UINT stride)
        {
            ++interceptedDraws_;
            if (!enabled_ || device_ == nullptr || device != device_ || original == nullptr
                || vertexData == nullptr)
                return original != nullptr
                    ? original(device, primitiveType, primitiveCount, vertexData, stride)
                    : D3DERR_INVALIDCALL;

            const auto count = vertex_count(primitiveType, primitiveCount);
            const bool supportedAuraShape = primitiveType == D3DPT_TRIANGLESTRIP
                && primitiveCount == 2 && count == 4
                && stride >= 20 && stride <= kMaxStride;
            if (!supportedAuraShape)
                return original(device, primitiveType, primitiveCount, vertexData, stride);

            ResourceMarker targetMarker{};
            D3DSURFACE_DESC targetDesc{};
            bool depthNull = false;
            const bool markedTarget = current_target_marker(targetMarker, targetDesc, depthNull);

            if (markedTarget)
            {
                auto activityIt = activity_.find(targetMarker.candidateId);
                if (activityIt == activity_.end() && activity_.size() < kMaxActivityEntries)
                    activityIt = activity_.emplace(targetMarker.candidateId, CandidateActivity{}).first;
                if (activityIt == activity_.end())
                {
                    ++activityTrackingDrops_;
                }
                else
                {
                    auto& activity = activityIt->second;
                    if (depthNull)
                        ++activity.dsNullUpDraws;
                    if (!activity.firstSeenPending)
                    {
                        activity.firstSeenPending = true;
                        pendingCandidateSeen_      = true;
                        pendingCandidateMarker_   = targetMarker;
                        pendingCandidateDepthNull_ = depthNull;
                    }

                    if (depthNull && activity.dsNullUpDraws >= 4
                        && selectorVerificationPending_)
                    {
                        const bool matches = selector_identity_matches(
                            selector_.moduleTimestamp,
                            selector_.moduleSize,
                            selector_.callerRva,
                            selector_.stackHash,
                            selector_.signatureOrdinal,
                            targetMarker.moduleTimestamp,
                            targetMarker.moduleSize,
                            targetMarker.callerRva,
                            targetMarker.stackHash,
                            targetMarker.signatureOrdinal);
                        const auto decision = evaluate_selector_activity(true, matches);
                        selectorVerificationPending_ = false;
                        if (decision == SelectorActivityDecision::confirmed)
                        {
                            pendingSelectorConfirmation_ = true;
                        }
                        else if (decision == SelectorActivityDecision::mismatch)
                        {
                            selectorMismatch_               = true;
                            enabled_                        = false;
                            pendingMismatchMarker_          = targetMarker;
                            pendingSelectorMismatchWarning_ = true;
                        }
                    }
                    else if (!selector_.valid && !selectorLearnedThisRun_ && depthNull
                        && activity.dsNullUpDraws >= 4 && !pendingLearn_)
                    {
                        pendingMarker_ = targetMarker;
                        pendingLearn_  = true;
                    }
                }

                if (targetMarker.actualSize > kOriginalSize && depthNull)
                {
                    if (rewrite_downsample(vertexData, stride, count, targetMarker.actualSize))
                    {
                        const auto hr = original(
                            device, primitiveType, primitiveCount, scratch_.data(), stride);
                        if (SUCCEEDED(hr))
                        {
                            ++scaledDraws_;
                            if (scaledDraws_ == 1)
                                pendingFirstScale_ = true;
                            return hr;
                        }
                        ++drawFailures_;
                        pendingFirstFailure_ = true;
                    }
                }
            }
            else if (targetDesc.Width == kOriginalSize && targetDesc.Height == kOriginalSize && depthNull)
            {
                ++unmarkedBlurDraws_;
            }

            auto stageZeroSize = trackedStageZeroSize_;
            auto stageZeroAura = trackedStageZeroAura_;
            auto rewrite = TapRewriteResult{};
            bool centerGeometry = false;
            if (stageZeroAura || nativeStageZeroQueryMode_)
            {
                centerGeometry = matches_center_composite_vertices(
                    vertexData, stride, count, targetDesc.Width, targetDesc.Height);
            }
            if (stageZeroAura)
            {
                rewrite = rewrite_taps(vertexData, stride, count, targetDesc.Width);
            }
            else if (nativeStageZeroQueryMode_)
            {
                const auto probe = rewrite_taps(vertexData, stride, count, targetDesc.Width);
                if (probe.matched || centerGeometry)
                {
                    stageZeroSize = query_stage_zero_size();
                    if (stageZeroSize != 0)
                    {
                        stageZeroAura = true;
                        rewrite = probe;
                    }
                }
            }

            if (stageZeroAura && centerGeometry && !markedTarget && !depthNull
                && center_composite_state_matches(device))
            {
                ++centerCompositeDraws_;
                if (compositeOpacityOverride_ && !compositeAdjustmentDisabledThisSession_)
                {
                    HRESULT compositeResult = D3D_OK;
                    if (draw_center_composite(
                            device, original, primitiveType, primitiveCount,
                            vertexData, stride, compositeResult))
                        return compositeResult;
                }
            }

            if (stageZeroAura && rewrite.matched)
            {
                ++tapDraws_;
                lastEffectiveSpread_ = rewrite.effectiveSpread;
                if (rewrite.rewritten)
                {
                    const auto hr = original(
                        device, primitiveType, primitiveCount, scratch_.data(), stride);
                    if (SUCCEEDED(hr))
                    {
                        if (rewrite.spreadAdjusted)
                        {
                            ++spreadDraws_;
                            if (spreadDraws_ == 1)
                                pendingFirstSpread_ = true;
                        }
                        if (rewrite.opacityAdjusted)
                        {
                            ++opacityDraws_;
                            if (opacityDraws_ == 1)
                                pendingFirstOpacity_ = true;
                        }
                        return hr;
                    }
                    ++drawFailures_;
                    pendingFirstFailure_ = true;
                }
            }


            return original(device, primitiveType, primitiveCount, vertexData, stride);
        }

        bool HandleCommand(int32_t, const char* command, bool) override
        {
            if (command == nullptr)
                return false;

            const auto cmd = lower_copy(command);
            if (cmd == "/unload spectralfix" || cmd == "/plugin unload spectralfix")
            {
                chat("SpectralFix uses an early Direct3D hook. Exit the client instead of unloading or reloading it.");
                return true;
            }
            if (cmd.rfind("/spectralfix", 0) != 0)
                return false;

            if (cmd == "/spectralfix" || cmd == "/spectralfix help")
            {
                show_help();
                return true;
            }
            if (cmd == "/spectralfix status")
            {
                log_status("command");
                chat(settings_text());
                chat(status_text());
                chat("Diagnostics: " + logPath_);
                return true;
            }
            if (selectorMismatch_)
            {
                chat("SpectralFix detected an allocation mismatch. Exit the entire client now; commands are disabled until relaunch.");
                return true;
            }
            std::istringstream input(cmd);
            std::string root;
            std::string action;
            std::string value;
            std::string extra;
            std::string tail;
            input >> root >> action >> value >> extra >> tail;
            if (root == "/spectralfix" && action == "spread" && !value.empty() && extra.empty())
            {
                float requested = 0.0F;
                if (value == "auto")
                {
                    spreadOverride_ = 0.0F;
                }
                else if (!parse_float(value, requested)
                    || requested < kMinManualSpread || requested > kMaxManualSpread)
                {
                    chat("Usage: /spectralfix spread <auto|1.0-16.0>");
                    return true;
                }
                else
                {
                    spreadOverride_ = requested;
                }
                const bool saved = save_settings();
                log_line("User changed " + settings_text() + (saved ? " (saved)." : " (not saved)."));
                chat(settings_text() + (saved ? " Saved." : " Active for this session only; spectralfix.ini could not be saved."));
                log_status("spread-command");
                return true;
            }
            if (root == "/spectralfix" && action == "target" && !value.empty() && extra.empty())
            {
                if (value == "medium" || value == "1024")
                {
                    configuredTargetSize_ = kMediumTargetSize;
                }
                else if (value == "high" || value == "2048")
                {
                    configuredTargetSize_ = kDefaultTargetSize;
                }
                else if (value == "ultra" || value == "4096")
                {
                    configuredTargetSize_ = kUltraTargetSize;
                }
                else
                {
                    chat("Usage: /spectralfix target <medium|high|ultra>");
                    return true;
                }
                const bool saved = save_settings();
                const char* targetMode = configuredTargetSize_ == kMediumTargetSize
                    ? "medium-resolution target mode"
                    : (configuredTargetSize_ == kUltraTargetSize
                        ? "experimental ultra-resolution target mode"
                        : "high-resolution target mode");
                log_line(std::string("User selected ")
                    + targetMode
                    + (saved ? " for the next launch (saved)." : " for the next launch (not saved)."));
                chat(saved
                    ? "Target mode saved. Exit and relaunch the entire client to apply it."
                    : "Target mode could not be saved; see spectralfix.log.");
                log_status("target-command");
                return true;
            }
            if (root == "/spectralfix" && action == "opacity" && !value.empty() && extra.empty())
            {
                float requested = 0.0F;
                if (value == "stock" || value == "auto")
                {
                    opacityPercent_ = kDefaultOpacityPercent;
                }
                else if (!parse_float(value, requested) || requested < 0.0F || requested > 100.0F)
                {
                    chat("Usage: /spectralfix opacity <stock|0-100>");
                    return true;
                }
                else
                {
                    opacityPercent_ = requested;
                }
                const bool saved = save_settings();
                log_line("User changed " + settings_text() + (saved ? " (saved)." : " (not saved)."));
                chat(settings_text() + (saved ? " Saved." : " Active for this session only; spectralfix.ini could not be saved."));
                log_status("opacity-command");
                return true;
            }
            if (root == "/spectralfix" && action == "composite" && value == "opacity"
                && !extra.empty() && tail.empty())
            {
                float requested = 0.0F;
                if (extra == "stock" || extra == "auto")
                {
                    compositeOpacityOverride_ = false;
                    compositeOpacityPercent_ = kDefaultOpacityPercent;
                }
                else if (!parse_float(extra, requested) || requested < 0.0F || requested > 100.0F)
                {
                    chat("Usage: /spectralfix composite opacity <stock|0-100>");
                    return true;
                }
                else
                {
                    compositeOpacityOverride_ = true;
                    compositeOpacityPercent_ = requested;
                    compositeAdjustmentDisabledThisSession_ = false;
                }
                const bool saved = save_settings();
                log_line("User changed " + settings_text() + (saved ? " (saved)." : " (not saved)."));
                chat(settings_text() + (saved ? " Saved." : " Active for this session only; spectralfix.ini could not be saved."));
                log_status("composite-opacity-command");
                return true;
            }

            chat("Unknown command. Use /spectralfix help.");
            return true;
        }

        CreatePlan before_create(
            const UINT width,
            const UINT height,
            const UINT levels,
            const DWORD usage,
            const D3DFORMAT format,
            const D3DPOOL pool,
            void* caller)
        {
            CreatePlan plan{};
            plan.requestedWidth  = width;
            plan.requestedHeight = height;
            plan.actualWidth     = width;
            plan.actualHeight    = height;

            plan.candidate = width == kOriginalSize
                && height == kOriginalSize
                && levels == 1
                && (usage & D3DUSAGE_RENDERTARGET) != 0
                && format == D3DFMT_A8R8G8B8
                && pool == D3DPOOL_DEFAULT;
            if (!plan.candidate)
                return plan;

            plan.candidateId = ++candidateCount_;
            plan.callerRva   = address_rva(caller);
            plan.stackHash   = capture_stack_hash();
            const uint64_t signature = (static_cast<uint64_t>(plan.callerRva) << 32) | plan.stackHash;
            auto signatureIt = signatureCounts_.find(signature);
            if (signatureIt == signatureCounts_.end())
            {
                if (signatureCounts_.size() >= kMaxSignatureEntries)
                {
                    plan.candidate = false;
                    ++signatureTrackingDrops_;
                    return plan;
                }
                signatureIt = signatureCounts_.emplace(signature, 0).first;
            }
            plan.signatureOrdinal = ++signatureIt->second;

            if (should_arm_ordinal_one_default(
                    selector_.valid, plan.candidateId, plan.signatureOrdinal))
            {
                selector_ = Selector{
                    module_.timestamp,
                    module_.size,
                    plan.callerRva,
                    plan.stackHash,
                    plan.signatureOrdinal,
                    configuredTargetSize_,
                    true,
                };
                selectorFromOrdinalDefault_  = true;
                selectorVerificationPending_ = true;
                log_line("Ordinal-1 startup default armed for candidate 1; awaiting aura-activity verification.");
            }

            if (enabled_ && selector_.valid
                && selector_.moduleTimestamp == module_.timestamp
                && selector_.moduleSize == module_.size
                && selector_.callerRva == plan.callerRva
                && selector_.stackHash == plan.stackHash
                && selector_.signatureOrdinal == plan.signatureOrdinal)
            {
                plan.resize       = true;
                plan.actualWidth  = selector_.targetSize;
                plan.actualHeight = selector_.targetSize;
            }
            return plan;
        }

        void after_create(
            CreatePlan& plan,
            const HRESULT firstResult,
            const HRESULT finalResult,
            IDirect3DTexture8* texture,
            const bool fellBack)
        {
            if (!plan.candidate)
                return;

            if (fellBack)
            {
                plan.actualWidth  = plan.requestedWidth;
                plan.actualHeight = plan.requestedHeight;
                ++resizeFailures_;
            }

            ResourceMarker marker{
                kMarkerMagic,
                kMarkerVersion,
                plan.candidateId,
                module_.timestamp,
                module_.size,
                plan.callerRva,
                plan.stackHash,
                plan.signatureOrdinal,
                kOriginalSize,
                plan.actualWidth,
            };

            HRESULT textureMarkerResult = E_FAIL;
            HRESULT surfaceMarkerResult = E_FAIL;
            if (SUCCEEDED(finalResult) && texture != nullptr)
            {
                textureMarkerResult = texture->SetPrivateData(kMarkerGuid, &marker, sizeof(marker), 0);
                IDirect3DSurface8* surface = nullptr;
                if (SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) && surface != nullptr)
                {
                    surfaceMarkerResult = surface->SetPrivateData(
                        kMarkerGuid, &marker, sizeof(marker), 0);
                    surface->Release();
                }
            }

            std::ostringstream out;
            out << "candidate #" << plan.candidateId
                << " caller=0x" << std::hex << std::uppercase << plan.callerRva
                << " stack=0x" << plan.stackHash << std::dec
                << " ordinal=" << plan.signatureOrdinal
                << " request=" << plan.requestedWidth << 'x' << plan.requestedHeight
                << " actual=" << plan.actualWidth << 'x' << plan.actualHeight
                << " first_hr=0x" << std::hex << std::uppercase << static_cast<uint32_t>(firstResult)
                << " final_hr=0x" << static_cast<uint32_t>(finalResult)
                << " texture_marker_hr=0x" << static_cast<uint32_t>(textureMarkerResult)
                << " surface_marker_hr=0x" << static_cast<uint32_t>(surfaceMarkerResult)
                << " texture=0x" << reinterpret_cast<uintptr_t>(texture) << std::dec;
            if (fellBack)
                out << " RESIZE_FAILED_FELL_BACK_TO_256";
            else if (plan.resize)
                out << " RESIZED";
            log_line(out.str());

            if (plan.resize && !fellBack && SUCCEEDED(finalResult))
                ++resizedAllocations_;
        }

        void track_stage_zero_binding(
            IDirect3DDevice8* device,
            const DWORD stage,
            IDirect3DBaseTexture8* texture)
        {
            if (device != device_ || stage != 0)
                return;

            ++stageZeroBindings_;
            trackedStageZeroSize_ = 0;
            trackedStageZeroAura_ = false;
            if (texture == nullptr)
                return;

            ResourceMarker marker{};
            DWORD markerSize = sizeof(marker);
            const auto markerResult = texture->GetPrivateData(kMarkerGuid, &marker, &markerSize);
            if (SUCCEEDED(markerResult) && marker_valid(marker, markerSize)
                && selected_aura_marker_is_trackable(
                    selector_.valid, marker_matches_selector(marker),
                    marker.actualSize, kOriginalSize))
            {
                trackedStageZeroSize_ = marker.actualSize;
                trackedStageZeroAura_ = true;
                ++textureMarkerHits_;
                ++auraStageZeroBindings_;
                return;
            }

            if (texture->GetType() != D3DRTYPE_TEXTURE || !selector_.valid)
                return;
            D3DSURFACE_DESC desc{};
            if (FAILED(static_cast<IDirect3DTexture8*>(texture)->GetLevelDesc(0, &desc)))
            {
                ++textureQueryFailures_;
                return;
            }
            if (selector_.targetSize > kOriginalSize
                && desc.Width == selector_.targetSize
                && desc.Height == selector_.targetSize
                && (desc.Usage & D3DUSAGE_RENDERTARGET) != 0)
            {
                trackedStageZeroSize_ = selector_.targetSize;
                trackedStageZeroAura_ = true;
                ++textureDimensionFallbacks_;
                ++auraStageZeroBindings_;
            }
        }

        uint32_t query_stage_zero_size()
        {
            ++stageZeroQueries_;
            if (device_ == nullptr)
                return 0;

            IDirect3DBaseTexture8* texture = nullptr;
            if (FAILED(device_->GetTexture(0, &texture)))
            {
                ++textureQueryFailures_;
                return 0;
            }
            if (texture == nullptr)
                return 0;

            uint32_t result = 0;
            ResourceMarker marker{};
            DWORD markerSize = sizeof(marker);
            const auto markerResult = texture->GetPrivateData(kMarkerGuid, &marker, &markerSize);
            if (SUCCEEDED(markerResult) && marker_valid(marker, markerSize)
                && selected_aura_marker_is_trackable(
                    selector_.valid, marker_matches_selector(marker),
                    marker.actualSize, kOriginalSize))
            {
                result = marker.actualSize;
                ++textureMarkerHits_;
            }
            else if (texture->GetType() == D3DRTYPE_TEXTURE && selector_.valid
                && selector_.targetSize > kOriginalSize)
            {
                D3DSURFACE_DESC desc{};
                if (FAILED(static_cast<IDirect3DTexture8*>(texture)->GetLevelDesc(0, &desc)))
                {
                    ++textureQueryFailures_;
                }
                else if (desc.Width == selector_.targetSize
                    && desc.Height == selector_.targetSize
                    && (desc.Usage & D3DUSAGE_RENDERTARGET) != 0)
                {
                    result = selector_.targetSize;
                    ++textureDimensionFallbacks_;
                }
            }
            texture->Release();
            if (result != 0)
                ++auraStageZeroQueries_;
            return result;
        }

        static HRESULT __stdcall hook_create_texture(
            IDirect3DDevice8* device,
            UINT width,
            UINT height,
            UINT levels,
            DWORD usage,
            D3DFORMAT format,
            D3DPOOL pool,
            IDirect3DTexture8** output)
        {
            const auto original = gOriginalCreateTexture;
            auto* plugin        = gPlugin;
            if (original == nullptr)
                return D3DERR_INVALIDCALL;
            if (plugin == nullptr || gInsideCreateTexture)
                return original(device, width, height, levels, usage, format, pool, output);

            gInsideCreateTexture = true;
            CreatePlan plan{};
            try
            {
                plan = plugin->before_create(width, height, levels, usage, format, pool, _ReturnAddress());
            }
            catch (...)
            {
                plan = CreatePlan{};
            }

            const auto firstResult = original(
                device,
                plan.candidate ? plan.actualWidth : width,
                plan.candidate ? plan.actualHeight : height,
                levels,
                usage,
                format,
                pool,
                output);

            auto finalResult = firstResult;
            bool fellBack    = false;
            if (plan.resize && FAILED(firstResult))
            {
                if (output != nullptr)
                    *output = nullptr;
                finalResult = original(device, width, height, levels, usage, format, pool, output);
                fellBack    = true;
            }

            try
            {
                plugin->after_create(
                    plan,
                    firstResult,
                    finalResult,
                    output != nullptr ? *output : nullptr,
                    fellBack);
            }
            catch (...)
            {
                plugin->note_create_exception();
            }
            gInsideCreateTexture = false;
            return finalResult;
        }

        static HRESULT __stdcall hook_set_texture(
            IDirect3DDevice8* device,
            const DWORD stage,
            IDirect3DBaseTexture8* texture)
        {
            const auto original = gOriginalSetTexture;
            auto* plugin        = gPlugin;
            if (original == nullptr)
                return D3DERR_INVALIDCALL;
            if (plugin == nullptr || gInsideSetTexture)
                return original(device, stage, texture);

            gInsideSetTexture = true;
            const auto result = original(device, stage, texture);
            if (SUCCEEDED(result))
            {
                try
                {
                    plugin->track_stage_zero_binding(device, stage, texture);
                }
                catch (...)
                {
                    plugin->note_set_texture_exception();
                }
            }
            gInsideSetTexture = false;
            return result;
        }

        static HRESULT __stdcall hook_draw_primitive_up(
            IDirect3DDevice8* device,
            const D3DPRIMITIVETYPE primitiveType,
            const UINT primitiveCount,
            const void* vertexData,
            const UINT stride)
        {
            const auto original = gOriginalDrawPrimitiveUP;
            auto* plugin        = gPlugin;
            if (original == nullptr)
                return D3DERR_INVALIDCALL;
            if (plugin == nullptr || gInsideDrawPrimitiveUP)
                return original(device, primitiveType, primitiveCount, vertexData, stride);

            gInsideDrawPrimitiveUP = true;
            HRESULT result         = D3DERR_INVALIDCALL;
            try
            {
                result = plugin->handle_draw_primitive_up(
                    device, original, primitiveType, primitiveCount, vertexData, stride);
            }
            catch (...)
            {
                plugin->note_draw_exception();
                result = original(device, primitiveType, primitiveCount, vertexData, stride);
            }
            gInsideDrawPrimitiveUP = false;
            return result;
        }

        void note_create_exception()
        {
            ++callbackFailures_;
            log_line("CreateTexture post-processing raised an exception; the original D3D result was preserved.");
        }

        void note_draw_exception()
        {
            ++callbackFailures_;
            pendingFirstFailure_ = true;
            log_line("DrawPrimitiveUP interception raised an exception; the original draw was preserved.");
        }

        void note_set_texture_exception()
        {
            trackedStageZeroSize_ = 0;
            trackedStageZeroAura_ = false;
            ++callbackFailures_;
            log_line("SetTexture observation raised an exception; stage-zero aura tracking was cleared.");
        }

    private:
        IAshitaCore* core_{nullptr};
        IDirect3DDevice8* device_{nullptr};
        void** createHookSlot_{nullptr};
        void* createHookPrevious_{nullptr};
        void** setTextureHookSlot_{nullptr};
        void* setTextureHookPrevious_{nullptr};
        void** drawHookSlot_{nullptr};
        void* drawHookPrevious_{nullptr};

        ModuleIdentity module_{};
        Selector selector_{};
        std::string installPath_{};
        std::string logPath_{};
        std::string logArchivePath_{};
        std::string configPath_{};
        std::ofstream log_{};
        std::mutex logMutex_{};
        uint64_t logBytesWritten_{0};
        bool logRotationFailedThisRun_{false};
        std::unordered_map<uint64_t, uint32_t> signatureCounts_{};
        std::unordered_map<uint32_t, CandidateActivity> activity_{};
        std::array<uint8_t, kMaxVertices * kMaxStride> scratch_{};

        bool enabled_{true};
        bool hookDisplaced_{false};
        bool targetUnsupported_{false};
        bool targetCapsKnown_{false};
        bool modulePinned_{false};
        uint32_t maxTextureWidth_{0};
        uint32_t maxTextureHeight_{0};
        bool nativeStageZeroQueryMode_{false};
        uint32_t hookRechains_{0};
        bool selectorLearnedThisRun_{false};
        bool selectorLoadedFromConfig_{false};
        bool selectorFromOrdinalDefault_{false};
        bool selectorVerificationPending_{false};
        bool selectorVerifiedThisRun_{false};
        bool selectorMismatch_{false};
        bool pendingSelectorConfirmation_{false};
        bool pendingSelectorMismatchWarning_{false};
        bool pendingLearn_{false};
        bool pendingCandidateSeen_{false};
        bool pendingCandidateDepthNull_{false};
        bool pendingFirstScale_{false};
        bool pendingFirstSpread_{false};
        bool pendingFirstOpacity_{false};
        bool pendingFirstComposite_{false};
        bool pendingCompositeStateFailure_{false};
        bool pendingFirstFailure_{false};
        ResourceMarker pendingMarker_{};
        ResourceMarker pendingCandidateMarker_{};
        ResourceMarker pendingMismatchMarker_{};

        uint64_t frames_{0};
        uint32_t candidateCount_{0};
        uint64_t signatureTrackingDrops_{0};
        uint64_t activityTrackingDrops_{0};
        uint32_t resizedAllocations_{0};
        uint32_t trackedStageZeroSize_{0};
        bool trackedStageZeroAura_{false};
        uint64_t interceptedDraws_{0};
        uint64_t stageZeroBindings_{0};
        uint64_t auraStageZeroBindings_{0};
        uint64_t stageZeroQueries_{0};
        uint64_t auraStageZeroQueries_{0};
        uint32_t resizeFailures_{0};
        uint32_t scaledDraws_{0};
        uint32_t tapDraws_{0};
        uint32_t spreadDraws_{0};
        uint32_t opacityDraws_{0};
        uint32_t centerCompositeDraws_{0};
        uint32_t centerCompositeAdjusted_{0};
        uint32_t centerCompositeSkipped_{0};
        uint32_t centerCompositeStateFailures_{0};
        uint32_t drawFailures_{0};
        uint32_t callbackFailures_{0};
        uint32_t unmarkedBlurDraws_{0};
        uint64_t targetQueryFailures_{0};
        uint64_t textureQueryFailures_{0};
        uint64_t surfaceMarkerHits_{0};
        uint64_t textureMarkerHits_{0};
        uint64_t targetDimensionFallbacks_{0};
        uint64_t textureDimensionFallbacks_{0};
        float spreadOverride_{kDefaultSpread};
        float opacityPercent_{kDefaultOpacityPercent};
        float compositeOpacityPercent_{kDefaultCompositeOpacityPercent};
        bool compositeOpacityOverride_{true};
        bool compositeAdjustmentDisabledThisSession_{false};
        float lastEffectiveSpread_{1.0F};
        uint32_t configuredTargetSize_{kDefaultTargetSize};

        static StateValue read_render_state(
            IDirect3DDevice8* device,
            const D3DRENDERSTATETYPE state)
        {
            StateValue result{};
            result.valid = device != nullptr && SUCCEEDED(device->GetRenderState(state, &result.value));
            return result;
        }

        static StateValue read_texture_state(
            IDirect3DDevice8* device,
            const D3DTEXTURESTAGESTATETYPE state)
        {
            StateValue result{};
            result.valid = device != nullptr
                && SUCCEEDED(device->GetTextureStageState(0, state, &result.value));
            return result;
        }

        static bool state_is(const StateValue& state, const DWORD expected)
        {
            return state.valid && state.value == expected;
        }

        bool center_composite_state_matches(IDirect3DDevice8* device) const
        {
            const auto alphaBlend = read_render_state(device, D3DRS_ALPHABLENDENABLE);
            const auto alphaTest = read_render_state(device, D3DRS_ALPHATESTENABLE);
            const auto alphaRef = read_render_state(device, D3DRS_ALPHAREF);
            const auto alphaFunc = read_render_state(device, D3DRS_ALPHAFUNC);
            const auto colorOp = read_texture_state(device, D3DTSS_COLOROP);
            const auto alphaOp = read_texture_state(device, D3DTSS_ALPHAOP);
            return state_is(alphaBlend, FALSE)
                && state_is(alphaTest, TRUE)
                && state_is(alphaRef, 96)
                && state_is(alphaFunc, D3DCMP_GREATER)
                && state_is(colorOp, D3DTOP_SELECTARG2)
                && state_is(alphaOp, D3DTOP_SELECTARG2);
        }

        void note_composite_state_failure()
        {
            ++centerCompositeStateFailures_;
            compositeAdjustmentDisabledThisSession_ = true;
            pendingCompositeStateFailure_ = true;
        }

        bool draw_center_composite(
            IDirect3DDevice8* device,
            const DrawPrimitiveUPFn original,
            const D3DPRIMITIVETYPE primitiveType,
            const UINT primitiveCount,
            const void* vertexData,
            const UINT stride,
            HRESULT& result)
        {
            if (compositeOpacityPercent_ <= 0.0001F)
            {
                ++centerCompositeSkipped_;
                if (centerCompositeSkipped_ == 1)
                    pendingFirstComposite_ = true;
                result = D3D_OK;
                return true;
            }

            const auto count = vertex_count(primitiveType, primitiveCount);
            if (!rewrite_uniform_alpha(
                    vertexData, stride, count, compositeOpacityPercent_,
                    scratch_.data(), scratch_.size()))
                return false;

            const auto alphaBlend = read_render_state(device, D3DRS_ALPHABLENDENABLE);
            const auto srcBlend = read_render_state(device, D3DRS_SRCBLEND);
            const auto dstBlend = read_render_state(device, D3DRS_DESTBLEND);
            const auto blendOp = read_render_state(device, D3DRS_BLENDOP);
            const auto alphaTest = read_render_state(device, D3DRS_ALPHATESTENABLE);
            const auto alphaOp = read_texture_state(device, D3DTSS_ALPHAOP);
            const auto alphaArg1 = read_texture_state(device, D3DTSS_ALPHAARG1);
            const auto alphaArg2 = read_texture_state(device, D3DTSS_ALPHAARG2);
            if (!alphaBlend.valid || !srcBlend.valid || !dstBlend.valid || !blendOp.valid
                || !alphaTest.valid || !alphaOp.valid || !alphaArg1.valid || !alphaArg2.valid)
            {
                note_composite_state_failure();
                return false;
            }

            const auto restore = [&]() {
                bool restored = true;
                restored = SUCCEEDED(device->SetTextureStageState(0, D3DTSS_ALPHAARG2, alphaArg2.value)) && restored;
                restored = SUCCEEDED(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, alphaArg1.value)) && restored;
                restored = SUCCEEDED(device->SetTextureStageState(0, D3DTSS_ALPHAOP, alphaOp.value)) && restored;
                restored = SUCCEEDED(device->SetRenderState(D3DRS_ALPHATESTENABLE, alphaTest.value)) && restored;
                restored = SUCCEEDED(device->SetRenderState(D3DRS_BLENDOP, blendOp.value)) && restored;
                restored = SUCCEEDED(device->SetRenderState(D3DRS_DESTBLEND, dstBlend.value)) && restored;
                restored = SUCCEEDED(device->SetRenderState(D3DRS_SRCBLEND, srcBlend.value)) && restored;
                restored = SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE, alphaBlend.value)) && restored;
                return restored;
            };

            bool configured = true;
            configured = SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE)) && configured;
            configured = SUCCEEDED(device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA)) && configured;
            configured = SUCCEEDED(device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA)) && configured;
            configured = SUCCEEDED(device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD)) && configured;
            configured = SUCCEEDED(device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE)) && configured;
            configured = SUCCEEDED(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE)) && configured;
            configured = SUCCEEDED(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE)) && configured;
            configured = SUCCEEDED(device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE)) && configured;

            if (!configured)
            {
                restore();
                note_composite_state_failure();
                return false;
            }

            const auto drawResult = original(
                device, primitiveType, primitiveCount, scratch_.data(), stride);
            const bool restored = restore();
            if (!restored)
            {
                note_composite_state_failure();
                result = drawResult;
                return true;
            }
            if (FAILED(drawResult))
            {
                ++drawFailures_;
                pendingFirstFailure_ = true;
                return false;
            }

            ++centerCompositeAdjusted_;
            if (centerCompositeAdjusted_ == 1)
                pendingFirstComposite_ = true;
            result = drawResult;
            return true;
        }

        bool pin_module()
        {
            if (modulePinned_)
                return true;

            HMODULE self = nullptr;
            if (!::GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCSTR>(&hook_draw_primitive_up), &self))
                return false;
            modulePinned_ = self != nullptr;
            if (modulePinned_)
                log_line("SpectralFix module pinned for process-lifetime hook safety.");
            return modulePinned_;
        }

        bool write_hook_slot(void** slot, void* value) const
        {
            if (slot == nullptr || value == nullptr)
                return false;

            DWORD oldProtect = 0;
            if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;
            ::InterlockedExchangePointer(
                reinterpret_cast<PVOID volatile*>(slot), value);
            DWORD ignored = 0;
            ::VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
            ::FlushInstructionCache(::GetCurrentProcess(), slot, sizeof(void*));
            return *slot == value;
        }

        static std::string module_path_for_address(void* address)
        {
            if (address == nullptr)
                return "<null>";
            HMODULE module = nullptr;
            if (!::GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(address), &module))
                return "<unmapped>";
            std::array<char, MAX_PATH> path{};
            const auto length = ::GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
            return length == 0 ? "<path unavailable>" : std::string(path.data(), length);
        }

        bool install_hooks()
        {
            auto*** object = reinterpret_cast<void***>(device_);
            if (object == nullptr || *object == nullptr)
                return false;

            createHookSlot_     = &(*object)[kCreateTextureSlot];
            createHookPrevious_ = *createHookSlot_;
            setTextureHookSlot_     = &(*object)[kSetTextureSlot];
            setTextureHookPrevious_ = *setTextureHookSlot_;
            drawHookSlot_       = &(*object)[kDrawPrimitiveUPSlot];
            drawHookPrevious_   = *drawHookSlot_;
            if (createHookPrevious_ == nullptr || setTextureHookPrevious_ == nullptr
                || drawHookPrevious_ == nullptr
                || createHookPrevious_ == reinterpret_cast<void*>(&hook_create_texture)
                || setTextureHookPrevious_ == reinterpret_cast<void*>(&hook_set_texture)
                || drawHookPrevious_ == reinterpret_cast<void*>(&hook_draw_primitive_up))
            {
                createHookSlot_     = nullptr;
                setTextureHookSlot_ = nullptr;
                drawHookSlot_       = nullptr;
                return false;
            }

            gOriginalCreateTexture  = reinterpret_cast<CreateTextureFn>(createHookPrevious_);
            gOriginalSetTexture = reinterpret_cast<SetTextureFn>(setTextureHookPrevious_);
            gOriginalDrawPrimitiveUP = reinterpret_cast<DrawPrimitiveUPFn>(drawHookPrevious_);

            if (!write_hook_slot(createHookSlot_, reinterpret_cast<void*>(&hook_create_texture)))
            {
                createHookSlot_     = nullptr;
                setTextureHookSlot_ = nullptr;
                drawHookSlot_       = nullptr;
                return false;
            }
            if (!write_hook_slot(setTextureHookSlot_, reinterpret_cast<void*>(&hook_set_texture)))
            {
                if (*createHookSlot_ == reinterpret_cast<void*>(&hook_create_texture))
                    write_hook_slot(createHookSlot_, createHookPrevious_);
                createHookSlot_     = nullptr;
                setTextureHookSlot_ = nullptr;
                drawHookSlot_       = nullptr;
                return false;
            }
            if (!write_hook_slot(drawHookSlot_, reinterpret_cast<void*>(&hook_draw_primitive_up)))
            {
                if (*setTextureHookSlot_ == reinterpret_cast<void*>(&hook_set_texture))
                    write_hook_slot(setTextureHookSlot_, setTextureHookPrevious_);
                if (*createHookSlot_ == reinterpret_cast<void*>(&hook_create_texture))
                    write_hook_slot(createHookSlot_, createHookPrevious_);
                createHookSlot_     = nullptr;
                setTextureHookSlot_ = nullptr;
                drawHookSlot_       = nullptr;
                return false;
            }

            std::ostringstream out;
            out << "CreateTexture hook installed: slot=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(createHookSlot_) << " original=0x"
                << reinterpret_cast<uintptr_t>(createHookPrevious_) << " hook=0x"
                << reinterpret_cast<uintptr_t>(&hook_create_texture) << std::dec
                << " owner=" << module_path_for_address(createHookPrevious_);
            log_line(out.str());
            out.str("");
            out.clear();
            out << "SetTexture observer installed: slot=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(setTextureHookSlot_) << " original=0x"
                << reinterpret_cast<uintptr_t>(setTextureHookPrevious_) << " hook=0x"
                << reinterpret_cast<uintptr_t>(&hook_set_texture) << std::dec
                << " owner=" << module_path_for_address(setTextureHookPrevious_);
            log_line(out.str());
            out.str("");
            out.clear();
            out << "DrawPrimitiveUP hook installed: slot=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(drawHookSlot_) << " original=0x"
                << reinterpret_cast<uintptr_t>(drawHookPrevious_) << " hook=0x"
                << reinterpret_cast<uintptr_t>(&hook_draw_primitive_up) << std::dec
                << " owner=" << module_path_for_address(drawHookPrevious_);
            log_line(out.str());
            return true;
        }

        bool hooks_intact() const
        {
            return createHookSlot_ != nullptr
                && drawHookSlot_ != nullptr
                && *createHookSlot_ == reinterpret_cast<void*>(&hook_create_texture)
                && (nativeStageZeroQueryMode_
                    || (setTextureHookSlot_ != nullptr
                        && *setTextureHookSlot_ == reinterpret_cast<void*>(&hook_set_texture)))
                && *drawHookSlot_ == reinterpret_cast<void*>(&hook_draw_primitive_up);
        }

        void log_displaced_slot(const char* name, void** slot, void* expected, void* current)
        {
            if (slot == nullptr || current == expected)
                return;
            std::ostringstream out;
            out << "Hook displacement: " << name << " slot=0x" << std::hex << std::uppercase
                << reinterpret_cast<uintptr_t>(slot) << " expected=0x"
                << reinterpret_cast<uintptr_t>(expected) << " current=0x"
                << reinterpret_cast<uintptr_t>(current) << std::dec
                << " current_owner=" << module_path_for_address(current);
            log_line(out.str());
        }

        bool activate_native_stage_zero_query_mode()
        {
            if (nativeStageZeroQueryMode_
                || createHookSlot_ == nullptr
                || setTextureHookSlot_ == nullptr
                || drawHookSlot_ == nullptr
                || *createHookSlot_ != reinterpret_cast<void*>(&hook_create_texture)
                || *drawHookSlot_ != reinterpret_cast<void*>(&hook_draw_primitive_up))
                return false;

            auto* const setTextureHook = reinterpret_cast<void*>(&hook_set_texture);
            void* const currentSetTexture = *setTextureHookSlot_;
            if (currentSetTexture == nullptr || currentSetTexture == setTextureHook)
                return false;

            const auto owner = lower_copy(module_path_for_address(currentSetTexture));
            const bool isWindowsD3D8 = owner.find("\\windows\\system32\\d3d8.dll") != std::string::npos
                || owner.find("\\windows\\syswow64\\d3d8.dll") != std::string::npos;
            if (!isWindowsD3D8)
                return false;

            log_displaced_slot("SetTexture", setTextureHookSlot_, setTextureHook, currentSetTexture);
            setTextureHookPrevious_ = currentSetTexture;
            gOriginalSetTexture = reinterpret_cast<SetTextureFn>(currentSetTexture);
            setTextureHookSlot_ = nullptr;
            trackedStageZeroSize_ = 0;
            nativeStageZeroQueryMode_ = true;
            return true;
        }

        bool rechain_displaced_hooks()
        {
            if (createHookSlot_ == nullptr || setTextureHookSlot_ == nullptr || drawHookSlot_ == nullptr)
                return false;

            auto* const createHook = reinterpret_cast<void*>(&hook_create_texture);
            auto* const setTextureHook = reinterpret_cast<void*>(&hook_set_texture);
            auto* const drawHook = reinterpret_cast<void*>(&hook_draw_primitive_up);
            void* const currentCreate = *createHookSlot_;
            void* const currentSetTexture = *setTextureHookSlot_;
            void* const currentDraw = *drawHookSlot_;

            log_displaced_slot("CreateTexture", createHookSlot_, createHook, currentCreate);
            log_displaced_slot("SetTexture", setTextureHookSlot_, setTextureHook, currentSetTexture);
            log_displaced_slot("DrawPrimitiveUP", drawHookSlot_, drawHook, currentDraw);

            const auto invalidTarget = [createHook, setTextureHook, drawHook](void* current, void* expected) {
                return current == nullptr
                    || (current != expected
                        && (current == createHook || current == setTextureHook || current == drawHook));
            };
            if (invalidTarget(currentCreate, createHook)
                || invalidTarget(currentSetTexture, setTextureHook)
                || invalidTarget(currentDraw, drawHook))
            {
                log_line("Hook re-chain refused: a replacement target was null or pointed at a mismatched SpectralFix hook.");
                return false;
            }

            void* const oldCreatePrevious = createHookPrevious_;
            void* const oldSetTexturePrevious = setTextureHookPrevious_;
            void* const oldDrawPrevious = drawHookPrevious_;
            const bool replaceCreate = currentCreate != createHook;
            const bool replaceSetTexture = currentSetTexture != setTextureHook;
            const bool replaceDraw = currentDraw != drawHook;

            if (replaceCreate)
            {
                createHookPrevious_ = currentCreate;
                gOriginalCreateTexture = reinterpret_cast<CreateTextureFn>(currentCreate);
            }
            if (replaceSetTexture)
            {
                setTextureHookPrevious_ = currentSetTexture;
                gOriginalSetTexture = reinterpret_cast<SetTextureFn>(currentSetTexture);
            }
            if (replaceDraw)
            {
                drawHookPrevious_ = currentDraw;
                gOriginalDrawPrimitiveUP = reinterpret_cast<DrawPrimitiveUPFn>(currentDraw);
            }

            bool wroteCreate = false;
            bool wroteSetTexture = false;
            bool wroteDraw = false;
            if (replaceCreate)
                wroteCreate = write_hook_slot(createHookSlot_, createHook);
            if ((!replaceCreate || wroteCreate) && replaceSetTexture)
                wroteSetTexture = write_hook_slot(setTextureHookSlot_, setTextureHook);
            if ((!replaceCreate || wroteCreate) && (!replaceSetTexture || wroteSetTexture) && replaceDraw)
                wroteDraw = write_hook_slot(drawHookSlot_, drawHook);

            const bool complete = (!replaceCreate || wroteCreate)
                && (!replaceSetTexture || wroteSetTexture)
                && (!replaceDraw || wroteDraw)
                && hooks_intact();
            if (complete)
                return true;

            if (wroteDraw && *drawHookSlot_ == drawHook)
                write_hook_slot(drawHookSlot_, currentDraw);
            if (wroteSetTexture && *setTextureHookSlot_ == setTextureHook)
                write_hook_slot(setTextureHookSlot_, currentSetTexture);
            if (wroteCreate && *createHookSlot_ == createHook)
                write_hook_slot(createHookSlot_, currentCreate);
            createHookPrevious_ = oldCreatePrevious;
            setTextureHookPrevious_ = oldSetTexturePrevious;
            drawHookPrevious_ = oldDrawPrevious;
            gOriginalCreateTexture = reinterpret_cast<CreateTextureFn>(oldCreatePrevious);
            gOriginalSetTexture = reinterpret_cast<SetTextureFn>(oldSetTexturePrevious);
            gOriginalDrawPrimitiveUP = reinterpret_cast<DrawPrimitiveUPFn>(oldDrawPrevious);
            log_line("Hook re-chain failed transactionally; any slots written by SpectralFix were rolled back.");
            return false;
        }

        void restore_hooks()
        {
            if (drawHookSlot_ != nullptr && drawHookPrevious_ != nullptr)
            {
                if (*drawHookSlot_ == reinterpret_cast<void*>(&hook_draw_primitive_up))
                {
                    if (write_hook_slot(drawHookSlot_, drawHookPrevious_))
                        log_line("DrawPrimitiveUP hook restored.");
                }
                else
                {
                    log_line("DrawPrimitiveUP slot not restored because another component owns it. Client exit is required.");
                }
            }
            if (setTextureHookSlot_ != nullptr && setTextureHookPrevious_ != nullptr)
            {
                if (*setTextureHookSlot_ == reinterpret_cast<void*>(&hook_set_texture))
                {
                    if (write_hook_slot(setTextureHookSlot_, setTextureHookPrevious_))
                        log_line("SetTexture observer restored.");
                }
                else
                {
                    log_line("SetTexture slot not restored because another component owns it. Client exit is required.");
                }
            }
            if (createHookSlot_ != nullptr && createHookPrevious_ != nullptr)
            {
                if (*createHookSlot_ == reinterpret_cast<void*>(&hook_create_texture))
                {
                    if (write_hook_slot(createHookSlot_, createHookPrevious_))
                        log_line("CreateTexture hook restored.");
                }
                else
                {
                    log_line("CreateTexture slot not restored because another component owns it. Client exit is required.");
                }
            }
            drawHookSlot_       = nullptr;
            setTextureHookSlot_ = nullptr;
            createHookSlot_     = nullptr;
        }

        ModuleIdentity read_module_identity() const
        {
            ModuleIdentity identity{};
            const auto module = ::GetModuleHandleA("FFXiMain.dll");
            if (module == nullptr)
                return identity;

            const auto base = reinterpret_cast<uintptr_t>(module);
            const auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
                return identity;
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
                return identity;

            identity.base      = base;
            identity.timestamp = nt->FileHeader.TimeDateStamp;
            identity.size      = nt->OptionalHeader.SizeOfImage;
            identity.valid     = identity.size != 0;
            return identity;
        }

        uint32_t address_rva(void* address) const
        {
            const auto value = reinterpret_cast<uintptr_t>(address);
            if (!module_.valid || value < module_.base || value >= module_.base + module_.size)
                return 0;
            return static_cast<uint32_t>(value - module_.base);
        }

        uint32_t capture_stack_hash() const
        {
            void* frames[24]{};
            const auto count = ::CaptureStackBackTrace(0, 24, frames, nullptr);
            uint32_t hash    = 2166136261u;
            uint32_t used    = 0;
            for (USHORT i = 0; i < count && used < 6; ++i)
            {
                const auto rva = address_rva(frames[i]);
                if (rva == 0)
                    continue;
                hash ^= rva;
                hash *= 16777619u;
                ++used;
            }
            return used == 0 ? 0 : hash;
        }

        ResourceMarker selector_marker() const
        {
            return ResourceMarker{
                kMarkerMagic,
                kMarkerVersion,
                selector_.signatureOrdinal,
                selector_.moduleTimestamp,
                selector_.moduleSize,
                selector_.callerRva,
                selector_.stackHash,
                selector_.signatureOrdinal,
                kOriginalSize,
                selector_.targetSize,
            };
        }

        bool marker_matches_selector(const ResourceMarker& marker) const
        {
            return selector_.valid && selector_identity_matches(
                selector_.moduleTimestamp,
                selector_.moduleSize,
                selector_.callerRva,
                selector_.stackHash,
                selector_.signatureOrdinal,
                marker.moduleTimestamp,
                marker.moduleSize,
                marker.callerRva,
                marker.stackHash,
                marker.signatureOrdinal);
        }

        bool current_target_marker(
            ResourceMarker& marker,
            D3DSURFACE_DESC& desc,
            bool& depthNull)
        {
            depthNull = false;
            IDirect3DSurface8* target = nullptr;
            const auto targetResult = device_->GetRenderTarget(&target);
            if (FAILED(targetResult))
            {
                ++targetQueryFailures_;
                return false;
            }
            if (target == nullptr)
                return false;

            const auto descResult = target->GetDesc(&desc);
            if (FAILED(descResult))
            {
                ++targetQueryFailures_;
                target->Release();
                return false;
            }

            IDirect3DSurface8* depth = nullptr;
            const auto depthResult = device_->GetDepthStencilSurface(&depth);
            depthNull = FAILED(depthResult) || depth == nullptr;
            if (depth != nullptr)
                depth->Release();

            const bool possibleAuraTarget =
                (desc.Width == kOriginalSize && desc.Height == kOriginalSize)
                || (selector_.valid
                    && desc.Width == selector_.targetSize
                    && desc.Height == selector_.targetSize);
            if (!possibleAuraTarget)
            {
                target->Release();
                return false;
            }

            DWORD surfaceMarkerSize = sizeof(marker);
            const auto surfaceMarkerResult = target->GetPrivateData(
                kMarkerGuid, &marker, &surfaceMarkerSize);

            if (SUCCEEDED(surfaceMarkerResult) && marker_valid(marker, surfaceMarkerSize))
            {
                ++surfaceMarkerHits_;
                target->Release();
                return true;
            }

            IDirect3DTexture8* texture = nullptr;
            const auto containerResult = target->GetContainer(
                kTexture8Guid, reinterpret_cast<void**>(&texture));
            target->Release();

            if (SUCCEEDED(containerResult) && texture != nullptr)
            {
                DWORD textureMarkerSize = sizeof(marker);
                const auto textureMarkerResult = texture->GetPrivateData(
                    kMarkerGuid, &marker, &textureMarkerSize);
                texture->Release();
                if (SUCCEEDED(textureMarkerResult) && marker_valid(marker, textureMarkerSize))
                {
                    ++textureMarkerHits_;
                    return true;
                }
            }
            else if (texture != nullptr)
            {
                texture->Release();
            }

            if (selector_.valid && selector_.targetSize > kOriginalSize && depthNull
                && desc.Width == selector_.targetSize
                && desc.Height == selector_.targetSize
                && (desc.Usage & D3DUSAGE_RENDERTARGET) != 0)
            {
                marker = selector_marker();
                ++targetDimensionFallbacks_;
                return true;
            }
            return false;
        }

        bool rewrite_downsample(
            const void* vertexData,
            const UINT stride,
            const uint32_t count,
            const uint32_t targetSize)
        {
            return rewrite_downsample_vertices(
                vertexData, stride, count, targetSize, kOriginalSize,
                scratch_.data(), scratch_.size());
        }

        TapRewriteResult rewrite_taps(
            const void* vertexData,
            const UINT stride,
            const uint32_t count,
            const UINT renderTargetWidth)
        {
            const auto automatic = std::clamp(
                static_cast<float>(renderTargetWidth) / static_cast<float>(kSpreadBase),
                kMinManualSpread, kMaxManualSpread);
            const auto spread = spreadOverride_ > 0.0F ? spreadOverride_ : automatic;
            return rewrite_tap_vertices(
                vertexData, stride, count, spread, opacityPercent_ / 100.0F,
                scratch_.data(), scratch_.size());
        }

        bool write_config(const Selector& selected)
        {
            const auto temporaryPath = configPath_ + ".tmp";
            std::ofstream config(temporaryPath, std::ios::out | std::ios::trunc);
            if (!config.is_open())
                return false;
            config << "# SpectralFix selector and visual settings. The selector is client-build-specific.\n";
            config << "version=1\n";
            config << "module_timestamp=0x" << std::hex << std::uppercase << selected.moduleTimestamp << '\n';
            config << "module_size=0x" << selected.moduleSize << '\n';
            config << "caller_rva=0x" << selected.callerRva << '\n';
            config << "stack_hash=0x" << selected.stackHash << std::dec << '\n';
            config << "signature_ordinal=" << selected.signatureOrdinal << '\n';
            config << "target_size=" << selected.targetSize << '\n';
            config << std::fixed << std::setprecision(2);
            config << "spread_override=" << spreadOverride_ << " # 0 = automatic\n";
            config << "opacity_percent=" << opacityPercent_ << " # 100 = stock tap opacity\n";
            config << "composite_opacity_percent=";
            if (compositeOpacityOverride_)
                config << compositeOpacityPercent_;
            else
                config << "stock";
            config << " # stock = original hard-cutout center pass\n";
            config.flush();
            const bool complete = config.good();
            config.close();
            if (!complete)
            {
                ::DeleteFileA(temporaryPath.c_str());
                return false;
            }
            if (!::MoveFileExA(
                    temporaryPath.c_str(), configPath_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                ::DeleteFileA(temporaryPath.c_str());
                return false;
            }
            return true;
        }

        bool save_selector(const ResourceMarker& marker)
        {
            Selector learned{
                marker.moduleTimestamp,
                marker.moduleSize,
                marker.callerRva,
                marker.stackHash,
                marker.signatureOrdinal,
                configuredTargetSize_,
                true,
            };
            return write_config(learned);
        }

        bool save_settings()
        {
            if (!selector_.valid)
                return false;
            auto saved       = selector_;
            saved.targetSize = configuredTargetSize_;
            return write_config(saved);
        }

        void load_selector()
        {
            std::ifstream config(configPath_);
            if (!config.is_open())
                return;

            Selector parsed{};
            std::string line;
            uint32_t version = 0;
            while (std::getline(config, line))
            {
                const auto hash = line.find('#');
                if (hash != std::string::npos)
                    line.erase(hash);
                const auto equal = line.find('=');
                if (equal == std::string::npos)
                    continue;
                auto key   = lower_copy(trim_copy(line.substr(0, equal)));
                auto value = trim_copy(line.substr(equal + 1));
                if (key == "spread_override")
                {
                    float number = 0.0F;
                    if (parse_float(value, number)
                        && (number == 0.0F
                            || (number >= kMinManualSpread && number <= kMaxManualSpread)))
                        spreadOverride_ = number;
                    else
                        log_line("Ignored malformed spectralfix.ini value for key: " + key);
                    continue;
                }
                if (key == "opacity_percent")
                {
                    float number = 0.0F;
                    if (parse_float(value, number) && number >= 0.0F && number <= 100.0F)
                        opacityPercent_ = number;
                    else
                        log_line("Ignored malformed spectralfix.ini value for key: " + key);
                    continue;
                }
                if (key == "composite_opacity_percent")
                {
                    if (lower_copy(value) == "stock" || lower_copy(value) == "auto")
                    {
                        compositeOpacityOverride_ = false;
                        compositeOpacityPercent_ = kDefaultOpacityPercent;
                    }
                    else
                    {
                        float number = 0.0F;
                        if (parse_float(value, number) && number >= 0.0F && number <= 100.0F)
                        {
                            compositeOpacityOverride_ = true;
                            compositeOpacityPercent_ = number;
                        }
                        else
                        {
                            log_line("Ignored malformed spectralfix.ini value for key: " + key);
                        }
                    }
                    continue;
                }
                try
                {
                    const auto number = static_cast<uint32_t>(std::stoul(value, nullptr, 0));
                    if (key == "version") version = number;
                    else if (key == "module_timestamp") parsed.moduleTimestamp = number;
                    else if (key == "module_size") parsed.moduleSize = number;
                    else if (key == "caller_rva") parsed.callerRva = number;
                    else if (key == "stack_hash") parsed.stackHash = number;
                    else if (key == "signature_ordinal") parsed.signatureOrdinal = number;
                    else if (key == "target_size") parsed.targetSize = number;
                }
                catch (...)
                {
                    log_line("Ignored malformed spectralfix.ini value for key: " + key);
                }
            }
            // Ashita can make caller_rva zero; the stack hash and ordinal remain authoritative.
            parsed.valid = selector_fields_valid(
                version,
                parsed.moduleTimestamp,
                parsed.moduleSize,
                parsed.callerRva,
                parsed.stackHash,
                parsed.signatureOrdinal,
                parsed.targetSize);
            selector_ = parsed;
            selectorLoadedFromConfig_ = selector_.valid;
            if (selector_.valid)
                configuredTargetSize_ = selector_.targetSize;
            if (!selector_.valid)
                log_line("spectralfix.ini exists but is incomplete or invalid; observe-only mode retained.");
        }

        void log_d3d8_module()
        {
            const auto module = ::GetModuleHandleA("d3d8.dll");
            if (module == nullptr)
            {
                log_line("d3d8.dll module not found.");
                return;
            }
            std::array<char, MAX_PATH> path{};
            const auto count = ::GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
            log_line(count == 0 ? "d3d8.dll path unavailable." : "d3d8.dll module: " + std::string(path.data(), count));
        }

        std::string mode_text() const
        {
            if (selectorMismatch_)
                return "disabled after allocation mismatch; immediate client exit required";
            if (hookDisplaced_)
                return "disabled after hook displacement";
            if (targetUnsupported_)
                return "disabled because requested target exceeds device limits";
            if (selectorFromOrdinalDefault_ && selectorVerificationPending_)
                return "ordinal-1 startup default active; verification pending";
            if (selectorFromOrdinalDefault_ && selectorVerifiedThisRun_)
                return "ordinal-1 startup default verified";
            if (selector_.valid)
                return selectorLoadedFromConfig_
                    ? "saved selector armed for this client build"
                    : "selector armed for this client build";
            if (selectorLearnedThisRun_)
                return "selector learned; full client restart required";
            return candidateCount_ == 0
                ? "ordinal-1 startup default waiting for allocation"
                : "observe-only fallback learning mode";
        }

        std::string settings_text() const
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << "SpectralFix settings: spread=";
            if (spreadOverride_ > 0.0F)
                out << spreadOverride_ << " (manual)";
            else
                out << "auto";
            out << ", target=";
            const auto activeTargetSize = selector_.valid
                ? selector_.targetSize
                : configuredTargetSize_;
            if (activeTargetSize == kMediumTargetSize)
                out << "medium resolution";
            else if (activeTargetSize == kUltraTargetSize)
                out << "ultra resolution (experimental)";
            else
                out << "high resolution";
            if (selector_.valid && configuredTargetSize_ != selector_.targetSize)
                out << " (change to " << configuredTargetSize_ << " pending restart)";
            out << ", effective=";
            if (tapDraws_ == 0)
                out << "pending";
            else
                out << lastEffectiveSpread_;
            out << ", tap opacity=" << opacityPercent_ << "%";
            out << ", center composite=";
            if (compositeOpacityOverride_)
                out << compositeOpacityPercent_ << "%";
            else
                out << "stock";
            if (compositeAdjustmentDisabledThisSession_)
                out << " (disabled after state failure)";
            return out.str();
        }

        std::string status_text() const
        {
            std::ostringstream out;
            out << "SpectralFix: " << mode_text()
                << "; candidates=" << candidateCount_
                << ", resized=" << resizedAllocations_
                << ", intercepted draws=" << interceptedDraws_
                << ", aura texture binds=" << auraStageZeroBindings_
                << ", aura texture queries=" << auraStageZeroQueries_
                << ", scale draws=" << scaledDraws_
                << ", tap draws=" << tapDraws_
                << ", spread draws=" << spreadDraws_
                << ", opacity draws=" << opacityDraws_
                << ", center composites=" << centerCompositeDraws_
                << ", center adjusted=" << centerCompositeAdjusted_
                << ", center skipped=" << centerCompositeSkipped_
                << ", draw failures=" << drawFailures_
                << ", unmarked blur draws=" << unmarkedBlurDraws_ << '.';
            return out.str();
        }

        void log_status(const char* reason)
        {
            std::ostringstream out;
            out << "status[" << reason << "] frames=" << frames_
                << " mode=" << mode_text()
                << " candidates=" << candidateCount_
                << " signature_entries=" << signatureCounts_.size()
                << " activity_entries=" << activity_.size()
                << " signature_tracking_drops=" << signatureTrackingDrops_
                << " activity_tracking_drops=" << activityTrackingDrops_
                << " resized=" << resizedAllocations_
                << " intercepted=" << interceptedDraws_
                << " stage0_bindings=" << stageZeroBindings_
                << " aura_stage0_bindings=" << auraStageZeroBindings_
                << " stage0_queries=" << stageZeroQueries_
                << " aura_stage0_queries=" << auraStageZeroQueries_
                << " tracked_stage0_size=" << trackedStageZeroSize_
                << " tracked_stage0_aura=" << (trackedStageZeroAura_ ? "true" : "false")
                << " resize_failures=" << resizeFailures_
                << " scaled=" << scaledDraws_
                << " tap_draws=" << tapDraws_
                << " spread=" << spreadDraws_
                << " opacity_draws=" << opacityDraws_
                << " spread_mode=" << (spreadOverride_ > 0.0F ? "manual" : "auto")
                << " spread_value=" << std::fixed << std::setprecision(2)
                << (spreadOverride_ > 0.0F ? spreadOverride_ : 0.0F)
                << " effective_spread=" << lastEffectiveSpread_
                << " opacity_percent=" << opacityPercent_
                << " composite_opacity_mode=" << (compositeOpacityOverride_ ? "manual" : "stock")
                << " composite_opacity_percent=" << compositeOpacityPercent_
                << " center_composites=" << centerCompositeDraws_
                << " center_adjusted=" << centerCompositeAdjusted_
                << " center_skipped=" << centerCompositeSkipped_
                << " center_state_failures=" << centerCompositeStateFailures_
                << " center_adjustment_disabled=" << (compositeAdjustmentDisabledThisSession_ ? "true" : "false")
                << " active_target_size=" << (selector_.valid ? selector_.targetSize : 0)
                << " configured_target_size=" << configuredTargetSize_
                << " target_caps_known=" << (targetCapsKnown_ ? "true" : "false")
                << " max_texture_width=" << maxTextureWidth_
                << " max_texture_height=" << maxTextureHeight_
                << " target_unsupported=" << (targetUnsupported_ ? "true" : "false")
                << " module_pinned=" << (modulePinned_ ? "true" : "false")
                << " hook_rechains=" << hookRechains_ << std::defaultfloat
                << " selector_source="
                << (selectorFromOrdinalDefault_ ? "ordinal1-default"
                    : (selectorLoadedFromConfig_ ? "saved" : "none"))
                << " selector_verification="
                << (selectorMismatch_ ? "mismatch"
                    : (selectorVerificationPending_ ? "pending"
                        : (selectorVerifiedThisRun_ ? "confirmed" : "not-required")))
                << " native_stage0_query_mode=" << (nativeStageZeroQueryMode_ ? "true" : "false")
                << " log_bytes=" << logBytesWritten_
                << " log_rotation_failed=" << (logRotationFailedThisRun_ ? "true" : "false")
                << " draw_failures=" << drawFailures_
                << " callback_failures=" << callbackFailures_
                << " unmarked_blur_draws=" << unmarkedBlurDraws_
                << " target_query_failures=" << targetQueryFailures_
                << " texture_query_failures=" << textureQueryFailures_
                << " surface_marker_hits=" << surfaceMarkerHits_
                << " texture_marker_hits=" << textureMarkerHits_
                << " target_dimension_fallbacks=" << targetDimensionFallbacks_
                << " texture_dimension_fallbacks=" << textureDimensionFallbacks_;
            if (pendingCandidateSeen_)
            {
                out << " last_candidate=" << pendingCandidateMarker_.candidateId
                    << " last_dsnull=" << (pendingCandidateDepthNull_ ? "true" : "false");
                pendingCandidateSeen_ = false;
            }
            log_line(out.str());
        }

        static std::string hex_line(const char* label, const uint32_t first, const uint32_t second)
        {
            std::ostringstream out;
            out << label << ": timestamp=0x" << std::hex << std::uppercase << first
                << " size=0x" << second;
            return out.str();
        }

        void show_help() const
        {
            chat(std::string("SpectralFix v") + kVersionString + " settings:");
            chat("/spectralfix target <medium|high|ultra> - 1024, 2048, or 4096 (restart required)");
            chat("/spectralfix spread <auto|1.0-16.0> - aura spread");
            chat("/spectralfix opacity <stock|0-100> - shifted glow opacity percent");
            chat("/spectralfix composite opacity <stock|0-100> - center glow opacity percent");
            chat("Defaults: high (2048), spread 2.0, stock glow opacity, center 25%.");
        }

        void chat(const std::string& message) const
        {
            if (core_ != nullptr && core_->GetChatManager() != nullptr)
            {
                const auto full = std::string("[SpectralFix] ") + message;
                core_->GetChatManager()->Write(1, false, full.c_str());
            }
        }

        void log_line(const std::string& message)
        {
            if (!log_.is_open())
                return;
            SYSTEMTIME time{};
            ::GetLocalTime(&time);
            std::ostringstream record;
            record << std::setfill('0')
                << time.wYear << '-' << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay
                << ' ' << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
                << ':' << std::setw(2) << time.wSecond << '.' << std::setw(3) << time.wMilliseconds
                << " | " << message << '\n';
            const auto text = record.str();

            std::unique_lock<std::mutex> lock(logMutex_);
            if (!logRotationFailedThisRun_ && logBytesWritten_ + text.size() > kMaxLogBytes)
            {
                log_.flush();
                log_.close();
                if (::MoveFileExA(logPath_.c_str(), logArchivePath_.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    logBytesWritten_ = 0;
                    log_.open(logPath_, std::ios::out | std::ios::trunc);
                }
                else
                {
                    logRotationFailedThisRun_ = true;
                    log_.open(logPath_, std::ios::out | std::ios::app);
                    logBytesWritten_ = file_size_or_zero(logPath_);
                }
            }

            if (log_.is_open())
            {
                log_ << text;
                log_.flush();
                if (log_.good())
                    logBytesWritten_ += text.size();
            }
            lock.unlock();
        }
    };
}

__declspec(dllexport) IPlugin* __stdcall expCreatePlugin(const char* args)
{
    UNREFERENCED_PARAMETER(args);
    return new spectralfix::Plugin();
}

__declspec(dllexport) void __stdcall expDestroyPlugin(void* instance)
{
    delete static_cast<spectralfix::Plugin*>(instance);
}

__declspec(dllexport) double __stdcall expGetInterfaceVersion()
{
    return ASHITA_INTERFACE_VERSION;
}
