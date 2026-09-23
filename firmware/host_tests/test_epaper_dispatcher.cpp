#include <gtest/gtest.h>

extern "C" {
#include "epaper.h"
#include "epaper_backend.h"
}

namespace {

struct Calls {
    int init;
    int display;
    int display_logical;
    int clear;
    int sleep;
};

Calls e1001_calls;
Calls e1002_calls;

esp_err_t fake_init_e1001(const epaper_config_t *)
{
    ++e1001_calls.init;
    return ESP_OK;
}

esp_err_t fake_display_e1001(uint8_t *)
{
    ++e1001_calls.display;
    return ESP_OK;
}

esp_err_t fake_display_logical_e1001(const uint8_t *, size_t)
{
    ++e1001_calls.display_logical;
    return ESP_OK;
}

esp_err_t fake_clear_e1001(uint8_t *, uint8_t)
{
    ++e1001_calls.clear;
    return ESP_OK;
}

esp_err_t fake_sleep_e1001(void)
{
    ++e1001_calls.sleep;
    return ESP_OK;
}

esp_err_t fake_init_e1002(const epaper_config_t *)
{
    ++e1002_calls.init;
    return ESP_OK;
}

esp_err_t fake_display_e1002(uint8_t *)
{
    ++e1002_calls.display;
    return ESP_OK;
}

esp_err_t fake_display_logical_e1002(const uint8_t *, size_t)
{
    ++e1002_calls.display_logical;
    return ESP_OK;
}

esp_err_t fake_clear_e1002(uint8_t *, uint8_t)
{
    ++e1002_calls.clear;
    return ESP_OK;
}

esp_err_t fake_sleep_e1002(void)
{
    ++e1002_calls.sleep;
    return ESP_OK;
}

const epaper_backend_t e1001_backend = {
    .name = "uc8179-gray4",
    .width = 800,
    .height = 480,
    .init = fake_init_e1001,
    .display = fake_display_e1001,
    .display_logical = fake_display_logical_e1001,
    .clear = fake_clear_e1001,
    .set_temperature = nullptr,
    .enter_deepsleep = fake_sleep_e1001,
};

const epaper_backend_t e1002_backend = {
    .name = "ed2208-gca",
    .width = 800,
    .height = 480,
    .init = fake_init_e1002,
    .display = fake_display_e1002,
    .display_logical = fake_display_logical_e1002,
    .clear = fake_clear_e1002,
    .set_temperature = nullptr,
    .enter_deepsleep = fake_sleep_e1002,
};

class EpaperDispatcherTest : public testing::Test {
  protected:
    void SetUp() override
    {
        e1001_calls = {};
        e1002_calls = {};
        epaper_dispatcher_reset_for_test();
        epaper_dispatcher_set_backends_for_test(&e1001_backend, &e1002_backend);
    }
};

TEST(EpaperPanelDiagnosticsTest, KeepsTimeoutStepUntilNextAttempt)
{
    epaper_panel_diagnostics_reset();
    EXPECT_EQ(epaper_panel_diagnostics_get().phase, EPAPER_PANEL_NONE);
    epaper_panel_diagnostics_timeout(EPAPER_PANEL_REFRESH, 40000, 0);
    const auto failure = epaper_panel_diagnostics_get();
    EXPECT_EQ(failure.phase, EPAPER_PANEL_REFRESH);
    EXPECT_EQ(failure.wait_ms, 40000u);
    EXPECT_EQ(failure.busy_level, 0u);
    epaper_panel_diagnostics_timeout(EPAPER_PANEL_SLEEP_POWER_OFF, 40000, 1);
    EXPECT_EQ(epaper_panel_diagnostics_get().phase, EPAPER_PANEL_REFRESH);
    epaper_panel_diagnostics_reset();
    EXPECT_EQ(epaper_panel_diagnostics_get().phase, EPAPER_PANEL_NONE);
}

TEST_F(EpaperDispatcherTest, UnknownRefusesEveryPanelOperationWithoutBackendCalls)
{
    uint8_t image[1] = {0};
    epaper_config_t config = {};

    EXPECT_EQ(epaper_init(&config), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(epaper_display(image), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(epaper_display_logical(image, sizeof(image)), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(epaper_clear(image, EPD_7IN3E_WHITE), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(epaper_enter_deepsleep(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(e1001_calls.init + e1001_calls.display + e1001_calls.display_logical +
                  e1001_calls.clear + e1001_calls.sleep,
              0);
    EXPECT_EQ(e1002_calls.init + e1002_calls.display + e1002_calls.display_logical +
                  e1002_calls.clear + e1002_calls.sleep,
              0);
}

TEST_F(EpaperDispatcherTest, E1002DelegatesEachLifecycleOperationExactlyOnce)
{
    uint8_t image[1] = {0};
    epaper_config_t config = {};

    ASSERT_EQ(epaper_select_backend(EPAPER_HARDWARE_E1002), ESP_OK);
    EXPECT_EQ(epaper_init(&config), ESP_OK);
    EXPECT_EQ(epaper_display(image), ESP_OK);
    EXPECT_EQ(epaper_display_logical(image, sizeof(image)), ESP_OK);
    EXPECT_EQ(epaper_clear(image, EPD_7IN3E_WHITE), ESP_OK);
    EXPECT_EQ(epaper_enter_deepsleep(), ESP_OK);

    EXPECT_EQ(e1002_calls.init, 1);
    EXPECT_EQ(e1002_calls.display, 1);
    EXPECT_EQ(e1002_calls.display_logical, 1);
    EXPECT_EQ(e1002_calls.clear, 1);
    EXPECT_EQ(e1002_calls.sleep, 1);
    EXPECT_EQ(e1001_calls.init + e1001_calls.display + e1001_calls.display_logical +
                  e1001_calls.clear + e1001_calls.sleep,
              0);
}

TEST_F(EpaperDispatcherTest, E1001LogicalFrameNeverFallsThroughToE1002)
{
    uint8_t image[1] = {0};
    epaper_config_t config = {};

    ASSERT_EQ(epaper_select_backend(EPAPER_HARDWARE_E1001), ESP_OK);
    EXPECT_EQ(epaper_active_hardware(), EPAPER_HARDWARE_E1001);
    EXPECT_EQ(epaper_init(&config), ESP_OK);
    EXPECT_EQ(epaper_display_logical(image, sizeof(image)), ESP_OK);
    EXPECT_EQ(epaper_enter_deepsleep(), ESP_OK);
    EXPECT_EQ(e1001_calls.init, 1);
    EXPECT_EQ(e1001_calls.display_logical, 1);
    EXPECT_EQ(e1001_calls.sleep, 1);
    EXPECT_EQ(e1002_calls.init + e1002_calls.display +
                  e1002_calls.display_logical + e1002_calls.clear +
                  e1002_calls.sleep,
              0);
}

TEST_F(EpaperDispatcherTest, SecondSelectionInOneBootIsRejected)
{
    ASSERT_EQ(epaper_select_backend(EPAPER_HARDWARE_E1002), ESP_OK);
    EXPECT_EQ(epaper_select_backend(EPAPER_HARDWARE_E1001), ESP_ERR_INVALID_STATE);
}

struct BusyProbe {
    unsigned polls;
};

bool always_busy(void *context)
{
    ++static_cast<BusyProbe *>(context)->polls;
    return true;
}

void no_delay(void *, uint32_t) {}

TEST(EpaperBusyWaitTest, StuckBusyReturnsTimeoutAfterTheConfiguredBound)
{
    BusyProbe probe = {};
    EXPECT_EQ(epaper_wait_busy_bounded(always_busy, no_delay, &probe, 10, 40), ESP_ERR_TIMEOUT);
    EXPECT_EQ(probe.polls, 5u);
}

}  // namespace
