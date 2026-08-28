#include "geometry_rewrite.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
    struct Vertex
    {
        float x;
        float y;
        float z;
        float rhw;
        uint32_t color;
        float u;
        float v;
    };

    bool near(const float left, const float right)
    {
        return std::fabs(left - right) < 0.001F;
    }
}

int main()
{
    const std::array<Vertex, 4> downsample{{
        {-0.5F, -0.5F, 0.0F, 1.0F, 0x80808080U, 0.0F, 0.0F},
        {255.5F, -0.5F, 0.0F, 1.0F, 0x80808080U, 1.0F, 0.0F},
        {-0.5F, 255.5F, 0.0F, 1.0F, 0x80808080U, 0.0F, 1.0F},
        {255.5F, 255.5F, 0.0F, 1.0F, 0x80808080U, 1.0F, 1.0F},
    }};
    std::array<uint8_t, sizeof(downsample)> output{};
    if (!spectralfix::rewrite_downsample_vertices(
            downsample.data(), sizeof(Vertex), static_cast<uint32_t>(downsample.size()),
            2048, 256, output.data(), output.size()))
    {
        std::cerr << "valid downsample quad was rejected\n";
        return 1;
    }
    const auto* scaled = reinterpret_cast<const Vertex*>(output.data());
    if (!near(scaled[0].x, -0.5F) || !near(scaled[0].y, -0.5F)
        || !near(scaled[3].x, 2047.5F) || !near(scaled[3].y, 2047.5F))
    {
        std::cerr << "half-pixel downsample scaling is incorrect\n";
        return 2;
    }

    output.fill(0);
    if (!spectralfix::rewrite_downsample_vertices(
            downsample.data(), sizeof(Vertex), static_cast<uint32_t>(downsample.size()),
            1024, 256, output.data(), output.size()))
    {
        std::cerr << "valid 1024 downsample quad was rejected\n";
        return 15;
    }
    const auto* mediumScaled = reinterpret_cast<const Vertex*>(output.data());
    if (!near(mediumScaled[0].x, -0.5F) || !near(mediumScaled[0].y, -0.5F)
        || !near(mediumScaled[3].x, 1023.5F) || !near(mediumScaled[3].y, 1023.5F))
    {
        std::cerr << "1024 half-pixel downsample scaling is incorrect\n";
        return 16;
    }

    output.fill(0);
    if (!spectralfix::rewrite_downsample_vertices(
            downsample.data(), sizeof(Vertex), static_cast<uint32_t>(downsample.size()),
            4096, 256, output.data(), output.size()))
    {
        std::cerr << "valid 4096 downsample quad was rejected\n";
        return 13;
    }
    const auto* ultraScaled = reinterpret_cast<const Vertex*>(output.data());
    if (!near(ultraScaled[0].x, -0.5F) || !near(ultraScaled[0].y, -0.5F)
        || !near(ultraScaled[3].x, 4095.5F) || !near(ultraScaled[3].y, 4095.5F))
    {
        std::cerr << "4096 half-pixel downsample scaling is incorrect\n";
        return 14;
    }

    const std::array<Vertex, 4> tap{{
        {4.0F, -3.0F, 0.0F, 1.0F, 0x1EFFFFD9U, 0.0F, 0.0F},
        {3444.0F, -3.0F, 0.0F, 1.0F, 0x1EFFFFD9U, 1.0F, 0.0F},
        {4.0F, 1437.0F, 0.0F, 1.0F, 0x1EFFFFD9U, 0.0F, 1.0F},
        {3444.0F, 1437.0F, 0.0F, 1.0F, 0x1EFFFFD9U, 1.0F, 1.0F},
    }};
    output.fill(0);
    const auto spreadResult = spectralfix::rewrite_tap_vertices(
            tap.data(), sizeof(Vertex), static_cast<uint32_t>(tap.size()),
            4.0F, 1.0F, output.data(), output.size());
    if (!spreadResult.matched || !spreadResult.rewritten
        || !spreadResult.spreadAdjusted || spreadResult.opacityAdjusted)
    {
        std::cerr << "valid blur tap quad was rejected\n";
        return 3;
    }
    const auto* spread = reinterpret_cast<const Vertex*>(output.data());
    if (!near(spread[0].x, 16.0F) || !near(spread[0].y, -12.0F)
        || !near(spread[3].x, 3456.0F) || !near(spread[3].y, 1428.0F))
    {
        std::cerr << "blur tap spread scaling is incorrect\n";
        return 4;
    }

    const auto stockResult = spectralfix::rewrite_tap_vertices(
            tap.data(), sizeof(Vertex), static_cast<uint32_t>(tap.size()),
            1.0F, 1.0F, output.data(), output.size());
    if (!stockResult.matched || stockResult.rewritten)
    {
        std::cerr << "stock-width tap unexpectedly requested a rewrite\n";
        return 5;
    }

    output.fill(0);
    const auto opacityResult = spectralfix::rewrite_tap_vertices(
        tap.data(), sizeof(Vertex), static_cast<uint32_t>(tap.size()),
        1.0F, 0.5F, output.data(), output.size());
    if (!opacityResult.matched || !opacityResult.rewritten
        || opacityResult.spreadAdjusted || !opacityResult.opacityAdjusted)
    {
        std::cerr << "valid opacity-only tap rewrite was rejected\n";
        return 6;
    }
    const auto* faded = reinterpret_cast<const Vertex*>(output.data());
    if ((faded[0].color >> 24) != 15U || (faded[0].color & 0x00FFFFFFU) != 0x00FFFFD9U)
    {
        std::cerr << "tap alpha scaling changed the wrong color component\n";
        return 7;
    }

    auto composite = tap;
    for (auto& vertex : composite)
    {
        vertex.x -= 4.0F;
        vertex.y += 3.0F;
    }
    const auto compositeResult = spectralfix::rewrite_tap_vertices(
        composite.data(), sizeof(Vertex), static_cast<uint32_t>(composite.size()),
        4.0F, 0.5F, output.data(), output.size());
    if (compositeResult.matched || compositeResult.rewritten)
    {
        std::cerr << "unshifted final composite was mistaken for a blur tap\n";
        return 8;
    }

    const auto summary = spectralfix::summarize_vertices(
        composite.data(), sizeof(Vertex), static_cast<uint32_t>(composite.size()));
    if (!summary.valid || !summary.hasColor || !summary.uniformColor || !summary.hasUv
        || summary.storedVertices != 4 || summary.firstColor != 0x1EFFFFD9U
        || !near(summary.minX, 0.0F) || !near(summary.minY, 0.0F)
        || !near(summary.maxX, 3440.0F) || !near(summary.maxY, 1440.0F)
        || !near(summary.u[3], 1.0F) || !near(summary.v[3], 1.0F))
    {
        std::cerr << "draw geometry summary lost composite evidence\n";
        return 9;
    }

    if (!spectralfix::matches_center_composite_vertices(
            composite.data(), sizeof(Vertex), static_cast<uint32_t>(composite.size()),
            3440, 1440)
        || spectralfix::matches_center_composite_vertices(
            tap.data(), sizeof(Vertex), static_cast<uint32_t>(tap.size()), 3440, 1440))
    {
        std::cerr << "center-composite geometry classifier is not narrow enough\n";
        return 10;
    }

    output.fill(0);
    if (!spectralfix::rewrite_uniform_alpha(
            composite.data(), sizeof(Vertex), static_cast<uint32_t>(composite.size()),
            50.0F, output.data(), output.size()))
    {
        std::cerr << "center-composite alpha rewrite was rejected\n";
        return 11;
    }
    const auto* halfOpacity = reinterpret_cast<const Vertex*>(output.data());
    if ((halfOpacity[0].color >> 24) != 128U
        || (halfOpacity[0].color & 0x00FFFFFFU) != 0x00FFFFD9U)
    {
        std::cerr << "center-composite alpha rewrite changed RGB or rounded incorrectly\n";
        return 12;
    }
    return 0;
}
