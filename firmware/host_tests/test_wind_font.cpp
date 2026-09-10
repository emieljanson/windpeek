#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include "wind_font.h"
}

TEST(WindFont, MeasuresGeneratedMetricsAtEveryDashboardSize) {
    const struct {
        wind_font_family_t family;
        int size;
        const char *text;
        int width;
        int ascent;
        int descent;
    } cases[] = {
        {WIND_FONT_BERKELEY_MONO_BOLD, 15, "KNMI 11:05", 90, 15, 4},
        {WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, 15, "44 kt", 40, 15, 4},
        {WIND_FONT_INTER, 43, "Edam", 121, 42, 11},
    };
    for (const auto &item : cases) {
        const auto measured = wind_font_measure(item.family, item.size, item.text);
        EXPECT_EQ(measured.width, item.width);
        EXPECT_EQ(measured.ascent, item.ascent);
        EXPECT_EQ(measured.descent, item.descent);
    }
}

TEST(WindFont, ProvidesASmallerCondensedFaceForTheFooter) {
    const auto footer = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, 12, "08h");
    const auto status = wind_font_measure(
        WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, 15, "08h");

    EXPECT_GT(footer.width, 0);
    EXPECT_LT(footer.width, status.width);
    EXPECT_LT(footer.ascent, status.ascent);
}

TEST(WindFont, DrawsAccentsAndFallbackInsideClippedBuffer) {
    constexpr int width = 64;
    constexpr int height = 24;
    constexpr int stride = 68;
    constexpr size_t guard = 32;
    std::vector<uint8_t> storage(guard + stride * height + guard, 0xa5);
    uint8_t *luma = storage.data() + guard;
    std::fill(luma, luma + stride * height, 255);

    wind_font_draw(luma, width, height, stride, -5, 17,
                   WIND_FONT_BERKELEY_MONO_BOLD, 15, 0, "Edam ëé ☃");

    EXPECT_TRUE(std::all_of(storage.begin(), storage.begin() + guard,
                            [](uint8_t value) { return value == 0xa5; }));
    EXPECT_TRUE(std::all_of(storage.end() - guard, storage.end(),
                            [](uint8_t value) { return value == 0xa5; }));
    EXPECT_TRUE(std::any_of(luma, luma + stride * height,
                            [](uint8_t value) { return value < 255; }));
    EXPECT_EQ(wind_font_measure(WIND_FONT_BERKELEY_MONO_BOLD, 15, "☃").width,
              wind_font_measure(WIND_FONT_BERKELEY_MONO_BOLD, 15, "?").width);
}

TEST(WindFont, DrawsGlyphCoverageAsSolidBlackOrWhitePixels) {
    constexpr int width = 160;
    constexpr int height = 56;
    std::vector<uint8_t> luma(width * height, 255);

    wind_font_draw(luma.data(), width, height, width, 4, 44,
                   WIND_FONT_INTER, 43, 0, "WIND");

    EXPECT_TRUE(std::any_of(luma.begin(), luma.end(),
                            [](uint8_t value) { return value == 0; }));
    EXPECT_TRUE(std::all_of(luma.begin(), luma.end(),
                            [](uint8_t value) {
                                return value == 0 || value == 255;
                            }));
}

TEST(WindFont, FallsBackToReadableLatinLettersForUnsupportedAccents) {
    const std::pair<const char *, const char *> cases[] = {
        {"Kuźnica", "Kuznica"}, {"KUŹNICA", "KUZNICA"},
        {"Łeba ž č ą ę ś ń ż", "Leba z c a e s n z"},
        {"Âãå Øø", "Aaa Oo"},
        {"Þþ Ðð", "Tt Dd"},
    };
    const std::pair<wind_font_family_t, int> fonts[] = {
        {WIND_FONT_BERKELEY_MONO_BOLD, 15},
        {WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, 12},
        {WIND_FONT_BERKELEY_MONO_BOLD_CONDENSED, 15},
        {WIND_FONT_INTER, 43},
    };
    for (const auto &[family, size] : fonts) {
        for (const auto &item : cases) {
            SCOPED_TRACE(item.first);
            EXPECT_EQ(wind_font_measure(family, size, item.first).width,
                      wind_font_measure(family, size, item.second).width);
            for (bool antialiased : {false, true}) {
                std::vector<uint8_t> actual(800 * 60, 255), expected(actual);
                const auto draw = antialiased ? wind_font_draw_antialiased
                                              : wind_font_draw;
                draw(actual.data(), 800, 60, 800, 0, 45, family, size, 0, item.first);
                draw(expected.data(), 800, 60, 800, 0, 45, family, size, 0, item.second);
                EXPECT_TRUE(actual == expected);
            }
        }
    }
}

TEST(WindFont, UsesBaselineMetricsForCrossFamilyPlacement) {
    const auto spot = wind_font_measure(WIND_FONT_INTER, 43, "EDAM");
    const auto status = wind_font_measure(WIND_FONT_BERKELEY_MONO_BOLD, 15,
                                          "KNMI 11:05");
    const int baseline = 78;
    EXPECT_EQ(baseline - spot.ascent, 36);
    EXPECT_EQ(baseline - status.ascent, 63);
    EXPECT_EQ(baseline + spot.descent, 89);
}

TEST(WindFont, UnsupportedSizeIsAStableNoOp) {
    uint8_t pixel = 123;
    const auto measured = wind_font_measure(WIND_FONT_INTER, 17, "Edam");
    EXPECT_EQ(measured.width, 0);
    EXPECT_EQ(measured.ascent, 0);
    EXPECT_EQ(measured.descent, 0);
    wind_font_draw(&pixel, 1, 1, 1, 0, 0, WIND_FONT_INTER, 17, 0, "x");
    EXPECT_EQ(pixel, 123);
}
