#include <Windows.h>

#include <cmath>
#include <iostream>

#include "Ashita.h"
#include "version.hpp"
#include "selector_validation.hpp"

using GetInterfaceVersionFn = double(__stdcall*)();
using CreatePluginFn = IPlugin*(__stdcall*)(const char*);
using DestroyPluginFn = void(__stdcall*)(void*);

int main(const int argc, char** argv)
{
    static_assert(spectralfix::selector_fields_valid(1, 0x1234, 0x2000, 0, 0x5678, 1, 2048));
    static_assert(spectralfix::selector_fields_valid(1, 0x1234, 0x2000, 0, 0x5678, 1, 1024));
    static_assert(spectralfix::selector_fields_valid(1, 0x1234, 0x2000, 0, 0x5678, 1, 4096));
    static_assert(!spectralfix::selector_fields_valid(1, 0x1234, 0x2000, 0, 0x5678, 1, 256));
    static_assert(!spectralfix::selector_fields_valid(1, 0x1234, 0x2000, 0, 0, 1, 2048));

    if (argc != 2)
    {
        std::cerr << "usage: spectralfix_export_smoke <spectralfix.dll>\n";
        return 2;
    }

    const auto module = ::LoadLibraryA(argv[1]);
    if (module == nullptr)
    {
        std::cerr << "LoadLibrary failed: " << ::GetLastError() << '\n';
        return 3;
    }

    const auto create = reinterpret_cast<CreatePluginFn>(
        ::GetProcAddress(module, "expCreatePlugin"));
    const auto destroy = reinterpret_cast<DestroyPluginFn>(
        ::GetProcAddress(module, "expDestroyPlugin"));
    const auto version = reinterpret_cast<GetInterfaceVersionFn>(
        ::GetProcAddress(module, "expGetInterfaceVersion"));

    if (create == nullptr || destroy == nullptr || version == nullptr)
    {
        std::cerr << "one or more required Ashita exports are missing\n";
        ::FreeLibrary(module);
        return 4;
    }

    const auto interfaceVersion = version();
    std::cout << "Ashita interface version: " << interfaceVersion << '\n';
    auto* plugin = create(nullptr);
    if (plugin == nullptr)
    {
        std::cerr << "expCreatePlugin returned null\n";
        ::FreeLibrary(module);
        return 5;
    }
    const auto pluginVersion = plugin->GetVersion();
    std::cout << "SpectralFix plugin version: " << pluginVersion << '\n';
    destroy(plugin);
    ::FreeLibrary(module);
    if (std::abs(interfaceVersion - ASHITA_INTERFACE_VERSION) >= 0.0001)
        return 6;
    return std::abs(pluginVersion - spectralfix::kPluginVersion) < 0.0001 ? 0 : 7;
}
