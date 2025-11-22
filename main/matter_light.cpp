/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <Arduino.h>
#include "matter_accessory_driver.h"

#include <esp_err.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include "esp_openthread_types.h"

#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() \
  { .radio_mode = RADIO_MODE_NATIVE, }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG() \
  { .host_connection_mode = HOST_CONNECTION_MODE_NONE, }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() \
  { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, }
#endif

// set your board button pin here
const uint8_t button_gpio = BUTTON_PIN;  // GPIO BOOT Button

uint16_t relay_endpoint_ids[4];
const uint8_t relay_pins[4] = {RELAY_PIN_1, RELAY_PIN_2, RELAY_PIN_3, RELAY_PIN_4};

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif  // CONFIG_ENABLE_ENCRYPTED_OTA

bool isAccessoryCommissioned() {
  return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
bool isWifiConnected() {
  return chip::DeviceLayer::ConnectivityMgr().IsWiFiStationConnected();
}
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
bool isThreadConnected() {
  return chip::DeviceLayer::ConnectivityMgr().IsThreadAttached();
}
#endif

#include <app/server/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

void PrintOnboardingCodesArduino() {
  chip::PayloadContents payload;
  CHIP_ERROR err = GetPayloadContents(payload, chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
  if (err != CHIP_NO_ERROR) {
    log_e("GetPayloadContents failed: %" CHIP_ERROR_FORMAT, err.Format());
    return;
  }

  char payloadBuffer[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1];
  chip::MutableCharSpan qrCode(payloadBuffer);

  if (GetQRCode(qrCode, payload) == CHIP_NO_ERROR) {
    log_i("SetupQRCode: [%.*s]", (int)qrCode.size(), qrCode.data());
    char qrCodeUrl[1024];
    if (GetQRCodeUrl(qrCodeUrl, sizeof(qrCodeUrl), qrCode) == CHIP_NO_ERROR) {
      log_i("Copy/paste the below URL in a browser to see the QR Code:");
      log_i("%s", qrCodeUrl);
    }
  }

  chip::MutableCharSpan manualPairingCode(payloadBuffer);
  if (GetManualPairingCode(manualPairingCode, payload) == CHIP_NO_ERROR) {
    log_i("Manual pairing code: [%.*s]", (int)manualPairingCode.size(), manualPairingCode.data());
  }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg) {
  switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
      log_i(
        "Interface %s Address changed", event->InterfaceIpAddressChanged.Type == chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned ? "IPv4" : "IPV6"
      );
      break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete: log_i("Commissioning complete"); break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired: log_i("Commissioning failed, fail safe timer expired"); break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted: log_i("Commissioning session started"); break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped: log_i("Commissioning session stopped"); break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
      log_i("Commissioning window opened");
      PrintOnboardingCodesArduino();
      break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed: log_i("Commissioning window closed"); break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
    {
      log_i("Fabric removed successfully");
      if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
        constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
        if (!commissionMgr.IsCommissioningWindowOpen()) {
          /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
          CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds, chip::CommissioningWindowAdvertisement::kDnssdOnly);
          if (err != CHIP_NO_ERROR) {
            log_e("Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
          }
        }
      }
      break;
    }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved: log_i("Fabric will be removed"); break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated: log_i("Fabric is updated"); break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted: log_i("Fabric is committed"); break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized: log_i("BLE deinitialized and memory reclaimed"); break;

    default: break;
  }
}

esp_err_t matter_relay_attribute_update(
  app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val
) {
  esp_err_t err = ESP_OK;
  if (cluster_id == OnOff::Id) {
    if (attribute_id == OnOff::Attributes::OnOff::Id) {
      err = relay_accessory_set_power(driver_handle, val->val.b);
    }
  }
  return err;
}

esp_err_t matter_relay_set_defaults(uint16_t endpoint_id) {
  esp_err_t err = ESP_OK;

  void *priv_data = endpoint::get_priv_data(endpoint_id);
  node_t *node = node::get();
  endpoint_t *endpoint = endpoint::get(node, endpoint_id);
  cluster_t *cluster = NULL;
  attribute_t *attribute = NULL;
  esp_matter_attr_val_t val = esp_matter_invalid(NULL);

  /* Setting power */
  cluster = cluster::get(endpoint, OnOff::Id);
  attribute = attribute::get(cluster, OnOff::Attributes::OnOff::Id);
  attribute::get_val(attribute, &val);
  err |= relay_accessory_set_power(priv_data, val.val.b);

  return err;
}

void button_driver_init() {
  /* Initialize button */
  pinMode(button_gpio, INPUT_PULLUP);
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(
  attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data
) {
  esp_err_t err = ESP_OK;

  if (type == PRE_UPDATE) {
    /* Driver update */
    app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
    err = matter_relay_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
  }

  return err;
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id, uint8_t effect_variant, void *priv_data) {
  log_i("Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
  return ESP_OK;
}

enum LedState {
  LED_OFF,
  LED_COMMISSIONING,
  LED_CONNECTING,
  LED_RUNNING
};

LedState g_led_state = LED_OFF;

void update_led() {
  static uint32_t last_update = 0;
  static int brightness = 0;
  static int fade_amount = 5;
  static bool led_on = false;
  
  uint32_t now = millis();
  
  switch (g_led_state) {
    case LED_COMMISSIONING:
      if (now - last_update > 200) {
        last_update = now;
        led_on = !led_on;
        analogWrite(LED_PIN, led_on ? 255 : 0);
      }
      break;
    case LED_CONNECTING:
      if (now - last_update > 1000) {
        last_update = now;
        led_on = !led_on;
        analogWrite(LED_PIN, led_on ? 255 : 0);
      }
      break;
    case LED_RUNNING:
      if (now - last_update > 20) { 
        last_update = now;
        brightness += fade_amount;
        if (brightness <= 0 || brightness >= 255) {
          fade_amount = -fade_amount;
        }
        if (brightness < 0) brightness = 0;
        if (brightness > 255) brightness = 255;
        analogWrite(LED_PIN, brightness);
      }
      break;
    case LED_OFF:
      analogWrite(LED_PIN, 0);
      break;
  }
}

void setup() {
  esp_err_t err = ESP_OK;

  button_driver_init();
  pinMode(LED_PIN, OUTPUT);

  /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
  node::config_t node_config;

  // node handle can be used to add/modify other endpoints.
  node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
  if (node == nullptr) {
    log_e("Failed to create Matter node");
    abort();
  }

  on_off_plugin_unit::config_t relay_config;
  relay_config.on_off.on_off = DEFAULT_POWER;
  relay_config.on_off.lighting.start_up_on_off = nullptr;

  for (int i = 0; i < 4; i++) {
    app_driver_handle_t relay_handle = relay_accessory_init(relay_pins[i]);
    endpoint_t *endpoint = on_off_plugin_unit::create(node, &relay_config, ENDPOINT_FLAG_NONE, relay_handle);
    if (endpoint == nullptr) {
      log_e("Failed to create relay endpoint %d", i);
      abort();
    }
    relay_endpoint_ids[i] = endpoint::get_id(endpoint);
    log_i("Relay %d created with endpoint_id %d", i, relay_endpoint_ids[i]);
  }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
  /* Set OpenThread platform config */
  esp_openthread_platform_config_t config = {
    .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
    .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
    .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
  };
  set_openthread_platform_config(&config);
#endif

  /* Matter start */
  err = esp_matter::start(app_event_cb);
  if (err != ESP_OK) {
    log_e("Failed to start Matter, err:%d", err);
    abort();
  }

#if CONFIG_ENABLE_ENCRYPTED_OTA
  err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
  if (err != ESP_OK) {
    log_e("Failed to initialized the encrypted OTA, err: %d", err);
    abort();
  }
#endif  // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
  esp_matter::console::diagnostics_register_commands();
  esp_matter::console::wifi_register_commands();
#if CONFIG_OPENTHREAD_CLI
  esp_matter::console::otcli_register_commands();
#endif
  esp_matter::console::init();
#endif
}

void loop() {
  static uint32_t button_time_stamp = 0;
  static bool button_state = false;
  static bool started = false;
  static uint32_t last_log_time = 0;

  bool commissioned = isAccessoryCommissioned();
  bool connected = true;

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
  if (!isWifiConnected()) connected = false;
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
  if (!isThreadConnected()) connected = false;
#endif

  if (!commissioned) {
    g_led_state = LED_COMMISSIONING;
    if (millis() - last_log_time > 5000) {
      log_w("Accessory not commissioned yet. Waiting for commissioning.");
      last_log_time = millis();
    }
  } else if (!connected) {
    g_led_state = LED_CONNECTING;
    if (millis() - last_log_time > 5000) {
      log_w("Network not connected yet. Waiting for connection.");
      last_log_time = millis();
    }
  } else {
    g_led_state = LED_RUNNING;
    // Once all network connections are established, the accessory is ready for use
    // Run it only once
    if (!started) {
      log_i("Accessory is commissioned and connected. Ready for use.");
      started = true;
      // Starting driver with default values
      for (int i = 0; i < 4; i++) {
        matter_relay_set_defaults(relay_endpoint_ids[i]);
      }
    }
  }

  update_led();

  // Check if the button is pressed and toggle the first relay right away
  if (digitalRead(button_gpio) == LOW && !button_state) {
    // deals with button debounce
    button_time_stamp = millis();  // record the time while the button is pressed.
    button_state = true;           // pressed.

    // Toggle button is pressed - toggle the first relay
    log_i("Toggle button pressed");

    endpoint_t *endpoint = endpoint::get(node::get(), relay_endpoint_ids[0]);
    cluster_t *cluster = cluster::get(endpoint, OnOff::Id);
    attribute_t *attribute = attribute::get(cluster, OnOff::Attributes::OnOff::Id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    val.val.b = !val.val.b;
    attribute::update(relay_endpoint_ids[0], OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
  }

  // Check if the button is released and handle the factory reset
  uint32_t time_diff = millis() - button_time_stamp;
  if (button_state && time_diff > 100 && digitalRead(button_gpio) == HIGH) {
    button_state = false;  // released. It can be pressed again after 100ms debounce.

    // Factory reset is triggered if the button is pressed for more than 10 seconds
    if (time_diff > 10000) {
      log_i("Factory reset triggered. Device will restored to factory settings.");
      esp_matter::factory_reset();
    }
  }

  delay(50);  // WDT is happier with a delay
}
