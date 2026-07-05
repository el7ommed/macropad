#include "macropad_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static const char *TAG = "MACROPAD_LCD";

#define LCD_PIXEL_CLOCK_HZ (12 * 1000 * 1000)
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define LCD_COLOR_INVERT true

#define LCD_BK_TIMER LEDC_TIMER_0
#define LCD_BK_CHANNEL LEDC_CHANNEL_0
#define LCD_BK_MODE LEDC_LOW_SPEED_MODE
#define LCD_BK_RESOLUTION LEDC_TIMER_13_BIT
#define LCD_BK_MAX_DUTY ((1 << LCD_BK_RESOLUTION) - 1)

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_framebuffer;

static const uint8_t s_font_digits[10][7] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
    {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
    {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
};

static const uint8_t s_font_letters[26][7] = {
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
    {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
    {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
    {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},
    {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
    {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
};

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((blue & 0xf8) << 8) | ((green & 0xfc) << 3) | (red >> 3);
}

static const uint8_t *glyph_for_char(char c)
{
    if (c >= 'a' && c <= 'z')
    {
        c = (char)(c - 'a' + 'A');
    }

    if (c >= 'A' && c <= 'Z')
    {
        return s_font_letters[c - 'A'];
    }

    if (c >= '0' && c <= '9')
    {
        return s_font_digits[c - '0'];
    }

    return NULL;
}

static size_t drawable_text_length(const char *word)
{
    size_t length = 0;

    for (const char *cursor = word; *cursor != '\0'; cursor++)
    {
        if (glyph_for_char(*cursor) || *cursor == ' ' || *cursor == '-')
        {
            length++;
        }
    }

    return length;
}

static int text_scale_for_length(size_t length)
{
    if (length == 0)
    {
        return 1;
    }

    int text_width = (int)(length * 6 - 1);
    int scale_x = (LCD_WIDTH - 24) / text_width;
    int scale_y = (LCD_HEIGHT - 24) / 7;
    int scale = scale_x < scale_y ? scale_x : scale_y;

    if (scale > 6)
    {
        scale = 6;
    }
    if (scale < 1)
    {
        scale = 1;
    }

    return scale;
}

static void fill_screen(uint16_t color)
{
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
    {
        s_framebuffer[i] = color;
    }
}

static void put_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT)
    {
        return;
    }

    s_framebuffer[y * LCD_WIDTH + x] = color;
}

static void draw_char(char c, int origin_x, int origin_y, int scale, uint16_t color)
{
    const uint8_t *glyph = glyph_for_char(c);

    if (c == '-')
    {
        for (int row = 3 * scale; row < 4 * scale; row++)
        {
            for (int col = 0; col < 5 * scale; col++)
            {
                put_pixel(origin_x + col, origin_y + row, color);
            }
        }
        return;
    }

    if (!glyph)
    {
        return;
    }

    for (int row = 0; row < 7; row++)
    {
        for (int col = 0; col < 5; col++)
        {
            if ((glyph[row] & (1 << (4 - col))) == 0)
            {
                continue;
            }

            for (int dy = 0; dy < scale; dy++)
            {
                for (int dx = 0; dx < scale; dx++)
                {
                    put_pixel(origin_x + col * scale + dx, origin_y + row * scale + dy, color);
                }
            }
        }
    }
}

static void draw_word(const char *word, uint16_t color)
{
    size_t length = drawable_text_length(word);
    int scale = text_scale_for_length(length);
    int text_width = (int)(length * 6 - 1) * scale;
    int text_height = 7 * scale;
    int cursor_x = (LCD_WIDTH - text_width) / 2;
    int origin_y = (LCD_HEIGHT - text_height) / 2;

    for (const char *cursor = word; *cursor != '\0'; cursor++)
    {
        if (!glyph_for_char(*cursor) && *cursor != ' ' && *cursor != '-')
        {
            continue;
        }

        draw_char(*cursor, cursor_x, origin_y, scale, color);
        cursor_x += 6 * scale;
    }
}

static esp_err_t backlight_init(void)
{
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_BL,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LCD_BK_MODE,
        .duty_resolution = LCD_BK_RESOLUTION,
        .timer_num = LCD_BK_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num = LCD_BL,
        .speed_mode = LCD_BK_MODE,
        .channel = LCD_BK_CHANNEL,
        .timer_sel = LCD_BK_TIMER,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_ERROR_CHECK(ledc_set_duty(LCD_BK_MODE, LCD_BK_CHANNEL, (LCD_BK_MAX_DUTY * 75) / 100));
    return ledc_update_duty(LCD_BK_MODE, LCD_BK_CHANNEL);
}

esp_err_t macropad_display_init(void)
{
    s_framebuffer = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_framebuffer)
    {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_CLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = SD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_COLOR_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    return backlight_init();
}

esp_err_t macropad_display_show_word(const char *word, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_panel || !s_framebuffer)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t background = rgb565(0, 0, 0);
    uint16_t foreground = rgb565(255, 255, 255);

    fill_screen(background);
    draw_word(word, foreground);

    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, s_framebuffer);
}
