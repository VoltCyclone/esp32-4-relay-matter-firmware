#pragma once

#include <esp_err.h>
#include <sdkconfig.h>

// Relay Pins
#define RELAY_PIN_1 10
#define RELAY_PIN_2 11
#define RELAY_PIN_3 22
#define RELAY_PIN_4 23

#define LED_PIN 1

// Set your board button pin here
#ifndef CONFIG_BUTTON_PIN
#define BUTTON_PIN 0
#else
#define BUTTON_PIN CONFIG_BUTTON_PIN
#endif

#define DEFAULT_POWER true

typedef void *app_driver_handle_t;

esp_err_t relay_accessory_set_power(app_driver_handle_t driver_handle, uint8_t val);
app_driver_handle_t relay_accessory_init(uint8_t gpio_pin);
