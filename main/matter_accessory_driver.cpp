/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <Arduino.h>
#include <esp_err.h>
#include "matter_accessory_driver.h"

esp_err_t relay_accessory_set_power(app_driver_handle_t driver_handle, uint8_t val) {
  uint32_t gpio_pin = (uint32_t)(uintptr_t)driver_handle;
  digitalWrite(gpio_pin, val ? HIGH : LOW);
  log_i("Relay on GPIO %d set to %d", gpio_pin, val);
  return ESP_OK;
}

app_driver_handle_t relay_accessory_init(uint8_t gpio_pin) {
  pinMode(gpio_pin, OUTPUT);
  digitalWrite(gpio_pin, LOW); // Default off
  return (app_driver_handle_t)(uintptr_t)gpio_pin;
}
