#include "Ashita.h"

#include <Windows.h>

#include <iostream>

using Direct3DCreate8Fn = IDirect3D8*(__stdcall*)(UINT);

int main(const int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: blur_wrapper_smoke <d3d8.dll> [d3d8.dll ...]\n";
        return 2;
    }

    for (int index = 1; index < argc; ++index)
    {
        const auto module = ::LoadLibraryA(argv[index]);
        if (module == nullptr)
        {
            std::cerr << argv[index] << ": LoadLibrary failed: " << ::GetLastError() << '\n';
            return 3;
        }

        const auto create = reinterpret_cast<Direct3DCreate8Fn>(
            ::GetProcAddress(module, "Direct3DCreate8"));
        if (create == nullptr)
        {
            std::cerr << argv[index] << ": Direct3DCreate8 export is missing\n";
            ::FreeLibrary(module);
            return 4;
        }

        auto* direct3D = create(D3D_SDK_VERSION);
        if (direct3D == nullptr)
        {
            std::cerr << argv[index] << ": Direct3DCreate8 returned null\n";
            ::FreeLibrary(module);
            return 5;
        }
        direct3D->Release();
        ::FreeLibrary(module);
        std::cout << argv[index] << ": Direct3DCreate8 succeeded\n";
    }
    return 0;
}
