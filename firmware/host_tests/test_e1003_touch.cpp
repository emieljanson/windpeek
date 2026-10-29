#include <gtest/gtest.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>
#include "esp_err.h"
#include "../components/board_hal/include/board_touch.h"

// Exercise the actual register driver against a deterministic GT911 bus.
using i2c_master_dev_handle_t = void *;
static void *i2c_bus;
static int probes, register_reads;
static std::array<uint8_t,65536> registers;
static int present_address, failed_register, acknowledgements, resets;
static std::vector<int> writes;
struct i2c_device_config_t { int dev_addr_length; int device_address; int scl_speed_hz; };
struct gpio_config_t { uint64_t pin_bit_mask; int mode; int pull_up_en; };
#define I2C_ADDR_BIT_LEN_7 0
#define BOARD_HAL_TOUCH_RESET 48
#define BOARD_HAL_TOUCH_INT 2
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_ENABLE 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(int) {}
static bool reset_held;
static int reset_level;
static void gpio_hold_dis(int pin) { if (pin==48) reset_held=false; }
static void gpio_hold_en(int pin) { if (pin==48) reset_held=true; }
static void gpio_config(const gpio_config_t *) {}
static void gpio_set_level(int pin,int level) {
    if(pin==48) reset_level=level;
    if(pin==48 && !level) {
        resets++;
        std::memcpy(registers.data()+0x8140,"911\0",4);
        registers[0x814b]=registers[0x814e]=0;
    }
}
using SemaphoreHandle_t = int;
static int xSemaphoreCreateMutex() { return 1; }
static void xSemaphoreTake(int, int) {}
static void xSemaphoreGive(int) {}
#define portMAX_DELAY 0
static esp_err_t i2c_master_probe(void *,int address,int) { ++probes; return address==present_address?ESP_OK:ESP_FAIL; }
static esp_err_t i2c_master_bus_add_device(void *,const i2c_device_config_t *,void **device) { *device=&registers;return ESP_OK; }
static void i2c_master_bus_rm_device(void *) {}
static esp_err_t i2c_master_transmit_receive(void *,const void *address,size_t,void *data,size_t size,int) {
    ++register_reads;
    auto bytes=static_cast<const uint8_t *>(address);int reg=bytes[0]*256+bytes[1];
    if(reg==failed_register)return ESP_ERR_TIMEOUT;
    std::memcpy(data,registers.data()+reg,size);return ESP_OK;
}
static esp_err_t i2c_master_transmit(void *,const void *data,size_t size,int) {
    auto bytes=static_cast<const uint8_t *>(data);
    int reg=bytes[0]*256+bytes[1];
    if(reg==failed_register)return ESP_ERR_TIMEOUT;
    writes.push_back(reg);
    std::memcpy(registers.data()+reg,bytes+2,size-2);
    if(reg==0x814e)acknowledgements++;
    return ESP_OK;
}
#include "../components/board_hal/src/e1003_touch.inc"

class E1003Touch : public testing::Test {
protected:
    void SetUp() override {
        registers.fill(0); present_address=0x5d;failed_register=-1;acknowledgements=resets=0;
        touch_device=nullptr;touch_width=touch_height=0;
        i2c_bus=&registers; probes=register_reads=0;
        writes.clear(); reset_held=false; reset_level=1;
        std::memcpy(registers.data()+0x8140,"911",3);
        limits(1872,1404); registers[0x804d]=1;
    }
    void limits(int x,int y) {
        registers[0x8048]=x&255;registers[0x8049]=x>>8;
        registers[0x804a]=y&255;registers[0x804b]=y>>8;
    }
    void contact(int count,int x,int y) {
        registers[0x814e]=0x80|count;
        registers[0x8150]=x&255;registers[0x8151]=x>>8;
        registers[0x8152]=y&255;registers[0x8153]=y>>8;
    }
};
TEST_F(E1003Touch, EnablingAfterButtonRestoresTouchWithoutStaleContact) {
    ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    contact(1,936,702);
    ASSERT_EQ(board_hal_touch_enable(),ESP_OK);
    EXPECT_FALSE(reset_held);
    EXPECT_EQ(reset_level,1);
    board_touch_sample_t point{};
    EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_FINISHED);
    contact(1,936,702);
    ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);
    EXPECT_EQ(point.contacts,1u); EXPECT_EQ(point.x,936); EXPECT_EQ(point.y,702);
    EXPECT_EQ(acknowledgements,1);
    int previous_resets=resets;
    ASSERT_EQ(board_hal_touch_enable(),ESP_OK);
    EXPECT_EQ(resets,previous_resets);
}

TEST_F(E1003Touch, DetectsBothAddresses) {
    for(int address:{0x5d,0x14}) {
        board_hal_touch_prepare_sleep();
        present_address=address;
        ASSERT_EQ(board_hal_touch_enable(),ESP_OK);
        EXPECT_TRUE(board_hal_touch_available());
    }
}
TEST_F(E1003Touch, MapsDifferentResolutionAndDeliversRelease) {
    limits(1000,1000);board_hal_touch_enable();contact(1,500,500);
    board_touch_sample_t point{};ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);
    EXPECT_EQ(point.x,936);EXPECT_EQ(point.y,702);
    contact(0,0,0);ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);EXPECT_EQ(point.contacts,0u);
}
TEST_F(E1003Touch, RejectsInvalidPointsAndAcknowledgesFailedFrames) {
    board_hal_touch_enable();board_touch_sample_t point{};
    contact(1,1872,10);EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_INVALID_RESPONSE);
    contact(6,10,10);EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_INVALID_RESPONSE);
    contact(1,10,10);failed_register=0x814f;EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_TIMEOUT);
    EXPECT_EQ(acknowledgements,3);
}
TEST_F(E1003Touch, MissingControllerLeavesPhysicalControlsAvailable) {
    present_address=0;board_hal_touch_enable();EXPECT_FALSE(board_hal_touch_available());EXPECT_EQ(resets,1);
    board_touch_sample_t point{};EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_SUPPORTED);
    EXPECT_EQ(board_hal_touch_read(nullptr),ESP_ERR_INVALID_ARG);
}

TEST_F(E1003Touch, SleepHoldsControllerInResetAndStopsTouchReads) {
    board_hal_touch_enable();
    ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    EXPECT_TRUE(reset_held);
    EXPECT_EQ(reset_level,0);
    EXPECT_FALSE(board_hal_touch_available());
    board_touch_sample_t point{};
    EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_SUPPORTED);
    EXPECT_TRUE(writes.empty());
}

TEST_F(E1003Touch, MissingControllerStillHeldOffDuringSleep) {
    present_address=0;
    board_hal_touch_enable();
    EXPECT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    EXPECT_TRUE(reset_held);
    EXPECT_EQ(reset_level,0);
}

TEST_F(E1003Touch, TimerBootDoesNotProbeOrReadTouchWhileHeldOff) {
    ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    board_touch_sample_t point{};
    for (int i=0;i<10;++i)
        EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_SUPPORTED);
    EXPECT_TRUE(reset_held);
    EXPECT_EQ(reset_level,0);
    EXPECT_FALSE(board_hal_touch_available());
    EXPECT_TRUE(writes.empty());
    EXPECT_EQ(probes,0);
    EXPECT_EQ(register_reads,0);
}

TEST_F(E1003Touch, FailedSharedBusDoesNotReleaseReset) {
    ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    i2c_bus=nullptr;
    EXPECT_EQ(board_hal_touch_enable(),ESP_ERR_INVALID_STATE);
    EXPECT_TRUE(reset_held);
    EXPECT_EQ(reset_level,0);
    EXPECT_EQ(probes,0);
}
