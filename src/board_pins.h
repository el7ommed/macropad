#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define LCD_WIDTH 240
#define LCD_HEIGHT 240
#define LCD_HOST SPI2_HOST

#define LCD_CLK 7
#define LCD_MOSI 6
#define LCD_CS 14
#define LCD_DC 15
#define LCD_RST 21
#define LCD_BL 22

#define RGB_LED_PIN 8
#define MODE_BUTTON_PIN 9

#define SD_CS 4
#define SD_MOSI 6
#define SD_CLK 7
#define SD_MISO 5
