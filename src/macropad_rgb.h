#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t macropad_rgb_init(void);
esp_err_t macropad_rgb_set(uint8_t red, uint8_t green, uint8_t blue);
