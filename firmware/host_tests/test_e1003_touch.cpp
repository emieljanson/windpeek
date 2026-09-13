#include <gtest/gtest.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>
#include "esp_err.h"
#include "../components/board_hal/include/board_touch.h"
#include "wind_overview.h"

// Exercise the actual register driver against a deterministic GT911 bus.
using i2c_master_dev_handle_t = void *;
static void *i2c_bus;
static std::array<uint8_t,65536> registers;
static int present_address, failed_register, acknowledgements, resets;
static bool supports_doze;
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
static void gpio_hold_dis(int) {}
static void gpio_config(const gpio_config_t *) {}
static void gpio_set_level(int pin,int level) {
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
static esp_err_t i2c_master_probe(void *,int address,int) { return address==present_address?ESP_OK:ESP_FAIL; }
static esp_err_t i2c_master_bus_add_device(void *,const i2c_device_config_t *,void **device) { *device=&registers;return ESP_OK; }
static void i2c_master_bus_rm_device(void *) {}
static esp_err_t i2c_master_transmit_receive(void *,const void *address,size_t,void *data,size_t size,int) {
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
    if(reg==0x8040 && bytes[2]==8 && supports_doze) {
        std::memcpy(registers.data()+0x8140,"GEST",4);
        registers[0x8146]=0x50; registers[0x8147]=7; // 1872
        registers[0x8148]=0x7c; registers[0x8149]=5; // 1404
    }
    if(reg==0x814e)acknowledgements++;
    return ESP_OK;
}
#include "../components/board_hal/src/e1003_touch.inc"

class E1003Touch : public testing::Test {
protected:
    void SetUp() override {
        registers.fill(0); present_address=0x5d;failed_register=-1;acknowledgements=resets=0;
        touch_device=nullptr;touch_width=touch_height=0;
        supports_doze=true; writes.clear();
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
TEST_F(E1003Touch, RetainsWakeContactWithoutResettingController) {
    contact(1,936,702);touch_init();
    ASSERT_TRUE(board_hal_touch_available()); EXPECT_EQ(resets,0);
    board_touch_sample_t point{};ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);
    EXPECT_EQ(point.contacts,1u);EXPECT_EQ(point.x,936);EXPECT_EQ(point.y,702);
    EXPECT_EQ(acknowledgements,1);EXPECT_EQ(registers[0x814e],0);
    EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_FINISHED);
}
TEST_F(E1003Touch, DetectsBothAddressesAndInterruptPolarities) {
    for(int address:{0x5d,0x14})for(int mode=0;mode<4;mode++) {
        present_address=address;registers[0x804d]=mode;touch_init();
        ASSERT_TRUE(board_hal_touch_available());EXPECT_EQ(board_hal_touch_wake_level(),mode==0||mode==3);
    }
}
TEST_F(E1003Touch, MapsDifferentResolutionAndDeliversRelease) {
    limits(1000,1000);touch_init();contact(1,500,500);
    board_touch_sample_t point{};ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);
    EXPECT_EQ(point.x,936);EXPECT_EQ(point.y,702);
    contact(0,0,0);ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);EXPECT_EQ(point.contacts,0u);
}
TEST_F(E1003Touch, RejectsInvalidPointsAndAcknowledgesFailedFrames) {
    touch_init();board_touch_sample_t point{};
    contact(1,1872,10);EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_INVALID_RESPONSE);
    contact(6,10,10);EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_INVALID_RESPONSE);
    contact(1,10,10);failed_register=0x814f;EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_TIMEOUT);
    EXPECT_EQ(acknowledgements,3);
}
TEST_F(E1003Touch, MissingControllerLeavesPhysicalControlsAvailable) {
    present_address=0;touch_init();EXPECT_FALSE(board_hal_touch_available());EXPECT_EQ(resets,1);
    board_touch_sample_t point{};EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_SUPPORTED);
    EXPECT_EQ(board_hal_touch_read(nullptr),ESP_ERR_INVALID_ARG);
}

TEST_F(E1003Touch, DozeEnablesOnlyDoubleTapAndPreservesPanelCalibration) {
    touch_init();
    registers[0x8067]=0x53;
    registers[0x8056]=0x25;
    ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    EXPECT_EQ(registers[0x8075],0);
    EXPECT_EQ(registers[0x8076],2);
    EXPECT_EQ(registers[0x8067],0x53);
    EXPECT_EQ(registers[0x8056],0xf5); // Hold wake HIGH; retain normal report rate.
    EXPECT_EQ(board_hal_touch_wake_level(),1);
    uint8_t checksum=0;
    for(int i=0x8047;i<=0x80ff;i++)checksum+=registers[i];
    EXPECT_EQ(checksum,0);
    ASSERT_GE(writes.size(),3u);
    EXPECT_LT(std::find(writes.begin(),writes.end(),0x8046),
              std::find(writes.begin(),writes.end(),0x8040));
    board_touch_sample_t point{};
    EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_FINISHED);
}

TEST_F(E1003Touch, GestureWakeIsConsumedBeforeResetAndNormalTouchResumes) {
    touch_init(); ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    registers[0x814b]=0xcc;
    touch_init();
    EXPECT_TRUE(board_hal_touch_gesture_wake());
    EXPECT_TRUE(board_hal_touch_double_tap());
    EXPECT_EQ(resets,1);
    contact(1,500,400);
    board_touch_sample_t point{};
    EXPECT_EQ(board_hal_touch_read(&point),ESP_OK);
    EXPECT_EQ(point.x,500);
}

TEST_F(E1003Touch, OtherGesturesAndTimerWakeDoNotBecomeDoubleTaps) {
    for(int code:{0,0xaa,0xbb,0x63}) {
        touch_init(); ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
        registers[0x814b]=code;
        touch_init();
        EXPECT_TRUE(board_hal_touch_gesture_wake());
        EXPECT_FALSE(board_hal_touch_double_tap());
    }
}

TEST_F(E1003Touch, UnsupportedDozeFallsBackToNormalTouchWake) {
    touch_init();supports_doze=false;
    EXPECT_NE(board_hal_touch_prepare_sleep(),ESP_OK);
    EXPECT_TRUE(board_hal_touch_available());
    EXPECT_EQ(board_hal_touch_wake_level(),0);
    EXPECT_GE(resets,1);
}

TEST_F(E1003Touch, FailedDozeCommandRestoresNormalTouchWake) {
    for(int reg:{0x8047,0x8046,0x8040}) {
        failed_register=-1;touch_init();failed_register=reg;
        EXPECT_NE(board_hal_touch_prepare_sleep(),ESP_OK);
        EXPECT_TRUE(board_hal_touch_available());
        EXPECT_EQ(board_hal_touch_wake_level(),0);
    }
}

TEST_F(E1003Touch, RetainsDoubleTapPositionAcrossControllerReset) {
    touch_init(); ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    registers[0x814b]=0xcc;
    registers[0x8151]=0xa8; registers[0x8152]=3; // x=936
    registers[0x8153]=0xbe; registers[0x8154]=2; // y=702
    touch_init();
    board_touch_sample_t point{};
    ASSERT_TRUE(board_hal_touch_wake_sample(&point));
    EXPECT_EQ(point.x,936); EXPECT_EQ(point.y,702);
    auto action=wind_overview_hit_test(point.x*800/1872,point.y*600/1404,true,1,10);
    EXPECT_EQ(action.kind,WIND_TOUCH_SELECT); EXPECT_EQ(action.spot_index,4u);
    EXPECT_FALSE(board_hal_touch_wake_sample(&point)); // Wake action is consumed once.
    EXPECT_EQ(board_hal_touch_read(&point),ESP_ERR_NOT_FINISHED);
    EXPECT_EQ(resets,1);
}

TEST_F(E1003Touch, InvalidGestureCoordinatesWakeWithoutAnAction) {
    for(int failure:{0,1,2}) {
        failed_register=-1; touch_init(); ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
        registers[0x814b]=0xcc;
        if(failure==0) registers[0x8152]=0xff;
        if(failure==1) registers[0x8146]=registers[0x8147]=0;
        if(failure==2) failed_register=0x8151;
        touch_init();
        board_touch_sample_t point{};
        EXPECT_TRUE(board_hal_touch_double_tap());
        EXPECT_FALSE(board_hal_touch_wake_sample(&point));
    }
}

TEST_F(E1003Touch, FailedGestureStatusReadRestoresCoordinateMode)
{
    touch_init(); ASSERT_EQ(board_hal_touch_prepare_sleep(),ESP_OK);
    registers[0x814b]=0xcc;
    failed_register=0x814b;
    touch_init();
    EXPECT_FALSE(board_hal_touch_gesture_wake());
    EXPECT_FALSE(board_hal_touch_double_tap());
    EXPECT_TRUE(board_hal_touch_available());
    EXPECT_EQ(resets,1);
    contact(1,500,400);
    board_touch_sample_t point{};
    ASSERT_EQ(board_hal_touch_read(&point),ESP_OK);
    EXPECT_EQ(point.x,500);
}

TEST_F(E1003Touch, FailedGestureProbeDoesNotAdvertiseAnAvailableController)
{
    std::memcpy(registers.data()+0x8140,"GEST",4);
    failed_register=0x814b;
    EXPECT_FALSE(touch_probe());
    EXPECT_FALSE(board_hal_touch_available());
}
