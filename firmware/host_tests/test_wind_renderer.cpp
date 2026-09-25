#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "wind_renderer.h"
#include "wind_renderer_internal.h"
#include "wind_renderer_fixture.h"
}

namespace {

using Frame = std::vector<uint8_t>;

wind_renderer_dashboard_t Dashboard(wind_renderer_state_t state = WIND_RENDERER_FRESH) {
    static const char *days[] = {"MON", "TUE", "WED", "THU", "FRI"};
    static const char *dates[] = {"24 AUG", "25 AUG", "26 AUG", "27 AUG", "28 AUG"};
    static const char *times[] = {"08", "11", "14", "17", "20"};
    wind_renderer_dashboard_t result{};
    result.spot_name = "Edam";
    result.provider = "HARMONIE SEAMLESS";
    result.updated_time = "24 AUG 21:00";
    result.state = state;
    result.age_hours = state == WIND_RENDERER_STALE ? 25 : 1;
    result.battery_percent = 74;
    result.display_mode = WIND_RENDERER_MODE_SOLID;
    result.threshold_kt = WIND_RENDERER_DEFAULT_THRESHOLD_KT;
    result.show_weather = 1;
    result.show_dedicated_footer = 1;
    result.use_24_hour = 1;
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        result.days[day].day = days[day];
        result.days[day].date = dates[day];
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            auto &slot = result.days[day].samples[sample];
            slot.time = times[sample];
            slot.sustained_kt = 7 + day * 2 + sample * 3;
            slot.gust_kt = slot.sustained_kt + 5;
            slot.destination_degrees = day * 55 + sample * 27;
            slot.available = 1;
            slot.weather =
                static_cast<wind_renderer_weather_t>(1 + (day * 5 + sample) % 8);
            slot.temperature_tenths_c = 120 + day * 5 + sample;
            slot.temperature_available = 1;
        }
    }
    return result;
}

Frame Render(const wind_renderer_dashboard_t &dashboard,
             wind_renderer_stats_t *stats = nullptr) {
    Frame frame(WIND_RENDERER_PALETTE_BYTES, 0x7f);
    EXPECT_EQ(wind_renderer_render(&dashboard, frame.data(), frame.size(), stats), 0);
    return frame;
}

Frame RenderPreview(const wind_renderer_dashboard_t &dashboard,
                    wind_renderer_stats_t *stats = nullptr) {
    Frame frame(WIND_RENDERER_RGBA_BYTES, 0x7f);
    EXPECT_EQ(wind_renderer_render_preview_rgba(&dashboard, frame.data(), frame.size(),
                                                stats),
              0);
    return frame;
}

int CountBlack(const Frame &frame, int left, int top, int right, int bottom) {
    int count = 0;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            count += frame[y * WIND_RENDERER_WIDTH + x] == 0;
    return count;
}

int CountColor(const Frame &frame, int left, int top, int right, int bottom,
               uint8_t color) {
    int count = 0;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            count += frame[y * WIND_RENDERER_WIDTH + x] == color;
    return count;
}

std::pair<double, double> BlackCentroid(const Frame &frame, int left, int top,
                                        int right, int bottom) {
    double x_total = 0;
    double y_total = 0;
    int count = 0;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (frame[y * WIND_RENDERER_WIDTH + x] != 0) continue;
            x_total += x;
            y_total += y;
            ++count;
        }
    }
    EXPECT_GT(count, 0);
    return {x_total / count, y_total / count};
}

struct FooterDiff {
    bool above_changed;
    bool footer_changed;
};

FooterDiff CompareFooterRegions(const Frame &first, const Frame &second) {
    const auto footer = first.begin() + 450 * WIND_RENDERER_WIDTH;
    const auto second_footer = second.begin() + 450 * WIND_RENDERER_WIDTH;
    return {
        !std::equal(first.begin(), footer, second.begin()),
        !std::equal(footer, first.end(), second_footer),
    };
}

std::string FixturePath(const char *name) {
    return (std::filesystem::path(__FILE__).parent_path() / "fixtures" /
            (std::string("dashboard_") + name + ".pbm"))
        .string();
}

std::string Gray4FixturePath(const char *name) {
    return (std::filesystem::path(__FILE__).parent_path() / "fixtures" /
            (std::string("e1001-gray4-") + name + ".bin"))
        .string();
}

void ExpectGray4Golden(const char *name, const Frame &actual) {
    const std::string path = Gray4FixturePath(name);
    if (std::getenv("WIND_UPDATE_EPAPER_GOLDENS")) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.good()) << path;
        output.write(reinterpret_cast<const char *>(actual.data()),
                     static_cast<std::streamsize>(actual.size()));
        ASSERT_TRUE(output.good()) << path;
    }
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.good()) << path;
    const Frame expected(std::istreambuf_iterator<char>(input), {});
    EXPECT_EQ(expected, actual);
}

void WritePbm(const std::string &path, const Frame &frame) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << path;
    output << "P4\n" << WIND_RENDERER_WIDTH << " " << WIND_RENDERER_HEIGHT << "\n";
    for (int y = 0; y < WIND_RENDERER_HEIGHT; ++y) {
        for (int x = 0; x < WIND_RENDERER_WIDTH; x += 8) {
            uint8_t byte = 0;
            for (int bit = 0; bit < 8; ++bit)
                if (frame[y * WIND_RENDERER_WIDTH + x + bit] == 0)
                    byte |= static_cast<uint8_t>(0x80u >> bit);
            output.put(static_cast<char>(byte));
        }
    }
    ASSERT_TRUE(output.good());
}

Frame ReadPbm(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << path;
    std::string magic;
    int width = 0;
    int height = 0;
    input >> magic >> width >> height;
    input.get();
    EXPECT_EQ(magic, "P4");
    EXPECT_EQ(width, WIND_RENDERER_WIDTH);
    EXPECT_EQ(height, WIND_RENDERER_HEIGHT);
    Frame frame(WIND_RENDERER_PALETTE_BYTES, 1);
    for (int y = 0; y < WIND_RENDERER_HEIGHT && input.good(); ++y) {
        for (int x = 0; x < WIND_RENDERER_WIDTH; x += 8) {
            const int byte = input.get();
            if (byte < 0) return {};
            for (int bit = 0; bit < 8; ++bit)
                frame[y * WIND_RENDERER_WIDTH + x + bit] =
                    (byte & (0x80 >> bit)) ? 0 : 1;
        }
    }
    return frame;
}

Frame ReadPaletteFixture(const char *name) {
    const auto path = std::filesystem::path(WIND_RENDERER_SHARED_FIXTURE_DIR) /
                      (std::string(name) + ".bin");
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << path;
    Frame frame(std::istreambuf_iterator<char>(input), {});
    EXPECT_EQ(frame.size(), WIND_RENDERER_PALETTE_BYTES) << path;
    return frame;
}

void ExpectGolden(const char *name, const wind_renderer_dashboard_t &dashboard) {
    const Frame actual = Render(dashboard);
    const std::string path = FixturePath(name);
    if (std::getenv("WIND_UPDATE_GOLDENS")) WritePbm(path, actual);
    const Frame expected = ReadPbm(path);
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_EQ(expected, actual);
}

} // namespace

TEST(WindRenderer, ProducesOneDeterministicMonochromeFrameWithoutClipping) {
    const auto dashboard = Dashboard();
    wind_renderer_stats_t first_stats{};
    const Frame first = Render(dashboard, &first_stats);
    const Frame second = Render(dashboard);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.size(), 800u * 480u);
    EXPECT_TRUE(std::all_of(first.begin(), first.end(),
                            [](uint8_t value) { return value == 0 || value == 1; }));
    EXPECT_EQ(first_stats.dither_passes, 1);
    EXPECT_EQ(first_stats.clipped_primitives, 0);
    EXPECT_EQ(first_stats.status_right, 770);
    EXPECT_EQ(first[12 * 800 + 12], 0);
    EXPECT_EQ(first[80 * 800 + 400], 0);
    EXPECT_EQ(first[449 * 800 + 787], 0);
}

TEST(WindRenderer, Gray4UsesFourIntentionalLevelsWithoutDithering) {
    auto dashboard = Dashboard();
    dashboard.display_mode = WIND_RENDERER_MODE_THRESHOLD;
    wind_renderer_stats_t stats{};
    Frame frame(WIND_RENDERER_PALETTE_BYTES, 0x7f);
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
                                               WIND_RENDERER_DISPLAY_E1001_GRAY4,
                                               frame.data(), frame.size(), &stats),
              0);
    EXPECT_EQ(stats.dither_passes, 0);
    for (uint8_t level = 0; level < 4; ++level) {
        EXPECT_NE(std::find(frame.begin(), frame.end(), level), frame.end())
            << "missing Gray4 level " << static_cast<int>(level);
    }
    EXPECT_TRUE(std::all_of(frame.begin(), frame.end(),
                            [](uint8_t value) { return value <= 3; }));
    ExpectGray4Golden("threshold-17", frame);
}

TEST(WindRenderer, MutedFooterStatusIsModelSpecific) {
    auto dashboard = Dashboard();
    Frame e1001(WIND_RENDERER_PALETTE_BYTES);
    Frame e1003(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
                  WIND_RENDERER_DISPLAY_E1001_GRAY4, e1001.data(), e1001.size(),
                  nullptr), 0);
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
                  WIND_RENDERER_DISPLAY_E1003_GC16, e1003.data(), e1003.size(),
                  nullptr), 0);
    const Frame e1002 = Render(dashboard);

    EXPECT_GT(CountColor(e1001, 30, 450, 770, 479, 1), 0);
    EXPECT_EQ(CountColor(e1001, 30, 450, 770, 479, 0), 0);
    EXPECT_GT(CountColor(e1002, 30, 450, 770, 479, 0), 0);
    EXPECT_GT(std::count(e1003.begin() + 1334 * WIND_RENDERER_E1003_WIDTH,
                         e1003.end(), 0), 0);
}

TEST(WindRenderer, WarningRemainsDistinctAndRenderSignatureCannotCrossModels) {
    auto dashboard = Dashboard();
    dashboard.display_mode = WIND_RENDERER_MODE_THRESHOLD;
    const Frame e1002 = Render(dashboard);
    Frame e1001(WIND_RENDERER_PALETTE_BYTES, 0x7f);
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
                                               WIND_RENDERER_DISPLAY_E1001_GRAY4,
                                               e1001.data(), e1001.size(), nullptr),
              0);

    auto warning = std::find(e1002.begin(), e1002.end(), 3);
    ASSERT_NE(warning, e1002.end());
    const size_t warning_index = static_cast<size_t>(warning - e1002.begin());
    EXPECT_EQ(e1001[warning_index], 1);
    EXPECT_NE(e1001[warning_index], e1001[0]);

    constexpr uint64_t base = UINT64_C(0x57494E440000000D);
    EXPECT_NE(
        wind_renderer_display_signature(base, WIND_RENDERER_DISPLAY_E1001_GRAY4),
        wind_renderer_display_signature(base, WIND_RENDERER_DISPLAY_E1002_SPECTRA6));
}

TEST(WindRenderer, RendersE1003WithSixteenLevelGrayscale) {
    auto dashboard = Dashboard();
    dashboard.display_mode = WIND_RENDERER_MODE_THRESHOLD;
    wind_renderer_stats_t stats{};
    Frame frame(WIND_RENDERER_E1003_COMPOSITION_BYTES, 0x7f);

    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
                                               WIND_RENDERER_DISPLAY_E1003_GC16,
                                               frame.data(), frame.size(), &stats),
              0);
    EXPECT_EQ(stats.dither_passes, 0);
    EXPECT_TRUE(std::all_of(frame.begin(), frame.end(),
                            [](uint8_t value) { return value <= 15; }));
    EXPECT_NE(std::find_if(frame.begin(), frame.end(),
                           [](uint8_t value) { return value > 3 && value < 15; }),
              frame.end());
}

TEST(WindRenderer, ProjectsNativeFourByThreeE1003Composition) {
    int width = 0;
    int height = 0;
    ASSERT_EQ(wind_renderer_display_dimensions(WIND_RENDERER_DISPLAY_E1003_GC16, &width,
                                               &height),
              0);
    EXPECT_EQ(width, WIND_RENDERER_E1003_WIDTH);
    EXPECT_EQ(height, WIND_RENDERER_E1003_HEIGHT);

    Frame source(WIND_RENDERER_E1003_COMPOSITION_BYTES, 15);
    std::fill(source.begin(), source.begin() + WIND_RENDERER_E1003_WIDTH, 0);
    source[(WIND_RENDERER_E1003_HEIGHT - 1) * WIND_RENDERER_E1003_WIDTH] = 4;
    std::vector<uint8_t> row(static_cast<size_t>(width), 0x7f);

    ASSERT_EQ(wind_renderer_project_display_row(WIND_RENDERER_DISPLAY_E1003_GC16,
                                                source.data(), source.size(), 0,
                                                row.data(), row.size()),
              0);
    EXPECT_TRUE(
        std::all_of(row.begin(), row.end(), [](uint8_t value) { return value == 0; }));

    ASSERT_EQ(wind_renderer_project_display_row(
                  WIND_RENDERER_DISPLAY_E1003_GC16, source.data(), source.size(),
                  WIND_RENDERER_E1003_HEIGHT - 1, row.data(), row.size()),
              0);
    EXPECT_EQ(row.front(), 4);
    EXPECT_EQ(row.back(), 15);
}

TEST(WindRenderer, E1003StraightGeometryKeepsNativePixelWidths) {
    Frame source(WIND_RENDERER_E1003_COMPOSITION_BYTES, 15);
    constexpr int day_line = 167;
    constexpr int bar_left = 185;
    for (int y = 0; y < WIND_RENDERER_E1003_HEIGHT; ++y)
        std::fill_n(source.begin() + y * WIND_RENDERER_E1003_WIDTH + day_line, 2, 0);
    for (int y = 200; y <= 210; ++y)
        std::fill_n(source.begin() + y * WIND_RENDERER_E1003_WIDTH + bar_left, 38, 0);
    for (int y : {300, 301})
        std::fill(source.begin() + y * WIND_RENDERER_E1003_WIDTH + 12,
                  source.begin() + y * WIND_RENDERER_E1003_WIDTH + 788, 0);

    Frame row(WIND_RENDERER_E1003_WIDTH, 15);
    auto project = [&](int y) {
        ASSERT_EQ(wind_renderer_project_display_row(
                      WIND_RENDERER_DISPLAY_E1003_GC16, source.data(),
                      source.size(), y, row.data(), row.size()), 0);
    };
    auto black_run = [&](int center) {
        int left = center;
        int right = center;
        while (left > 0 && row[left - 1] == 0) --left;
        while (right < WIND_RENDERER_E1003_WIDTH && row[right] == 0) ++right;
        return right - left;
    };

    project(205);
    EXPECT_EQ(black_run(day_line), 2);
    EXPECT_EQ(black_run(bar_left), 38);

    for (int y : {300, 301}) {
        project(y);
        EXPECT_EQ(row[700], 0);
    }
    project(302);
    EXPECT_EQ(row[700], 15);

    source[20 * WIND_RENDERER_E1003_WIDTH + day_line] = 0;
    for (int y = 150; y <= 160; ++y)
        std::fill_n(source.begin() + y * WIND_RENDERER_E1003_WIDTH + bar_left, 38, 0);
    project(155);
    EXPECT_EQ(black_run(day_line), 2);
    EXPECT_EQ(black_run(bar_left), 38);

    std::fill(source.begin(), source.end(), 15); // focused 13-sample day
    for (int y = 200; y <= 210; ++y)
        std::fill_n(source.begin() + y * WIND_RENDERER_E1003_WIDTH + 59, 38, 0);
    project(205);
    EXPECT_EQ(black_run(59), 38);

    std::fill(source.begin(), source.end(), 15);
    for (int y : {300, 301})
        std::fill(source.begin() + y * WIND_RENDERER_E1003_WIDTH + 30,
                  source.begin() + y * WIND_RENDERER_E1003_WIDTH + 771, 5);
    for (int y : {300, 301}) {
        project(y);
        EXPECT_EQ(row[700], 5);
    }
    project(302);
    EXPECT_EQ(row[700], 15);
}

TEST(WindRenderer, NativeE1003DashboardKeepsRulesBarsAndThresholdExact) {
    wind_renderer_input_v2_t input{};
    wind_renderer_dashboard_t dashboard{};
    ASSERT_EQ(wind_renderer_fixture_build(0, &input), 0);
    ASSERT_EQ(wind_renderer_input_v2_to_dashboard(&input, &dashboard), 0);
    Frame pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    wind_renderer_stats_t stats{};
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
        WIND_RENDERER_DISPLAY_E1003_GC16, pixels.data(), pixels.size(), &stats), 0);
    EXPECT_EQ(stats.clipped_primitives, 0u);
    const auto pixel = [&](int x, int y) {
        return pixels[y * WIND_RENDERER_E1003_WIDTH + x];
    };
    for (int y : {240, 480, 600, 1000, 1100}) {
        EXPECT_EQ(pixel(390, y), 15);
        EXPECT_EQ(pixel(391, y), 0);
        EXPECT_EQ(pixel(392, y), 0);
        EXPECT_EQ(pixel(393, y), 15);
    }
    for (int x = 554; x < 592; ++x) EXPECT_EQ(pixel(x, 1100), 0);
    EXPECT_EQ(pixel(553, 1100), 15);
    EXPECT_EQ(pixel(592, 1100), 15);
    for (int x : {400, 550, 750, 850}) {
        EXPECT_EQ(pixel(x, 1125), 15);
        EXPECT_EQ(pixel(x, 1126), 0);
        EXPECT_EQ(pixel(x, 1127), 0);
        EXPECT_EQ(pixel(x, 1128), 15);
    }
    EXPECT_EQ(pixel(500, 1126), 0); // The crossing is black as well.
}

TEST(WindRenderer, UsesTheSameCompositionForACleanUnditheredPreview) {
    auto dashboard = Dashboard();
    dashboard.display_mode = WIND_RENDERER_MODE_SOLID;
    const Frame palette_before = Render(dashboard);
    wind_renderer_stats_t stats{};
    const Frame preview = RenderPreview(dashboard, &stats);
    const Frame palette_after = Render(dashboard);

    EXPECT_EQ(palette_before, palette_after);
    EXPECT_EQ(preview.size(), static_cast<size_t>(WIND_RENDERER_RGBA_BYTES));
    EXPECT_EQ(stats.dither_passes, 0);
    EXPECT_EQ(stats.clipped_primitives, 0);
    bool all_alpha_opaque = true;
    for (size_t offset = 0; offset < preview.size(); offset += 4) {
        all_alpha_opaque = all_alpha_opaque && preview[offset + 3] == 255;
    }
    EXPECT_TRUE(all_alpha_opaque);

    bool title_has_antialiased_edge = false;
    for (int y = 20; y <= 70; ++y) {
        for (int x = 30; x <= 180; ++x) {
            const size_t offset = static_cast<size_t>(y * WIND_RENDERER_WIDTH + x) * 4;
            const uint8_t luma = preview[offset];
            if (luma > 0 && luma < 255 && preview[offset + 1] == luma &&
                preview[offset + 2] == luma) {
                title_has_antialiased_edge = true;
            }
        }
    }
    EXPECT_TRUE(title_has_antialiased_edge);

    dashboard.display_mode = WIND_RENDERER_MODE_THRESHOLD;
    const Frame threshold_palette = Render(dashboard);
    const Frame threshold = RenderPreview(dashboard);
    bool has_red = false;
    bool red_geometry_matches = true;
    for (size_t offset = 0; offset < threshold.size(); offset += 4) {
        const bool preview_red =
            threshold[offset] == 255 && threshold[offset + 1] == 0 &&
            threshold[offset + 2] == 0 && threshold[offset + 3] == 255;
        const bool palette_red = threshold_palette[offset / 4] == 3;
        has_red = has_red || preview_red;
        red_geometry_matches = red_geometry_matches && preview_red == palette_red;
    }
    EXPECT_TRUE(has_red);
    EXPECT_TRUE(red_geometry_matches);
}

TEST(WindRenderer, ExpandsNativePaletteToPhysicalBlackWhiteAndRedRgb) {
    const uint8_t palette[] = {0, 1, 3, 0};
    std::array<uint8_t, 12> rgb{};
    ASSERT_EQ(wind_renderer_palette_row_to_rgb(palette, std::size(palette), rgb.data(),
                                               rgb.size()),
              0);
    const std::array<uint8_t, 12> expected = {
        0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 0,
    };
    EXPECT_EQ(rgb, expected);

    const uint8_t invalid[] = {2};
    EXPECT_NE(wind_renderer_palette_row_to_rgb(invalid, std::size(invalid), rgb.data(),
                                               rgb.size()),
              0);
}

TEST(WindRenderer, UsesFixedScaleAndPinsOverflowValueAboveTheBar) {
    auto dashboard = Dashboard();
    const int speeds[] = {0, 1, 39, 40, 44};
    for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
        dashboard.days[0].samples[sample].sustained_kt = speeds[sample];
        dashboard.days[0].samples[sample].gust_kt = speeds[sample];
    }
    const Frame frame = Render(dashboard);
    const auto black = [&frame](int x, int y) { return frame[y * 800 + x] == 0; };
    constexpr int baseline = 406;
    EXPECT_FALSE(black(38, baseline - 1));
    EXPECT_TRUE(black(64, baseline - 6));
    EXPECT_FALSE(black(64, baseline - 7));
    EXPECT_TRUE(black(90, baseline - 240));
    EXPECT_FALSE(black(90, baseline - 241));
    EXPECT_TRUE(black(116, baseline - 243));
    int overflow_label_pixels = 0;
    for (int y = baseline - 251; y <= baseline - 232; ++y)
        for (int x = 132; x <= 152; ++x)
            if (black(x, y)) ++overflow_label_pixels;
    EXPECT_GT(overflow_label_pixels, 0);
}

TEST(WindRenderer, PlacesFiveTimeSlotsAcrossEachDayOnOneBaseline) {
    auto dashboard = Dashboard();
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            dashboard.days[day].samples[sample].sustained_kt = 10;
            dashboard.days[day].samples[sample].gust_kt = 12;
        }

    const Frame frame = Render(dashboard);
    constexpr int baseline = 389;
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day) {
        std::array<int, WIND_RENDERER_SAMPLES_PER_DAY> centers{};
        std::array<int, WIND_RENDERER_SAMPLES_PER_DAY> baselines{};
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            centers[sample] = 12 + day * 155 + 26 + sample * 26;
            baselines[sample] = baseline;
            EXPECT_EQ(frame[(baseline - 1) * 800 + centers[sample]], 0)
                << "day " << day << ", sample " << sample;
        }
        for (int sample = 1; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            EXPECT_LT(centers[sample - 1], centers[sample]);
            EXPECT_NE(centers[sample - 1], centers[sample]);
            EXPECT_EQ(baselines[sample - 1], baselines[sample]);
        }
    }
}

TEST(WindRenderer, KeepsDirectionIconOpticallyCenteredAtEveryAngle) {
    auto dashboard = Dashboard();
    for (auto &day : dashboard.days)
        for (auto &sample : day.samples)
            sample.available = 0;

    constexpr int center_x = 38;
    constexpr int center_y = 133;
    auto &sample = dashboard.days[0].samples[0];
    sample.available = 1;
    sample.sustained_kt = 0;
    sample.gust_kt = 0;

    for (int degrees = 0; degrees < 360; degrees += 45) {
        sample.destination_degrees = degrees;
        const Frame frame = Render(dashboard);
        const auto [centroid_x, centroid_y] = BlackCentroid(
            frame, center_x - 9, center_y - 9, center_x + 9, center_y + 9);
        EXPECT_NEAR(centroid_x, center_x, 0.5) << degrees << " degrees";
        EXPECT_NEAR(centroid_y, center_y, 0.5) << degrees << " degrees";
    }
}

TEST(WindRenderer, UsesTheBottomBandAsATimeAxisAndStatusArea) {
    auto dashboard = Dashboard();
    dashboard.use_24_hour = 1;
    const Frame original = Render(dashboard);

    auto first_day_time_changed = dashboard;
    first_day_time_changed.days[0].samples[0].time = "09";
    const Frame changed_time = Render(first_day_time_changed);
    const FooterDiff time_diff = CompareFooterRegions(original, changed_time);
    EXPECT_TRUE(time_diff.footer_changed);
    EXPECT_FALSE(time_diff.above_changed);

    auto hidden_time_changed = dashboard;
    hidden_time_changed.days[4].samples[0].time = "09";
    EXPECT_EQ(Render(hidden_time_changed), original);

    auto third_day_time_changed = dashboard;
    third_day_time_changed.days[2].samples[0].time = "09";
    EXPECT_NE(Render(third_day_time_changed), original);

    auto fourth_day_time_changed = dashboard;
    fourth_day_time_changed.days[3].samples[0].time = "09";
    EXPECT_NE(Render(fourth_day_time_changed), original);

    auto long_status = dashboard;
    long_status.provider = "A VERY LONG FORECAST MODEL NAME";
    const Frame long_status_frame = Render(long_status);
    EXPECT_EQ(long_status_frame, original);
    auto long_status_hidden_time_changed = long_status;
    long_status_hidden_time_changed.days[2].samples[0].time = "09";
    EXPECT_NE(Render(long_status_hidden_time_changed), long_status_frame);

    auto status_changed = dashboard;
    status_changed.provider = "BEST MATCH";
    status_changed.updated_time = "11:05";
    const Frame changed_status = Render(status_changed);
    const FooterDiff status_diff = CompareFooterRegions(original, changed_status);
    EXPECT_TRUE(status_diff.footer_changed);
    EXPECT_FALSE(status_diff.above_changed);

    for (int day = 1; day < WIND_RENDERER_DAY_COUNT; ++day) {
        const int divider_x = 12 + day * 155;
        EXPECT_EQ(original[450 * WIND_RENDERER_WIDTH + divider_x], 1)
            << "footer divider for day " << day;
    }
}

TEST(WindRenderer, HidesAllHoursWhenTheFooterIsHidden) {
    auto dashboard = Dashboard();
    dashboard.show_dedicated_footer = 0;
    for (bool ordered : {false, true}) {
        dashboard.custom_modules = ordered;
        dashboard.ordered_modules = ordered;
        dashboard.wind_size = 2;
        dashboard.swell_size = ordered ? 2 : 0;
        for (int index = 0; index < 5; ++index) dashboard.module_order[index] = index;
        for (bool twelve_hour : {false, true}) {
            dashboard.use_24_hour = !twelve_hour;
            const Frame original = Render(dashboard);
            dashboard.days[0].samples[0].time = twelve_hour ? "8PM" : "20";
            EXPECT_EQ(Render(dashboard), original);
            dashboard.days[0].samples[0].time = "08";
        }
    }
}

TEST(WindRenderer, ShowsOnlyBatteryAndUpdateBelowItWhenLegendIsHidden) {
    auto dashboard = Dashboard();
    dashboard.show_dedicated_footer = 0;
    const Frame original = Render(dashboard);

    auto provider_changed = dashboard;
    provider_changed.provider = "A COMPLETELY DIFFERENT MODEL";
    EXPECT_EQ(Render(provider_changed), original);

    auto update_changed = dashboard;
    update_changed.updated_time = "30 AUG 22:00";
    EXPECT_NE(Render(update_changed), original);

    EXPECT_GT(CountColor(original, 746, 30, 771, 42, 0), 0);
}

TEST(WindRenderer, SupportsThresholdAndSolidDisplayModes) {
    auto dashboard = Dashboard();
    for (int day = 0; day < WIND_RENDERER_DAY_COUNT; ++day)
        for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
            dashboard.days[day].samples[sample].sustained_kt = 10;
            dashboard.days[day].samples[sample].gust_kt = 14;
        }

    dashboard.display_mode = WIND_RENDERER_MODE_THRESHOLD;
    const Frame threshold = Render(dashboard);
    dashboard.display_mode = WIND_RENDERER_MODE_SOLID;
    const Frame solid = Render(dashboard);

    const int threshold_bottom = CountBlack(threshold, 13, 390, 786, 433);
    const int solid_bottom = CountBlack(solid, 13, 390, 786, 433);
    EXPECT_EQ(threshold_bottom, solid_bottom);
    EXPECT_GT(CountColor(threshold, 28, 300, 771, 304, 3), 0);
    EXPECT_EQ(CountColor(solid, 28, 300, 771, 304, 3), 0);
}

TEST(WindRenderer, UsesTheConfiguredThresholdAcrossItsSupportedRange) {
    auto dashboard = Dashboard();
    dashboard.display_mode = WIND_RENDERER_MODE_THRESHOLD;

    dashboard.threshold_kt = WIND_RENDERER_MIN_THRESHOLD_KT;
    const Frame minimum = Render(dashboard);
    dashboard.threshold_kt = WIND_RENDERER_DEFAULT_THRESHOLD_KT;
    const Frame default_threshold = Render(dashboard);
    const Frame repeated_default = Render(dashboard);
    dashboard.threshold_kt = WIND_RENDERER_MAX_THRESHOLD_KT;
    const Frame maximum = Render(dashboard);

    const auto threshold_y = [](int knots) { return 406 - knots * 247 / 40; };
    EXPECT_GT(CountColor(minimum, 36, threshold_y(WIND_RENDERER_MIN_THRESHOLD_KT), 762,
                         threshold_y(WIND_RENDERER_MIN_THRESHOLD_KT), 3),
              0);
    EXPECT_GT(CountColor(default_threshold, 36,
                         threshold_y(WIND_RENDERER_DEFAULT_THRESHOLD_KT), 762,
                         threshold_y(WIND_RENDERER_DEFAULT_THRESHOLD_KT), 3),
              0);
    EXPECT_GT(CountColor(maximum, 36, threshold_y(WIND_RENDERER_MAX_THRESHOLD_KT), 762,
                         threshold_y(WIND_RENDERER_MAX_THRESHOLD_KT), 3),
              0);
    EXPECT_NE(minimum, default_threshold);
    EXPECT_NE(default_threshold, maximum);
    EXPECT_EQ(default_threshold, repeated_default);
}

TEST(WindRenderer, RejectsInvalidConfigurationAndIncompleteAvailableSamples) {
    Frame frame(WIND_RENDERER_PALETTE_BYTES);
    auto dashboard = Dashboard();

    dashboard.display_mode = WIND_RENDERER_MODE_COUNT;
    EXPECT_NE(wind_renderer_render(&dashboard, frame.data(), frame.size(), nullptr), 0);

    dashboard = Dashboard();
    dashboard.threshold_kt = WIND_RENDERER_MIN_THRESHOLD_KT - 1;
    EXPECT_NE(wind_renderer_render(&dashboard, frame.data(), frame.size(), nullptr), 0);
    dashboard.threshold_kt = WIND_RENDERER_MAX_THRESHOLD_KT + 1;
    EXPECT_NE(wind_renderer_render(&dashboard, frame.data(), frame.size(), nullptr), 0);

    dashboard = Dashboard();
    dashboard.days[0].samples[0].time = nullptr;
    EXPECT_NE(wind_renderer_render(&dashboard, frame.data(), frame.size(), nullptr), 0);

    dashboard = Dashboard();
    const std::string oversized(WIND_RENDERER_SPOT_NAME_CAPACITY, 'W');
    dashboard.spot_name = oversized.c_str();
    EXPECT_NE(wind_renderer_render(&dashboard, frame.data(), frame.size(), nullptr), 0);
}

TEST(WindRenderer, ConvertsVersionedBoundedInputToTheCanonicalDashboard) {
    wind_renderer_input_v2_t input{};
    wind_renderer_input_v2_init(&input);
    EXPECT_EQ(wind_renderer_contract_version(), WIND_RENDERER_CONTRACT_VERSION);
    EXPECT_EQ(input.version, WIND_RENDERER_CONTRACT_VERSION);
    EXPECT_EQ(input.threshold_kt, WIND_RENDERER_DEFAULT_THRESHOLD_KT);

    EXPECT_EQ(
        wind_renderer_input_v2_set_metadata(&input, "Brouwersdam", "KNMI", "11:05"), 0);
    EXPECT_EQ(wind_renderer_input_v2_set_status(&input, WIND_RENDERER_FRESH, 0, 1, 74,
                                                WIND_RENDERER_MODE_THRESHOLD, 23),
              0);
    EXPECT_EQ(wind_renderer_input_v2_set_display_rows(&input, 1, 1, 1, 1), 0);
    EXPECT_EQ(wind_renderer_input_v2_set_preferences(&input, 1, 1, 1), 0);
    EXPECT_EQ(wind_renderer_input_v2_set_day(&input, 0, "TODAY", "24 AUG"), 0);
    EXPECT_EQ(wind_renderer_input_v2_set_sample(&input, 0, 0, "08", 18, 24, 245, 1,
                                                WIND_RENDERER_WEATHER_CLEAR_DAY, -24,
                                                1),
              0);
    EXPECT_EQ(wind_renderer_input_v2_set_tide_sample(&input, 0, 0, 8, -350, 1), 0);
    EXPECT_EQ(wind_renderer_input_v2_set_tide_sample(&input, 1, 0, 9, -300, 1), 0);
    EXPECT_EQ(wind_renderer_input_v2_set_tide_extremum(&input, 0, 0, 8, 15, -350, 0, 1),
              0);

    wind_renderer_dashboard_t dashboard{};
    EXPECT_EQ(wind_renderer_input_v2_to_dashboard(&input, &dashboard), 0);
    EXPECT_STREQ(dashboard.spot_name, "Brouwersdam");
    EXPECT_EQ(dashboard.threshold_kt, 23);
    EXPECT_EQ(dashboard.show_dedicated_footer, 1);
    EXPECT_EQ(dashboard.use_24_hour, 1);
    EXPECT_EQ(dashboard.temperature_fahrenheit, 1);
    EXPECT_EQ(dashboard.days[0].samples[0].sustained_kt, 18);
    EXPECT_EQ(dashboard.days[0].samples[0].temperature_tenths_c, -24);
    EXPECT_EQ(dashboard.tide_samples[0].sea_level_mm, -350);
    EXPECT_EQ(dashboard.tide_extrema[0].local_minute, 15);

    const std::string oversized(WIND_RENDERER_SPOT_NAME_CAPACITY, 'W');
    EXPECT_NE(
        wind_renderer_input_v2_set_metadata(&input, oversized.c_str(), "KNMI", "11:05"),
        0);
    EXPECT_NE(wind_renderer_input_v2_set_status(&input, WIND_RENDERER_FRESH, 0, 1, 74,
                                                WIND_RENDERER_MODE_COUNT, 23),
              0);
    EXPECT_NE(wind_renderer_input_v2_set_sample(&input, 0, 0, "", 18, 24, 245, 1,
                                                WIND_RENDERER_WEATHER_CLEAR_DAY, 120,
                                                1),
              0);

    input.version = WIND_RENDERER_CONTRACT_VERSION + 1;
    EXPECT_NE(wind_renderer_input_v2_to_dashboard(&input, &dashboard), 0);
}

TEST(WindRenderer, MatchesEveryFullPaletteCrossRuntimeFixture) {
    for (std::size_t fixture_index = 0; fixture_index < WIND_RENDERER_FIXTURE_COUNT;
         ++fixture_index) {
        wind_renderer_input_v2_t input{};
        ASSERT_EQ(wind_renderer_fixture_build(fixture_index, &input), 0);
        Frame actual(WIND_RENDERER_PALETTE_BYTES);
        ASSERT_EQ(wind_renderer_input_v2_render(&input, actual.data(), actual.size(),
                                                nullptr),
                  0);
        const char *name = wind_renderer_fixture_name(fixture_index);
        ASSERT_NE(name, nullptr);
        const Frame expected = ReadPaletteFixture(name);
        EXPECT_EQ(actual, expected) << name;
        if (input.display_mode == WIND_RENDERER_MODE_THRESHOLD) {
            EXPECT_GT(std::count(actual.begin(), actual.end(), static_cast<uint8_t>(3)),
                      0)
                << name;
        }
    }
}

TEST(WindRenderer, AllocatesEveryOptionalRowCombinationAndKeepsWindFlexible) {
    for (int mask = 0; mask < 8; ++mask) {
        auto dashboard = Dashboard();
        dashboard.show_weather = mask & 1;
        dashboard.show_temperature = (mask >> 1) & 1;
        dashboard.show_tide = (mask >> 2) & 1;
        dashboard.tide_available = 0;
        wind_renderer_stats_t stats{};
        const Frame frame = Render(dashboard, &stats);
        EXPECT_EQ(stats.clipped_primitives, 0) << mask;
        const int conditions_height =
            dashboard.show_weather && dashboard.show_temperature   ? 54
            : dashboard.show_weather || dashboard.show_temperature ? 35
                                                                   : 0;
        EXPECT_EQ(stats.wind_baseline,
                  441 - conditions_height - (dashboard.show_tide ? 60 : 0))
            << mask;
        EXPECT_EQ(frame[(stats.wind_baseline - 1) * 800 + 38], 0) << mask;
    }
}

TEST(WindRenderer, DrawsTemperatureAndTideWithoutMovingEnabledRowsWhenDataIsMissing) {
    auto dashboard = Dashboard();
    dashboard.show_temperature = 1;
    dashboard.show_tide = 1;
    dashboard.tide_available = 1;
    dashboard.tide_sample_count = 120;
    for (int index = 0; index < 120; ++index) {
        dashboard.tide_samples[index] = {
            index / 24,
            index % 24,
            static_cast<int>(std::sin(index / 6.0) * 800),
            1,
        };
    }
    wind_renderer_stats_t populated_stats{};
    const Frame populated = Render(dashboard, &populated_stats);
    EXPECT_GT(CountBlack(populated, 13, populated_stats.temperature_row_top + 1, 786,
                         populated_stats.tide_row_top - 1),
              0);
    EXPECT_GT(CountBlack(populated, 13, populated_stats.tide_row_top + 1, 786, 466), 0);

    for (auto &day : dashboard.days)
        for (auto &sample : day.samples)
            sample.temperature_available = 0;
    dashboard.tide_available = 0;
    wind_renderer_stats_t missing_stats{};
    (void)Render(dashboard, &missing_stats);
    EXPECT_EQ(missing_stats.wind_baseline, populated_stats.wind_baseline);
    EXPECT_EQ(missing_stats.temperature_row_top, populated_stats.temperature_row_top);
    EXPECT_EQ(missing_stats.tide_row_top, populated_stats.tide_row_top);
}

TEST(WindRenderer, UsesRealTideDataInTheMarginsOutsideForecastLabels) {
    auto first = Dashboard();
    first.show_tide = 1;
    first.tide_available = 1;
    first.tide_sample_count = 24;
    for (int hour = 0; hour < 24; ++hour) {
        const int central_level = (hour - 14) * (hour - 14) * 10;
        first.tide_samples[hour] = {0, hour, central_level, 1};
    }

    auto second = first;
    second.tide_samples[5].sea_level_mm = -1200;
    second.tide_samples[6].sea_level_mm = 1200;
    second.tide_samples[7].sea_level_mm = -1200;
    second.tide_samples[21].sea_level_mm = 1200;
    second.tide_samples[22].sea_level_mm = -1200;
    second.tide_samples[23].sea_level_mm = 1200;

    EXPECT_NE(Render(first), Render(second));
}

TEST(WindRenderer, DoesNotLabelTideExtremaOutsideForecastCenters) {
    auto dashboard = Dashboard();
    dashboard.show_dedicated_footer = 0;
    dashboard.show_tide = 1;
    dashboard.tide_available = 1;
    dashboard.tide_sample_count = 24;
    for (int hour = 0; hour < 24; ++hour) {
        int level = 1000 - hour * 100;
        if (hour == 5) level = 0;
        if (hour == 6) level = 1200;
        if (hour == 22) level = -1200;
        if (hour == 23) level = 0;
        dashboard.tide_samples[hour] = {0, hour, level, 1};
    }
    for (int day = 0; day < 3; ++day)
        for (auto &sample : dashboard.days[day].samples)
            sample.available = 0;

    dashboard.use_24_hour = 0;
    const Frame twelve_hour = Render(dashboard);
    dashboard.use_24_hour = 1;
    const Frame twenty_four_hour = Render(dashboard);

    EXPECT_EQ(twelve_hour, twenty_four_hour);
}

TEST(WindRenderer, AppliesClockAndTemperaturePreferencesInTheSharedComposition) {
    auto dashboard = Dashboard();
    dashboard.use_24_hour = 0;
    dashboard.show_temperature = 1;
    dashboard.show_tide = 1;
    dashboard.tide_available = 1;
    dashboard.tide_sample_count = 120;
    for (int index = 0; index < 120; ++index) {
        dashboard.tide_samples[index] = {
            index / 24,
            index % 24,
            static_cast<int>(std::sin(index / 6.0) * 800),
            1,
        };
    }

    const Frame metricTwelveHour = Render(dashboard);
    dashboard.use_24_hour = 1;
    const Frame metricTwentyFourHour = Render(dashboard);
    dashboard.temperature_fahrenheit = 1;
    const Frame imperialTwentyFourHour = Render(dashboard);

    EXPECT_NE(metricTwentyFourHour, metricTwelveHour);
    EXPECT_NE(imperialTwentyFourHour, metricTwentyFourHour);
}

TEST(WindRenderer, PlacesExplicitQuarterHourTideExtremaBetweenHourPoints) {
    auto dashboard = Dashboard();
    dashboard.show_tide = 1;
    dashboard.tide_available = 1;
    dashboard.tide_sample_count = 24;
    for (int hour = 0; hour < 24; ++hour) {
        dashboard.tide_samples[hour] = {
            0,
            hour,
            static_cast<int>(std::sin(hour / 6.0) * 800),
            1,
        };
    }
    dashboard.tide_extremum_count = 1;
    dashboard.tide_extrema[0] = {0, 14, 0, 800, 1, 1};
    const Frame whole_hour = Render(dashboard);

    dashboard.tide_extrema[0].local_minute = 15;
    const Frame quarter_hour = Render(dashboard);

    EXPECT_NE(whole_hour, quarter_hour);
}

TEST(WindRenderer, KeepsStraightStructuralPixelsAtFullLumaInCleanPreview) {
    auto dashboard = Dashboard();
    dashboard.show_temperature = 1;
    dashboard.show_tide = 1;
    const Frame preview = RenderPreview(dashboard);
    const auto channel = [&preview](int x, int y) {
        return preview[(y * 800 + x) * 4];
    };
    for (int x = 12; x <= 787; ++x) {
        EXPECT_TRUE(channel(x, 12) == 0 || channel(x, 12) == 255);
        EXPECT_TRUE(channel(x, 449) == 0 || channel(x, 449) == 255);
    }
}

TEST(WindRenderer, MakesBatteryRedBelowTenPercentOnly) {
    auto dashboard = Dashboard();
    dashboard.battery_percent = 9;
    const Frame low = Render(dashboard);
    EXPECT_GT(CountColor(low, 746, 457, 771, 470, 3), 0);

    dashboard.battery_percent = 10;
    const Frame ten = Render(dashboard);
    EXPECT_EQ(CountColor(ten, 746, 457, 771, 470, 3), 0);
}

TEST(WindRenderer, ShowsNinetyNinePercentAsVisuallyFull) {
    auto dashboard = Dashboard();
    dashboard.battery_percent = 99;
    const Frame nearly_full = Render(dashboard);

    dashboard.battery_percent = 100;
    const Frame full = Render(dashboard);

    EXPECT_EQ(nearly_full, full);
}

TEST(WindRenderer, KeepsStatusRightAlignedWhenFadingLongTitle) {
    auto dashboard = Dashboard();
    wind_renderer_stats_t normal_stats{};
    (void)Render(dashboard, &normal_stats);
    dashboard.spot_name = "Noord-Holland Windmeetpost Met Een Uitzonderlijk Lange Naam";
    wind_renderer_stats_t long_stats{};
    (void)Render(dashboard, &long_stats);
    EXPECT_EQ(long_stats.status_right, normal_stats.status_right);
    EXPECT_EQ(long_stats.clipped_primitives, 0);
}

TEST(WindRenderer, UsesAvailableHeaderSpaceBesideStatus) {
    auto dashboard = Dashboard();
    dashboard.spot_name = "Edam Noord - Galgenveld";
    dashboard.show_dedicated_footer = 0;
    const Frame without_footer = Render(dashboard);
    dashboard.show_dedicated_footer = 1;
    const Frame with_footer = Render(dashboard);
    for (int y = 25; y <= 70; ++y)
        for (int x = 30; x <= 650; ++x)
            EXPECT_EQ(without_footer[(size_t)y * WIND_RENDERER_WIDTH + x],
                      with_footer[(size_t)y * WIND_RENDERER_WIDTH + x])
                << "at " << x << ", " << y;
}

TEST(WindRenderer, RejectsDuplicateOrderedModulesDuringValidation) {
    auto dashboard = Dashboard();
    dashboard.ordered_modules = 1;
    for (int index = 0; index < 5; ++index) dashboard.module_order[index] = index;
    EXPECT_EQ(wind_renderer_dashboard_valid(&dashboard), 1);
    dashboard.module_order[4] = 0;
    EXPECT_EQ(wind_renderer_dashboard_valid(&dashboard), 0);
}

TEST(WindRenderer, UppercasesTheSpotNameInTheSharedComposition) {
    auto mixed_case = Dashboard();
    mixed_case.spot_name = "Edam é";
    auto uppercase = mixed_case;
    uppercase.spot_name = "EDAM É";

    EXPECT_EQ(Render(mixed_case), Render(uppercase));
}

TEST(WindRenderer, ShowsUnsupportedSpotAccentsAsUppercaseLatinLetters) {
    auto accented = Dashboard();
    auto plain = accented;
    const std::pair<const char *, const char *> cases[] = {
        {"Kuźnica ž Łeba", "KUZNICA Z LEBA"},
        {"São ø ÿ ð þ", "SAO O Y D T"},
        {"ı ſ ĸ", "I S K"},
        {"Þorlákshöfn", "TORLÁKSHÖFN"},
    };
    for (const auto &[name, expected] : cases) {
        SCOPED_TRACE(name);
        accented.spot_name = name;
        plain.spot_name = expected;
        EXPECT_TRUE(Render(accented) == Render(plain));
    }
}

TEST(WindRenderer, FadesLongTitleIntoDitherWithoutTouchingStatus) {
    auto dashboard = Dashboard();
    dashboard.spot_name = "Noord-Holland Windmeetpost Met Een Uitzonderlijk Lange Naam";

    const Frame preview = RenderPreview(dashboard);
    int intermediate_luma = 0;
    for (int y = 20; y <= 82; ++y) {
        for (int x = 494; x <= 565; ++x) {
            const uint8_t luma = preview[(y * WIND_RENDERER_WIDTH + x) * 4];
            intermediate_luma += luma > 0 && luma < 255;
        }
    }
    EXPECT_GT(intermediate_luma, 0);

    const Frame dithered = Render(dashboard);
    EXPECT_GT(CountColor(dithered, 494, 20, 565, 82, 0), 0);
    EXPECT_GT(CountColor(dithered, 494, 20, 565, 82, 1), 0);

    wind_renderer_stats_t normal_stats{};
    (void)Render(Dashboard(), &normal_stats);
    wind_renderer_stats_t faded_stats{};
    (void)Render(dashboard, &faded_stats);
    EXPECT_EQ(faded_stats.status_right, normal_stats.status_right);
}

TEST(WindRenderer, EndsFadeAtLastVisibleGlyphInsteadOfTextAdvanceBox) {
    constexpr int title_left = 30;
    const auto rightmost_title_pixel = [=](const Frame &preview) {
        int rightmost = -1;
        for (int y = 20; y <= 70; ++y)
            for (int x = title_left; x <= 565; ++x)
                if (preview[(y * WIND_RENDERER_WIDTH + x) * 4] < 255 && x > rightmost)
                    rightmost = x;
        return rightmost;
    };

    auto plain_dashboard = Dashboard();
    plain_dashboard.spot_name = "CASTRICUM AAN";
    const int plain_ink_right = rightmost_title_pixel(RenderPreview(plain_dashboard));

    auto dashboard = Dashboard();
    dashboard.spot_name =
        "CASTRICUM AAN                                                  ";

    const Frame preview = RenderPreview(dashboard);
    int intermediate_luma = 0;
    for (int y = 20; y <= 70; ++y) {
        for (int x = title_left; x <= 565; ++x) {
            const uint8_t luma = preview[(y * WIND_RENDERER_WIDTH + x) * 4];
            intermediate_luma += luma > 0 && luma < 255;
        }
    }
    const int faded_ink_right = rightmost_title_pixel(preview);

    EXPECT_GT(intermediate_luma, 0);
    EXPECT_GT(faded_ink_right, title_left);
    EXPECT_LT(faded_ink_right, plain_ink_right);
}

TEST(WindRenderer, CoversAgedStaleUnavailableAndBatteryWarnings) {
    auto aged = Dashboard(WIND_RENDERER_AGED);
    aged.age_hours = 8;
    aged.battery_percent = 12;
    auto stale = Dashboard(WIND_RENDERER_STALE);
    stale.days[2].samples[3].available = 0;
    auto offline = Dashboard();
    offline.refresh_failed = 1;
    auto unavailable = Dashboard(WIND_RENDERER_UNAVAILABLE);
    unavailable.battery_percent = -1;
    for (const auto *dashboard : {&aged, &stale, &offline, &unavailable}) {
        wind_renderer_stats_t stats{};
        const Frame frame = Render(*dashboard, &stats);
        EXPECT_EQ(stats.dither_passes, 1);
        EXPECT_EQ(stats.clipped_primitives, 0);
        EXPECT_TRUE(std::all_of(frame.begin(), frame.end(),
                                [](uint8_t value) { return value <= 1; }));
    }
}

TEST(WindRendererGolden, Normal) {
    ExpectGolden("normal", Dashboard());
}

TEST(WindRendererGolden, HighWind) {
    auto dashboard = Dashboard();
    for (int sample = 0; sample < WIND_RENDERER_SAMPLES_PER_DAY; ++sample) {
        dashboard.days[0].samples[sample].sustained_kt = 36 + sample * 2;
        dashboard.days[0].samples[sample].gust_kt = 42 + sample * 3;
    }
    ExpectGolden("high_wind", dashboard);
}

TEST(WindRendererGolden, LongName) {
    auto dashboard = Dashboard();
    dashboard.spot_name = "Noord-Holland Windmeetpost Met Een Uitzonderlijk Lange Naam";
    ExpectGolden("long_name", dashboard);
}

TEST(WindRendererGolden, Stale) {
    auto dashboard = Dashboard(WIND_RENDERER_STALE);
    dashboard.age_hours = 25;
    dashboard.battery_percent = 12;
    ExpectGolden("stale", dashboard);
}

TEST(WindRendererGolden, Unavailable) {
    auto dashboard = Dashboard(WIND_RENDERER_UNAVAILABLE);
    dashboard.battery_percent = -1;
    ExpectGolden("unavailable", dashboard);
}

TEST(BatteryEmptyRenderer, UsesEachPanelsNativeWhiteAndKeepsBlackBackground) {
    for (auto display : {WIND_RENDERER_DISPLAY_E1001_GRAY4,
                         WIND_RENDERER_DISPLAY_E1002_SPECTRA6,
                         WIND_RENDERER_DISPLAY_E1003_GC16}) {
        const int height = display == WIND_RENDERER_DISPLAY_E1003_GC16 ? 600 : 480;
        const int width = display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? WIND_RENDERER_E1003_WIDTH : WIND_RENDERER_WIDTH;
        const int physical_height = display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? WIND_RENDERER_E1003_HEIGHT : WIND_RENDERER_HEIGHT;
        const auto px = [display](int value) { return display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? (value * WIND_RENDERER_E1003_WIDTH + 400) / 800 : value; };
        const auto py = [display](int value) { return display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? (value * WIND_RENDERER_E1003_HEIGHT + 300) / 600 : value; };
        const int offset = (height - 480) / 2;
        const uint8_t white = display == WIND_RENDERER_DISPLAY_E1001_GRAY4 ? 3 :
            display == WIND_RENDERER_DISPLAY_E1003_GC16 ? 15 : 1;
        std::vector<uint8_t> pixels(width * physical_height + 1, 0xA5);
        ASSERT_EQ(wind_renderer_render_battery_empty_for_display(
            display, pixels.data(), pixels.size() - 1), 0);
        EXPECT_EQ(pixels.back(), 0xA5);
        EXPECT_EQ(pixels.front(), 0);
        EXPECT_EQ(pixels[py(184 + offset) * width + px(357)], white);
        EXPECT_EQ(pixels[py(200 + offset) * width + px(400)], 0);
        EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end() - 1,
            [white, display](uint8_t p) { return display == WIND_RENDERER_DISPLAY_E1003_GC16
                ? p <= white : p == 0 || p == white; }));
        EXPECT_GT(std::count(pixels.begin() + py(245 + offset) * width,
                             pixels.begin() + py(290 + offset) * width, white), 100);
        int panel_width, panel_height;
        ASSERT_EQ(wind_renderer_display_dimensions(display, &panel_width, &panel_height), 0);
        std::vector<uint8_t> row(panel_width);
        for (int y = 0; y < panel_height; ++y)
            ASSERT_EQ(wind_renderer_project_display_row(display, pixels.data(),
                pixels.size() - 1, y, row.data(), row.size()), 0);
    }
}

TEST(SetupRenderer, ProducesTextInEachPanelsPaletteWithoutOverrunningTheBuffer) {
    for (auto display : {WIND_RENDERER_DISPLAY_E1001_GRAY4,
                         WIND_RENDERER_DISPLAY_E1002_SPECTRA6,
                         WIND_RENDERER_DISPLAY_E1003_GC16}) {
        const size_t size = display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? WIND_RENDERER_E1003_COMPOSITION_BYTES : WIND_RENDERER_PALETTE_BYTES;
        const uint8_t white = display == WIND_RENDERER_DISPLAY_E1001_GRAY4 ? 3 :
            display == WIND_RENDERER_DISPLAY_E1003_GC16 ? 15 : 1;
        std::vector<uint8_t> pixels(size + 1, 0xA5);
        ASSERT_NE(wind_renderer_render_setup(display, pixels.data(), size - 1), 0);
        EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](uint8_t p) { return p == 0xA5; }));
        ASSERT_EQ(wind_renderer_render_setup(display, pixels.data(), size), 0);
        EXPECT_EQ(pixels.back(), 0xA5);
        EXPECT_EQ(pixels.front(), white);
        EXPECT_GT(std::count(pixels.begin(), pixels.end() - 1, 0), 100);
        EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end() - 1,
            [white, display](uint8_t p) { return display == WIND_RENDERER_DISPLAY_E1003_GC16
                ? p <= white : p == 0 || p == white; }));
    }
}

TEST(BatteryEmptyRenderer, RejectsInvalidBuffersBeforeWriting) {
    std::vector<uint8_t> pixels(WIND_RENDERER_PALETTE_BYTES, 0xA5);
    EXPECT_NE(wind_renderer_render_battery_empty(nullptr, pixels.size()), 0);
    EXPECT_NE(wind_renderer_render_battery_empty(pixels.data(), pixels.size() - 1), 0);
    EXPECT_NE(wind_renderer_render_battery_empty_for_display(
        WIND_RENDERER_DISPLAY_E1003_GC16, pixels.data(), pixels.size()), 0);
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](uint8_t p) { return p == 0xA5; }));
}

TEST(SpotOverviewRenderer, RendersMixedGraphsAndFullFinalPagesInGc16) {
    wind_renderer_dashboard_t rows[3] = {Dashboard(), Dashboard(), Dashboard()};
    rows[0].spot_name="EDAM"; rows[1].spot_name="WIJK AAN ZEE"; rows[2].spot_name="SCHEVENINGEN";
    rows[1].swell_size=2;
    std::vector<uint8_t> pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES+1,0xA5);
    wind_renderer_stats_t stats{};
    ASSERT_EQ(wind_renderer_render_overview(rows,3,0,7,pixels.data(),pixels.size()-1,&stats),0);
    EXPECT_EQ(stats.clipped_primitives,0);
    EXPECT_EQ(pixels.back(),0xA5);
    EXPECT_TRUE(std::all_of(pixels.begin(),pixels.end()-1,[](uint8_t p){return p<=15;}));
    EXPECT_GT(std::count(pixels.begin(),pixels.end(),11),0);
    for (int y : {240, 480, 1100}) {
        EXPECT_EQ(pixels[y*WIND_RENDERER_E1003_WIDTH+390], 15);
        EXPECT_EQ(pixels[y*WIND_RENDERER_E1003_WIDTH+391], 0);
        EXPECT_EQ(pixels[y*WIND_RENDERER_E1003_WIDTH+392], 0);
        EXPECT_EQ(pixels[y*WIND_RENDERER_E1003_WIDTH+393], 15);
    }
    EXPECT_EQ(pixels[1273*WIND_RENDERER_E1003_WIDTH+1661],0);
    ASSERT_EQ(wind_renderer_render_overview(rows,3,1,4,pixels.data(),pixels.size()-1,&stats),0);
    EXPECT_EQ(stats.clipped_primitives,0);
    ASSERT_EQ(wind_renderer_render_overview(rows,3,2,5,pixels.data(),pixels.size()-1,&stats),0);
    EXPECT_EQ(stats.clipped_primitives,0);
    ASSERT_EQ(wind_renderer_render_overview(rows,3,4,7,pixels.data(),pixels.size()-1,&stats),0);
    EXPECT_EQ(stats.clipped_primitives,0);
    EXPECT_EQ(pixels[1374*WIND_RENDERER_E1003_WIDTH+117],0); // Outer bottom divider stays continuous.
}

TEST(SpotOverviewRenderer, RejectsInvalidPagesWithoutWriting) {
    auto row=Dashboard();
    std::vector<uint8_t> pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES,0xA5);
    EXPECT_NE(wind_renderer_render_overview(&row,0,0,7,pixels.data(),pixels.size(),nullptr),0);
    EXPECT_NE(wind_renderer_render_overview(&row,1,1,7,pixels.data(),pixels.size(),nullptr),0);
    EXPECT_NE(wind_renderer_render_overview(&row,1,9,7,pixels.data(),pixels.size(),nullptr),0);
    EXPECT_NE(wind_renderer_render_overview(&row,1,0,7,pixels.data(),pixels.size()-1,nullptr),0);
    EXPECT_TRUE(std::all_of(pixels.begin(),pixels.end(),[](uint8_t p){return p==0xA5;}));
}

TEST(WindRenderer, DayFocusKeepsRowHeightsShowsHourlySamplesAndFitsFooter) {
    auto d=Dashboard();
    d.custom_modules=1; d.ordered_modules=1; d.wind_size=2; d.swell_size=2;
    d.show_temperature=1;
    for(int i=0;i<5;++i)d.module_order[i]=i;
    d.visible_day_count=5;
    const auto render=[](const wind_renderer_dashboard_t &dashboard) {
        Frame out(WIND_RENDERER_E1003_COMPOSITION_BYTES);
        wind_renderer_stats_t stats{};
        EXPECT_EQ(wind_renderer_render_for_display(&dashboard,WIND_RENDERER_DISPLAY_E1003_GC16,out.data(),out.size(),&stats),0);
        EXPECT_EQ(stats.clipped_primitives,0u);
        return out;
    };
    const auto wide=render(d);
    d.visible_day_count=1; d.visible_sample_count=13;
    static const char *hours[]={"08","09","10","11","12","13","14","15","16","17","18","19","20"};
    for(int i=0;i<13;++i){d.days[0].samples[i]=d.days[0].samples[0];d.days[0].samples[i].time=hours[i];}
    const auto focus=render(d);
    EXPECT_NE(wide,focus);
    // Horizontal module boundaries remain at exactly the same y positions.
    for(int y=188;y<1334;++y){
        const auto line=[&](const Frame &f){for(int x=28;x<=1841;++x)if(f[y*WIND_RENDERER_E1003_WIDTH+x]!=0)return false;return true;};
        EXPECT_EQ(line(wide),line(focus)) << y;
    }
    d.days[0].samples[1].time="XX";
    const auto changed_time=render(d);
    EXPECT_FALSE(std::equal(focus.begin()+WIND_RENDERER_E1003_WIDTH*1334,focus.end(),
                            changed_time.begin()+WIND_RENDERER_E1003_WIDTH*1334));
    d.days[0].samples[1].time=hours[1];
    d.days[0].samples[12].sustained_kt=39;
    EXPECT_NE(focus,render(d)); // The last hourly sample reaches the renderer.
    d.visible_day_count=5;
    Frame invalid(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    EXPECT_NE(wind_renderer_render_for_display(&d,WIND_RENDERER_DISPLAY_E1003_GC16,invalid.data(),invalid.size(),nullptr),0);
    d.visible_sample_count=5; d.visible_day_count=3;
    EXPECT_NE(wind_renderer_render_for_display(&d,WIND_RENDERER_DISPLAY_E1003_GC16,invalid.data(),invalid.size(),nullptr),0);
}

TEST(WindRenderer, E1003FiveDayDashboardPreservesForecastAndBattery) {
    auto dashboard = Dashboard();
    dashboard.visible_day_count = 5;
    dashboard.battery_percent = 75;
    const auto render = [](const wind_renderer_dashboard_t &value) {
        Frame pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES);
        wind_renderer_stats_t stats{};
        EXPECT_EQ(wind_renderer_render_for_display(&value,
            WIND_RENDERER_DISPLAY_E1003_GC16, pixels.data(), pixels.size(), &stats), 0);
        EXPECT_EQ(stats.clipped_primitives, 0u);
        return pixels;
    };
    const auto original = render(dashboard);
    dashboard.days[4].samples[2].sustained_kt = 39;
    EXPECT_NE(original, render(dashboard));
    dashboard = Dashboard();
    dashboard.visible_day_count = 5;
    dashboard.battery_percent = 0;
    EXPECT_NE(original, render(dashboard));
    dashboard.battery_percent = 75;
    dashboard.state = WIND_RENDERER_UNAVAILABLE;
    EXPECT_NE(original, render(dashboard));
}

TEST(WindRenderer, E1003ThresholdValueIsSolidNativePixelText) {
    wind_renderer_input_v2_t input{};
    wind_renderer_dashboard_t threshold{};
    ASSERT_EQ(wind_renderer_fixture_build(WIND_RENDERER_FIXTURE_THRESHOLD_DEFAULT,
                                          &input), 0);
    ASSERT_EQ(wind_renderer_input_v2_to_dashboard(&input, &threshold), 0);
    auto solid = threshold;
    solid.display_mode = WIND_RENDERER_MODE_SOLID;
    Frame with_value(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    Frame without_value(with_value.size());
    ASSERT_EQ(wind_renderer_render_for_display(&threshold,
        WIND_RENDERER_DISPLAY_E1003_GC16, with_value.data(), with_value.size(), nullptr), 0);
    ASSERT_EQ(wind_renderer_render_for_display(&solid,
        WIND_RENDERER_DISPLAY_E1003_GC16, without_value.data(), without_value.size(), nullptr), 0);
    int changed = 0;
    int native_edges = 0;
    for (int y = 780; y < 920; ++y)
        for (int x = 1570; x < 1850; ++x) {
            const size_t index = (size_t)y * WIND_RENDERER_E1003_WIDTH + x;
            if (with_value[index] == without_value[index]) continue;
            ++changed;
            EXPECT_TRUE(with_value[index] == 0 || with_value[index] == 15);
            if (x > 1570 && with_value[index] != with_value[index - 1])
                ++native_edges;
        }
    EXPECT_GT(changed, 200);
    EXPECT_GT(native_edges, 30);
}

TEST(WindRenderer, E1003SetupAndBatteryHeadlinesUseFullSizeType) {
    Frame pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    ASSERT_EQ(wind_renderer_render_setup(WIND_RENDERER_DISPLAY_E1003_GC16,
        pixels.data(), pixels.size()), 0);
    int setup_ink_rows = 0;
    for (int y = 610; y < 710; ++y) {
        bool has_ink = false;
        for (int x = 600; x < 1300; ++x)
            has_ink |= pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + x] < 4;
        setup_ink_rows += has_ink;
    }
    EXPECT_GT(setup_ink_rows, 55);

    ASSERT_EQ(wind_renderer_render_battery_empty_for_display(
        WIND_RENDERER_DISPLAY_E1003_GC16, pixels.data(), pixels.size()), 0);
    int battery_ink_rows = 0;
    for (int y = 735; y < 830; ++y) {
        bool has_ink = false;
        for (int x = 600; x < 1300; ++x)
            has_ink |= pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + x] > 11;
        battery_ink_rows += has_ink;
    }
    EXPECT_GT(battery_ink_rows, 55);
}

TEST(WindRenderer, E1003TideCurveUsesNativeSubpixelHeights) {
    wind_renderer_input_v2_t input{};
    wind_renderer_dashboard_t dashboard{};
    ASSERT_EQ(wind_renderer_fixture_build(WIND_RENDERER_FIXTURE_ROWS_TIDE,
                                          &input), 0);
    ASSERT_EQ(wind_renderer_input_v2_to_dashboard(&input, &dashboard), 0);
    Frame pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
        WIND_RENDERER_DISPLAY_E1003_GC16, pixels.data(), pixels.size(), nullptr), 0);
    int subpixel_ink = 0;
    for (int y = 1200; y < 1268; ++y)
        for (int x = 50; x < 350; ++x) {
            const uint8_t pixel = pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + x];
            subpixel_ink += pixel > 0 && pixel < 15;
        }
    EXPECT_GT(subpixel_ink, 100);
}

TEST(WindRenderer, SwellArrowStaysConnectedWhileRotating) {
    auto dashboard = Dashboard();
    dashboard.custom_modules = 1;
    dashboard.ordered_modules = 1;
    dashboard.wind_size = 0;
    dashboard.swell_size = 1;
    dashboard.show_weather = 0;
    for (int module = 0; module < 5; ++module) dashboard.module_order[module] = module;
    for (auto &day : dashboard.swell)
        for (auto &sample : day)
            sample = {-1, -1, -1};

    for (const auto display : {WIND_RENDERER_DISPLAY_E1002_SPECTRA6,
                               WIND_RENDERER_DISPLAY_E1003_GC16}) {
        const int width = display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? WIND_RENDERER_E1003_WIDTH : WIND_RENDERER_WIDTH;
        const size_t size = display == WIND_RENDERER_DISPLAY_E1003_GC16
            ? WIND_RENDERER_E1003_COMPOSITION_BYTES : WIND_RENDERER_PALETTE_BYTES;
        Frame empty(size), frame(size);
        ASSERT_EQ(wind_renderer_render_for_display(&dashboard, display,
            empty.data(), empty.size(), nullptr), 0);
        std::vector<double> centers_x, centers_y;
        for (int degrees = 0; degrees < 360; degrees += 15) {
            dashboard.swell[0][0].destination_degrees = degrees;
            ASSERT_EQ(wind_renderer_render_for_display(&dashboard, display,
                frame.data(), frame.size(), nullptr), 0);
            std::vector<uint8_t> added(size, 0), visited(size, 0);
            std::vector<int> ink;
            for (size_t pixel = 0; pixel < size; ++pixel)
                if (frame[pixel] < empty[pixel]) {
                    added[pixel] = 1;
                    ink.push_back(static_cast<int>(pixel));
                }
            ASSERT_GT(ink.size(), 20u) << degrees;
            double x_total = 0, y_total = 0, weight_total = 0;
            for (const int pixel : ink) {
                const double weight = empty[pixel] - frame[pixel];
                x_total += (pixel % width + 0.5) * weight;
                y_total += (pixel / width + 0.5) * weight;
                weight_total += weight;
            }
            centers_x.push_back(x_total / weight_total);
            centers_y.push_back(y_total / weight_total);
            int components = 0;
            for (const int first : ink) {
                if (visited[first]) continue;
                ++components;
                std::vector<int> pending = {first};
                visited[first] = 1;
                while (!pending.empty()) {
                    const int current = pending.back();
                    pending.pop_back();
                    const int x = current % width, y = current / width;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (abs(dx) + abs(dy) != 1) continue;
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || nx >= width || ny < 0 ||
                                static_cast<size_t>(ny) * width + nx >= size) continue;
                            const int next = ny * width + nx;
                            if (added[next] && !visited[next]) {
                                visited[next] = 1;
                                pending.push_back(next);
                            }
                        }
                }
            }
            EXPECT_EQ(components, 1) << "display " << display << ", " << degrees << "°";
        }
        const double max_drift = display == WIND_RENDERER_DISPLAY_E1003_GC16 ? 2.1 : 0.9;
        const auto x_extent = std::minmax_element(centers_x.begin(), centers_x.end());
        const auto y_extent = std::minmax_element(centers_y.begin(), centers_y.end());
        EXPECT_LE(*x_extent.second - *x_extent.first, max_drift);
        EXPECT_LE(*y_extent.second - *y_extent.first, max_drift);
        dashboard.swell[0][0].destination_degrees = -1;
    }
}

TEST(SpotOverviewRenderer, E1003SpotNameUsesIndividualPanelPixels) {
    auto row = Dashboard();
    row.spot_name = "WIJK AAN ZEE";
    Frame pixels(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    ASSERT_EQ(wind_renderer_render_overview(&row, 1, 0, 1,
        pixels.data(), pixels.size(), nullptr), 0);
    std::vector<bool> logical_boundaries(WIND_RENDERER_E1003_WIDTH, false);
    for (int x = 0; x < WIND_RENDERER_WIDTH; ++x)
        logical_boundaries[(x * WIND_RENDERER_E1003_WIDTH + WIND_RENDERER_WIDTH / 2) /
                           WIND_RENDERER_WIDTH] = true;
    int native_edges = 0;
    int gray_name_pixels = 0;
    for (int y = 150; y < 220; ++y)
        for (int x = 80; x < 650; ++x) {
            const uint8_t pixel = pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + x];
            gray_name_pixels += pixel != 0 && pixel != 15;
            if (!logical_boundaries[x] &&
                pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + x] !=
                    pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + x - 1])
                ++native_edges;
        }
    EXPECT_GT(native_edges, 500);
    EXPECT_EQ(gray_name_pixels, 0);
    int visible_divider_pixels = 0;
    for (int y = 150; y < 195; ++y)
        visible_divider_pixels +=
            pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + 390] == 15 &&
            pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + 391] == 0 &&
            pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + 392] == 0 &&
            pixels[(size_t)y * WIND_RENDERER_E1003_WIDTH + 393] == 15;
    EXPECT_GT(visible_divider_pixels, 0);
}

TEST(WindRenderer, E1003TextUsesIndividualPanelPixels) {
    const auto dashboard = Dashboard();
    Frame frame(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
        WIND_RENDERER_DISPLAY_E1003_GC16, frame.data(), frame.size(), nullptr), 0);
    std::vector<bool> logical_boundaries(WIND_RENDERER_E1003_WIDTH, false);
    for (int x = 0; x < WIND_RENDERER_WIDTH; ++x)
        logical_boundaries[(x * WIND_RENDERER_E1003_WIDTH + WIND_RENDERER_WIDTH / 2) /
                           WIND_RENDERER_WIDTH] = true;
    int native_edges = 0;
    for (int y = 50; y < 165; ++y)
        for (int x = 75; x < 700; ++x)
            if (!logical_boundaries[x] &&
                frame[y * WIND_RENDERER_E1003_WIDTH + x] !=
                    frame[y * WIND_RENDERER_E1003_WIDTH + x - 1])
                ++native_edges;
    EXPECT_GT(native_edges, 100);
}

TEST(WindRenderer, E1003SmallLabelsUseSolidPanelPixels) {
    auto first = Dashboard();
    auto second = first;
    second.days[0].day = "SUN";
    Frame before(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    Frame after(before.size());
    ASSERT_EQ(wind_renderer_render_for_display(&first,
        WIND_RENDERER_DISPLAY_E1003_GC16, before.data(), before.size(), nullptr), 0);
    ASSERT_EQ(wind_renderer_render_for_display(&second,
        WIND_RENDERER_DISPLAY_E1003_GC16, after.data(), after.size(), nullptr), 0);
    int changed = 0;
    for (int y = 190; y < 250; ++y)
        for (int x = 40; x < 300; ++x) {
            const size_t index = (size_t)y * WIND_RENDERER_E1003_WIDTH + x;
            if (before[index] == after[index]) continue;
            ++changed;
            EXPECT_TRUE(after[index] == 0 || after[index] == 15);
        }
    EXPECT_GT(changed, 50);
}

TEST(WindRenderer, E1003WeatherIconsUseIndividualPanelPixels) {
    auto dashboard = Dashboard();
    auto without_weather = dashboard;
    for (auto &day : without_weather.days)
        for (auto &sample : day.samples)
            sample.weather = WIND_RENDERER_WEATHER_UNAVAILABLE;
    Frame with_icon(WIND_RENDERER_E1003_COMPOSITION_BYTES);
    Frame without_icon(with_icon.size());
    ASSERT_EQ(wind_renderer_render_for_display(&dashboard,
        WIND_RENDERER_DISPLAY_E1003_GC16, with_icon.data(), with_icon.size(), nullptr), 0);
    ASSERT_EQ(wind_renderer_render_for_display(&without_weather,
        WIND_RENDERER_DISPLAY_E1003_GC16, without_icon.data(), without_icon.size(), nullptr), 0);
    std::vector<bool> logical_boundaries(WIND_RENDERER_E1003_WIDTH, false);
    for (int x = 0; x < WIND_RENDERER_WIDTH; ++x)
        logical_boundaries[(x * WIND_RENDERER_E1003_WIDTH + WIND_RENDERER_WIDTH / 2) /
                           WIND_RENDERER_WIDTH] = true;
    int native_edges = 0;
    for (int y = 0; y < WIND_RENDERER_E1003_HEIGHT; ++y)
        for (int x = 1; x < WIND_RENDERER_E1003_WIDTH; ++x) {
            const size_t index = (size_t)y * WIND_RENDERER_E1003_WIDTH + x;
            const bool changed = with_icon[index] != without_icon[index];
            const bool previous_changed = with_icon[index - 1] != without_icon[index - 1];
            if (changed != previous_changed && !logical_boundaries[x]) ++native_edges;
        }
    EXPECT_GT(native_edges, 100);
}
