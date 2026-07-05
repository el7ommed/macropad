#include "macropad_rgb.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "driver/rmt_tx.h"

#define RGB_RMT_RESOLUTION_HZ 10000000

static rmt_channel_handle_t s_led_channel;
static rmt_encoder_handle_t s_led_encoder;
static uint8_t s_led_pixel[3];

static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .duration0 = 0.3 * RGB_RMT_RESOLUTION_HZ / 1000000,
    .level1 = 0,
    .duration1 = 0.9 * RGB_RMT_RESOLUTION_HZ / 1000000,
};

static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .duration0 = 0.9 * RGB_RMT_RESOLUTION_HZ / 1000000,
    .level1 = 0,
    .duration1 = 0.3 * RGB_RMT_RESOLUTION_HZ / 1000000,
};

static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .duration0 = RGB_RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = RGB_RMT_RESOLUTION_HZ / 1000000 * 50 / 2,
};

static size_t ws2812_encoder_callback(const void *data, size_t data_size, size_t symbols_written, size_t symbols_free, rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    if (symbols_free < 8)
    {
        return 0;
    }

    size_t data_pos = symbols_written / 8;
    const uint8_t *data_bytes = (const uint8_t *)data;

    if (data_pos < data_size)
    {
        size_t symbol_pos = 0;

        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1)
        {
            symbols[symbol_pos++] = (data_bytes[data_pos] & bitmask) ? ws2812_one : ws2812_zero;
        }

        return symbol_pos;
    }

    symbols[0] = ws2812_reset;
    *done = true;
    return 1;
}

esp_err_t macropad_rgb_init(void)
{
    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RGB_LED_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = RGB_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channel_config, &s_led_channel));

    rmt_simple_encoder_config_t encoder_config = {
        .callback = ws2812_encoder_callback,
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&encoder_config, &s_led_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_led_channel));

    return macropad_rgb_set(0, 0, 0);
}

esp_err_t macropad_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
    s_led_pixel[0] = green;
    s_led_pixel[1] = red;
    s_led_pixel[2] = blue;

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(s_led_channel, s_led_encoder, s_led_pixel, sizeof(s_led_pixel), &transmit_config));
    return rmt_tx_wait_all_done(s_led_channel, -1);
}
