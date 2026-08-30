#include "composite_state.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

namespace
{
    // Records render and texture-stage state the way a device would, so a test can
    // assert that every value SpectralFix touched came back to what it was.
    class RecordingDevice
    {
    public:
        std::map<DWORD, DWORD> renderStates;
        std::map<DWORD, DWORD> textureStates;
        int writes{0};
        int failWritesAfter{-1}; // -1 never fails; otherwise fail once this many writes have succeeded.
        bool failReads{false};

        HRESULT GetRenderState(const D3DRENDERSTATETYPE state, DWORD* value)
        {
            if (failReads)
                return E_FAIL;
            *value = renderStates[static_cast<DWORD>(state)];
            return S_OK;
        }

        HRESULT GetTextureStageState(DWORD, const D3DTEXTURESTAGESTATETYPE state, DWORD* value)
        {
            if (failReads)
                return E_FAIL;
            *value = textureStates[static_cast<DWORD>(state)];
            return S_OK;
        }

        HRESULT SetRenderState(const D3DRENDERSTATETYPE state, const DWORD value)
        {
            if (failWritesAfter >= 0 && writes >= failWritesAfter)
                return E_FAIL;
            ++writes;
            renderStates[static_cast<DWORD>(state)] = value;
            return S_OK;
        }

        HRESULT SetTextureStageState(DWORD, const D3DTEXTURESTAGESTATETYPE state, const DWORD value)
        {
            if (failWritesAfter >= 0 && writes >= failWritesAfter)
                return E_FAIL;
            ++writes;
            textureStates[static_cast<DWORD>(state)] = value;
            return S_OK;
        }
    };

    using Scope = spectralfix::BasicCompositeStateScope<RecordingDevice>;

    RecordingDevice make_device()
    {
        RecordingDevice device;
        // Values matching FFXI's centered hard-cutout pass.
        device.renderStates[D3DRS_ALPHABLENDENABLE] = FALSE;
        device.renderStates[D3DRS_SRCBLEND]         = D3DBLEND_ONE;
        device.renderStates[D3DRS_DESTBLEND]        = D3DBLEND_ZERO;
        device.renderStates[D3DRS_BLENDOP]          = D3DBLENDOP_ADD;
        device.renderStates[D3DRS_ALPHATESTENABLE]  = TRUE;
        device.textureStates[D3DTSS_ALPHAOP]        = D3DTOP_SELECTARG2;
        device.textureStates[D3DTSS_ALPHAARG1]      = D3DTA_TEXTURE;
        device.textureStates[D3DTSS_ALPHAARG2]      = D3DTA_DIFFUSE;
        return device;
    }

    bool matches(const RecordingDevice& left, const RecordingDevice& right)
    {
        return left.renderStates == right.renderStates
            && left.textureStates == right.textureStates;
    }
}

int main()
{
    const auto original = make_device();

    // 1. The ordinary path: apply, draw, restore explicitly.
    {
        auto device = make_device();
        bool restoreFailed = false;
        {
            Scope scope(&device, &restoreFailed);
            if (!scope.captured() || !scope.apply())
            {
                std::cerr << "a healthy device failed capture or apply\n";
                return 1;
            }
            if (device.renderStates[D3DRS_ALPHABLENDENABLE] != TRUE
                || device.textureStates[D3DTSS_ALPHAOP] != D3DTOP_MODULATE)
            {
                std::cerr << "apply did not install the composite states\n";
                return 2;
            }
            if (!scope.restore())
            {
                std::cerr << "restore reported failure on a healthy device\n";
                return 3;
            }
        }
        if (restoreFailed || !matches(device, original))
        {
            std::cerr << "explicit restore did not return every state\n";
            return 4;
        }
    }

    // 2. The regression this RAII scope exists for: a draw that throws must still
    //    leave the device exactly as it was found. Before the fix, the restore was
    //    skipped and SpectralFix's blend state leaked into the rest of the frame.
    {
        auto device = make_device();
        bool restoreFailed = false;
        bool threw = false;
        try
        {
            Scope scope(&device, &restoreFailed);
            if (!scope.captured() || !scope.apply())
            {
                std::cerr << "setup for the unwind case failed\n";
                return 5;
            }
            throw std::runtime_error("simulated faulting draw");
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        if (!threw)
        {
            std::cerr << "the simulated fault did not propagate\n";
            return 6;
        }
        if (restoreFailed || !matches(device, original))
        {
            std::cerr << "an unwinding draw leaked SpectralFix render state\n";
            return 7;
        }
    }

    // 3. A partial apply, where the device starts refusing writes midway, is still
    //    fully restored rather than left half-configured.
    {
        auto device = make_device();
        device.failWritesAfter = 4;
        bool restoreFailed = false;
        {
            Scope scope(&device, &restoreFailed);
            if (!scope.captured())
            {
                std::cerr << "capture failed even though reads were healthy\n";
                return 8;
            }
            if (scope.apply())
            {
                std::cerr << "a refused write was reported as a successful apply\n";
                return 9;
            }
            device.failWritesAfter = -1; // Restoration is allowed to proceed.
            if (!scope.restore())
            {
                std::cerr << "restore failed after a partial apply\n";
                return 10;
            }
        }
        if (!matches(device, original))
        {
            std::cerr << "a partial apply was not fully restored\n";
            return 11;
        }
    }

    // 4. If restoration itself fails, the caller is told through the out flag.
    {
        auto device = make_device();
        bool restoreFailed = false;
        {
            Scope scope(&device, &restoreFailed);
            (void)scope.apply();
            device.failWritesAfter = device.writes; // Refuse every restore write.
        }
        if (!restoreFailed)
        {
            std::cerr << "a failed restore was not reported\n";
            return 12;
        }
    }

    // 5. A device that cannot report its state is not touched at all.
    {
        auto device = make_device();
        device.failReads = true;
        bool restoreFailed = false;
        {
            Scope scope(&device, &restoreFailed);
            if (scope.captured() || scope.apply())
            {
                std::cerr << "a device with unreadable state was still modified\n";
                return 13;
            }
        }
        if (restoreFailed || device.writes != 0)
        {
            std::cerr << "an uncaptured scope wrote to the device\n";
            return 14;
        }
    }

    // 6. restore() is idempotent, so the destructor after an explicit restore is a
    //    no-op rather than a second round of writes.
    {
        auto device = make_device();
        bool restoreFailed = false;
        int writesAfterRestore = 0;
        {
            Scope scope(&device, &restoreFailed);
            (void)scope.apply();
            (void)scope.restore();
            writesAfterRestore = device.writes;
        }
        if (device.writes != writesAfterRestore)
        {
            std::cerr << "the destructor restored a second time\n";
            return 15;
        }
    }

    return 0;
}
