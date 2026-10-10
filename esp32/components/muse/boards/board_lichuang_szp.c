/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Lichuang SZP ESP32-S3 N16R8: 2-inch 320x240 ST7789, FT6336 touch,
 * ES8311 speaker and ES7210 dual microphones. BOOT on GPIO0 is talk.
 * Vendor references (pins, panel orientation, expander and audio):
 * https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lcd-display.html
 * https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lvgl.html
 * https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-input-es7210.html
 * https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-output-es8311.html
 * https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/key.html
 * PCA9557 at 0x19 drives LCD_CS (bit 0), PA_EN (bit 1), and camera
 * power-down (bit 2). Camera stays powered down. Native USB is the console.
 */
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"

static const char *TAG = "board";

#define LCD_H_RES 320
#define LCD_V_RES 240
#define LCD_HOST SPI3_HOST
#define LCD_SCLK GPIO_NUM_41
#define LCD_MOSI GPIO_NUM_40
#define LCD_CS GPIO_NUM_NC    /* PCA9557 bit 0 */
#define LCD_DC GPIO_NUM_39
#define LCD_RST GPIO_NUM_NC
#define LCD_BL GPIO_NUM_42    /* active low: vendor PWM uses output_invert */
/* Keep internal RAM available for the 8 KB BLE provisioning stack and TLS. */
#define DRAW_BUF_LINES 8

#define I2C_SDA GPIO_NUM_1
#define I2C_SCL GPIO_NUM_2
#define TOUCH_ADDR 0x38
#define TOUCH_REG_TD_STATUS 0x02
#define TOUCH_REG_P1 0x03
#define EXP_ADDR 0x19
#define EXP_OUTPUT 0x01
#define EXP_CONFIG 0x03
#define EXP_LCD_CS BIT(0)
#define EXP_PA_EN BIT(1)
#define EXP_CAMERA_PWDN BIT(2)

#define I2S_MCLK GPIO_NUM_38
#define I2S_BCLK GPIO_NUM_14
#define I2S_DIN GPIO_NUM_12
#define I2S_DOUT GPIO_NUM_45
#define I2S_WS GPIO_NUM_13
#define MIC_ADDR 0x41        /* not the ES7210 default 0x40 */
#define TALK_GPIO GPIO_NUM_0

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_touch_dev;
static i2c_master_dev_handle_t s_exp;
static esp_lcd_panel_handle_t s_panel;
static muse_gpio_button_t s_talk;

static esp_err_t exp_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = { reg, value };
    return i2c_master_transmit(s_exp, data, sizeof(data), 100);
}

static esp_err_t init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");

    const i2c_device_config_t exp_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXP_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &exp_cfg, &s_exp), TAG, "expander i2c");
    /* Set output levels before enabling them, as in the vendor example. */
    ESP_RETURN_ON_ERROR(exp_write(EXP_OUTPUT, EXP_CAMERA_PWDN | EXP_LCD_CS), TAG, "expander output");
    ESP_RETURN_ON_ERROR(exp_write(EXP_CONFIG, 0xf8), TAG, "expander direction");
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_i2c, TOUCH_ADDR, 100), TAG, "FT6336 missing");
    const i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &touch_cfg, &s_touch_dev), TAG, "touch i2c");

    return muse_gpio_button_init(&s_talk, TALK_GPIO);
}

static esp_err_t touch_read_regs(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_touch_dev, &reg, 1, buf, n, 50);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t count = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    if (touch_read_regs(TOUCH_REG_TD_STATUS, &count, 1) != ESP_OK || (count & 0x0F) == 0) {
        return;
    }
    uint8_t p[4];
    if (touch_read_regs(TOUCH_REG_P1, p, sizeof(p)) != ESP_OK) {
        return;
    }
    int x = ((p[0] & 0x0F) << 8) | p[1];
    int y = ((p[2] & 0x0F) << 8) | p[3];
    if (x >= LCD_V_RES || y >= LCD_H_RES) {
        return;
    }
    /* esp_lcd_touch applies mirror_x before swap_xy in the vendor setup. */
    data->point.x = y;
    data->point.y = LCD_V_RES - 1 - x;
    data->state = LV_INDEV_STATE_PRESSED;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_ch = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .flags.output_invert = true,
    };
    if (ledc_timer_config(&bl_timer) != ESP_OK || ledc_channel_config(&bl_ch) != ESP_OK) {
        return NULL;
    }

    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * DRAW_BUF_LINES * 2,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return NULL;
    }
    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 2,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io) != ESP_OK) {
        return NULL;
    }
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel) != ESP_OK) {
        return NULL;
    }
    /* Prime SPI mode 2 while CS is high, as in the vendor's reset-before-CS
     * sequence. IDF applies the idle clock level on the first transaction. */
    if (esp_lcd_panel_reset(s_panel) != ESP_OK ||
        exp_write(EXP_OUTPUT, EXP_CAMERA_PWDN) != ESP_OK ||
        esp_lcd_panel_reset(s_panel) != ESP_OK) {
        return NULL;
    }
    /* ST7789 SWRESET in sleep-in needs 120 ms before SLPOUT. IDF waits 20 ms. */
    vTaskDelay(pdMS_TO_TICKS(120));
    if (esp_lcd_panel_init(s_panel) != ESP_OK ||
        esp_lcd_panel_invert_color(s_panel, true) != ESP_OK ||
        esp_lcd_panel_swap_xy(s_panel, true) != ESP_OK ||
        esp_lcd_panel_mirror(s_panel, true, false) != ESP_OK ||
        esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) {
        return NULL;
    }

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        return NULL;
    }
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_H_RES,
            .ver_res = LCD_V_RES,
            .buffer_height = DRAW_BUF_LINES,
            .use_psram = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp) {
        return NULL;
    }

    /* No esp_lcd_touch driver for the FT6336U in the registry setup here, so
     * register a plain LVGL pointer indev that polls it over I2C. */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);
    lv_indev_set_read_cb(indev, touch_read_cb);
    *touch = indev;

    if (esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return disp;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_disp_sleep(s_panel, sleep);   /* SLPIN/SLPOUT; GRAM is kept */
}

/* ES8311 playback and ES7210 capture share the duplex I2S clocks. */
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), TAG, "i2s channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s tx on");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "i2s rx on");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    audio_codec_i2c_cfg_t mic_i2c = { .port = I2C_NUM_0, .addr = MIC_ADDR << 1, .bus_handle = s_i2c };
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && ctrl_if && mic_ctrl && gpio_if, ESP_ERR_NO_MEM, TAG, "codec interfaces");

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,    /* PCA9557 controls the amplifier */
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "ES8311 not responding");

    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data_if };
    es7210_codec_cfg_t adc_cfg = {
        .ctrl_if = mic_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *adc = es7210_codec_new(&adc_cfg);
    ESP_RETURN_ON_FALSE(adc, ESP_FAIL, TAG, "ES7210 not responding");
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = adc, .data_if = data_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);
    ESP_RETURN_ON_FALSE(*spk && *mic, ESP_FAIL, TAG, "codec devices");
    return exp_write(EXP_OUTPUT, EXP_CAMERA_PWDN | EXP_PA_EN);
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk);
}

static esp_err_t power_off(void)
{
    set_brightness(0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    ESP_RETURN_ON_ERROR(exp_write(EXP_OUTPUT, EXP_CAMERA_PWDN), TAG, "amp off");
    while (gpio_get_level(TALK_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext0_wakeup(TALK_GPIO, 0), TAG, "wake button");
    esp_deep_sleep_start();
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = "Lichuang SZP ESP32-S3",
    .width = LCD_H_RES,
    .height = LCD_V_RES,
    .round = false,
    .touch = true,
    .diagonal_in = 2.0f,
    .talk_button = "boot",
    .talk_hint = { LV_ALIGN_TOP_LEFT, 8, 8 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .audio_init = audio_init,
    .mic_slot = 0,              /* MIC1; MIC2 is the second I2S slot */
    .poll_buttons = poll_buttons,
    .power_off = power_off,
};

/* Home Link's app_main starts Muse with this board (main/main.c). */
const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
