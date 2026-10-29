#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>

extern "C" {
#include "wind_schedule.h"
#include "wind_timezone.h"
}

static const char *TIMEZONE = "Europe/Amsterdam";

static time_t local_time(int year, int month, int day, int hour, int minute)
{
    wind_local_datetime_t value = {
        .year = (int16_t) year, .month = (uint8_t) month, .day = (uint8_t) day,
        .hour = (uint8_t) hour, .minute = (uint8_t) minute,
    };
    int64_t unix_seconds = 0;
    EXPECT_EQ(wind_timezone_to_unix(TIMEZONE, &value, &unix_seconds), ESP_OK);
    return (time_t) unix_seconds;
}

class WindScheduleTest : public testing::Test {};

TEST_F(WindScheduleTest, PreservesScheduledRetriesWhenUpgradingShortIdentifiers) {
    struct PreviousRecord {
        uint32_t magic;
        struct {
            uint32_t schema_version;
            char spot_id[32];
            char timezone[40];
            int64_t last_attempted_boundary, last_satisfied_boundary;
            int64_t retry_boundary, retry_at, final_retry_at;
        } state;
        uint32_t checksum;
    } previous{};
    previous.magic = 0x574E4453u;
    previous.state.schema_version = 4;
    strcpy(previous.state.spot_id, "edam");
    strcpy(previous.state.timezone, TIMEZONE);
    previous.state.last_attempted_boundary = 1787544000;
    previous.state.last_satisfied_boundary = 1787530000;
    previous.state.retry_boundary = 1787544000;
    previous.state.retry_at = 1787544030;
    previous.state.final_retry_at = 1787544300;
    uint32_t checksum = 2166136261u;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&previous.state);
    for (size_t i = 0; i < sizeof(previous.state); ++i) checksum = (checksum ^ bytes[i]) * 16777619u;
    previous.checksum = checksum;
    const auto path = std::filesystem::temp_directory_path() / "windpeek-upgrade-schedule.cache";
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&previous), sizeof(previous));
    file.close();
    wind_schedule_state_t state{};
    ASSERT_EQ(wind_schedule_state_load_scoped(path.c_str(), "edam", TIMEZONE, &state), ESP_OK);
    EXPECT_EQ(state.schema_version, WIND_SCHEDULE_SCHEMA_VERSION);
    EXPECT_EQ(state.last_attempted_boundary, previous.state.last_attempted_boundary);
    EXPECT_EQ(state.last_satisfied_boundary, previous.state.last_satisfied_boundary);
    EXPECT_EQ(state.retry_boundary, previous.state.retry_boundary);
    EXPECT_EQ(state.retry_at, previous.state.retry_at);
    EXPECT_EQ(state.final_retry_at, previous.state.final_retry_at);
    ASSERT_EQ(wind_schedule_state_store(path.c_str(), &state), ESP_OK);
    ASSERT_EQ(wind_schedule_state_load_scoped(path.c_str(), "edam", TIMEZONE, &state), ESP_OK);
    EXPECT_EQ(state.final_retry_at, previous.state.final_retry_at);
    std::filesystem::remove(path);
}

TEST_F(WindScheduleTest, SelectsAllFixedBoundariesAndNextBoundary)
{
    const int hours[] = {0, 7, 11, 15, 19};
    const int minutes[] = {5, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i) {
        time_t at = local_time(2026, 8, 24, hours[i], minutes[i]);
        EXPECT_EQ(wind_schedule_latest_boundary(TIMEZONE, at), at);
    }
    EXPECT_EQ(wind_schedule_next_boundary(TIMEZONE, local_time(2026, 8, 24, 19, 0)),
              local_time(2026, 8, 25, 0, 5));
}

TEST_F(WindScheduleTest, AttemptsAutomaticallyOncePerBoundaryAndManualDoesNotAlterState)
{
    wind_schedule_state_t state;
    ASSERT_EQ(wind_schedule_state_set_scope(&state, "edam", "Europe/Amsterdam"), ESP_OK);
    time_t now = local_time(2026, 8, 24, 11, 30);
    int64_t boundary = 0;
    ASSERT_TRUE(wind_schedule_is_due(&state, now, &boundary));
    wind_schedule_mark_attempted(&state, boundary);
    EXPECT_FALSE(wind_schedule_is_due(&state, now, nullptr));
    EXPECT_TRUE(wind_schedule_is_due(&state, local_time(2026, 8, 24, 15, 1), nullptr));
}

TEST_F(WindScheduleTest, HandlesDstDaysUsingLocalWallClock)
{
    time_t before_jump = local_time(2026, 3, 29, 0, 6);
    EXPECT_EQ(wind_schedule_next_boundary(TIMEZONE, before_jump),
              local_time(2026, 3, 29, 7, 0));
    time_t repeated_hour_day = local_time(2026, 10, 25, 19, 1);
    EXPECT_EQ(wind_schedule_next_boundary(TIMEZONE, repeated_hour_day),
              local_time(2026, 10, 26, 0, 5));
}

TEST_F(WindScheduleTest, OffersOneRetryBeforeTheNextRegularBoundary)
{
    wind_schedule_state_t state;
    ASSERT_EQ(wind_schedule_state_set_scope(&state, "edam", "Europe/Amsterdam"), ESP_OK);
    const time_t failed_at = local_time(2026, 8, 24, 11, 0);
    const int64_t boundary = wind_schedule_latest_boundary(TIMEZONE, failed_at);
    wind_schedule_mark_attempted(&state, boundary);
    wind_schedule_schedule_retry(&state, boundary, failed_at + 5 * 60);

    EXPECT_EQ(wind_schedule_next_attempt(&state, failed_at), failed_at + 5 * 60);
    EXPECT_FALSE(wind_schedule_retry_is_due(&state, failed_at + 4 * 60, nullptr));
    EXPECT_TRUE(wind_schedule_retry_is_due(&state, failed_at + 5 * 60, nullptr));

    wind_schedule_consume_retry(&state);
    EXPECT_FALSE(wind_schedule_retry_is_due(&state, failed_at + 10 * 60, nullptr));
    EXPECT_EQ(wind_schedule_next_attempt(&state, failed_at + 5 * 60),
              wind_schedule_next_boundary(TIMEZONE, failed_at + 5 * 60));
}

TEST_F(WindScheduleTest, PersistsScopeAndRejectsStateFromAnotherSpot)
{
    auto path = std::filesystem::temp_directory_path() / "einkwind-schedule-scope-test";
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".tmp");

    wind_schedule_state_t state;
    ASSERT_EQ(wind_schedule_state_set_scope(&state, "edam", "Europe/Amsterdam"), ESP_OK);
    ASSERT_EQ(wind_schedule_state_set_scope(&state, "edam", "Europe/Amsterdam"), ESP_OK);
    const time_t boundary = local_time(2026, 8, 24, 11, 0);
    wind_schedule_mark_attempted(&state, boundary);
    wind_schedule_schedule_retry(&state, boundary, boundary + 5 * 60);
    ASSERT_EQ(wind_schedule_state_store(path.c_str(), &state), ESP_OK);

    wind_schedule_state_t loaded;
    ASSERT_EQ(wind_schedule_state_load_scoped(path.c_str(), "edam", "Europe/Amsterdam", &loaded),
              ESP_OK);
    EXPECT_EQ(loaded.last_attempted_boundary, state.last_attempted_boundary);
    EXPECT_EQ(loaded.retry_at, boundary + 5 * 60);
    EXPECT_EQ(loaded.retry_boundary, boundary);

    EXPECT_EQ(wind_schedule_state_load_scoped(path.c_str(), "brouwersdam", "Europe/Amsterdam",
                                              &loaded),
              ESP_ERR_INVALID_STATE);
    EXPECT_TRUE(wind_schedule_state_matches_scope(&loaded, "brouwersdam", "Europe/Amsterdam"));
    EXPECT_EQ(loaded.last_attempted_boundary, 0);

    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".tmp");
}
