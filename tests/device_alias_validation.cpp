#include "device_alias_cache.hpp"

#include <iostream>

namespace
{
    struct FakeIdentity
    {
        unsigned long references{1};
        unsigned long AddRef() { return ++references; }
        unsigned long Release() { return --references; }
    };

    struct FakeInterface
    {
        FakeIdentity* identity{nullptr};
        unsigned long references{1};
        unsigned long AddRef() { return ++references; }
        unsigned long Release() { return --references; }
    };

    FakeIdentity* acquire_identity(FakeInterface* value)
    {
        if (value == nullptr || value->identity == nullptr)
            return nullptr;
        value->identity->AddRef();
        return value->identity;
    }
}

int main()
{
    using namespace spectralfix;

    FakeIdentity primaryIdentity{};
    FakeIdentity otherIdentity{};
    FakeInterface primary{&primaryIdentity};
    FakeInterface createAlias{&primaryIdentity};
    FakeInterface drawAlias{&primaryIdentity};
    FakeInterface sharedVtableOther{&otherIdentity};

    {
        DeviceAliasCache<FakeInterface, FakeIdentity, 3> cache;
        if (!cache.initialize(&primary, acquire_identity)
            || primaryIdentity.references != 2)
        {
            std::cerr << "primary canonical identity was not retained exactly once\n";
            return 1;
        }
        if (cache.classify(&primary, DeviceMethod::drawPrimitiveUP)
            != AliasIdentityResult::exactPointer)
        {
            std::cerr << "primary pointer did not use the exact fast path\n";
            return 2;
        }
        if (cache.classify(&createAlias, DeviceMethod::createTexture)
                != AliasIdentityResult::canonicalIdentity
            || cache.classify(&drawAlias, DeviceMethod::drawPrimitiveUP)
                != AliasIdentityResult::canonicalIdentity
            || createAlias.references != 2 || drawAlias.references != 2)
        {
            std::cerr << "separate method aliases were not retained and accepted\n";
            return 3;
        }
        if (cache.classify(&sharedVtableOther, DeviceMethod::drawPrimitiveUP)
                != AliasIdentityResult::mismatch
            || sharedVtableOther.references != 2)
        {
            std::cerr << "different canonical identity was not rejected and bounded\n";
            return 4;
        }
        FakeInterface overflow{&primaryIdentity};
        if (cache.classify(&overflow, DeviceMethod::setTexture)
                != AliasIdentityResult::cacheFull
            || overflow.references != 1 || cache.size() != cache.capacity())
        {
            std::cerr << "alias cache exceeded its fixed capacity\n";
            return 5;
        }
        if (!cache.is_cached_for(&createAlias, DeviceMethod::createTexture)
            || cache.is_cached_for(&createAlias, DeviceMethod::drawPrimitiveUP))
        {
            std::cerr << "method-specific alias use was not recorded\n";
            return 6;
        }
        if (cache.classify(&createAlias, DeviceMethod::drawPrimitiveUP)
                != AliasIdentityResult::canonicalIdentity
            || !cache.is_cached_for(&createAlias, DeviceMethod::drawPrimitiveUP)
            || createAlias.references != 2)
        {
            std::cerr << "a cached alias was not safely reused by another method\n";
            return 7;
        }
    }

    if (primaryIdentity.references != 1 || otherIdentity.references != 1
        || createAlias.references != 1 || drawAlias.references != 1
        || sharedVtableOther.references != 1)
    {
        std::cerr << "retained COM-style references were not balanced on cleanup\n";
        return 8;
    }

    return 0;
}
