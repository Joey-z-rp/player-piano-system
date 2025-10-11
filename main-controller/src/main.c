#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_device.h"
#include "string.h"

// WS2812 RGB LED configuration
#define WS2812_PIN GPIO_NUM_48 // Data pin for WS2812 LED

static const char *TAG = "ble_midi_controller";

// RMT handle
static rmt_channel_handle_t ws2812_rmt_handle = NULL;

// BLE MIDI Service UUIDs (128-bit UUIDs in little-endian format)
// Correct BLE MIDI Service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
#define MIDI_SERVICE_UUID_128 {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0x00, 0x03, 0x5A, 0x8E, 0x3B, 0x03}
// Correct BLE MIDI Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3
#define MIDI_CHARACTERISTIC_UUID_128 {0xF3, 0x06, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x38, 0x68, 0x00, 0xDB, 0xE5, 0x72, 0x77}

// BLE GATT Server
static uint16_t gatts_handle_table[2];
static uint16_t conn_id = 0;
static bool is_connected = false;
static bool advertising_started = false;

// Advertising parameters - standard BLE MIDI settings
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20, // 20ms minimum interval
    .adv_int_max = 0x40, // 40ms maximum interval
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// MIDI data buffer (removed unused variables)

// GATT profile
static esp_gatt_srvc_id_t service_id = {
    .is_primary = true,
    .id.inst_id = 0x00,
    .id.uuid.len = ESP_UUID_LEN_128,
    .id.uuid.uuid.uuid128 = MIDI_SERVICE_UUID_128};

// Characteristic ID (removed unused variable)

static esp_bt_uuid_t char_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid.uuid128 = MIDI_CHARACTERISTIC_UUID_128};

// Advertising data - minimal for maximum compatibility
static uint8_t adv_data[] = {
    0x02, 0x01, 0x06,                                                                               // Flags: LE General Discoverable Mode
    0x11, 0x07,                                                                                     // Complete list of 128-bit service UUIDs (17 bytes)
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0x00, 0x03, 0x5A, 0x8E, 0x3B, 0x03, // MIDI Service UUID
    0x04, 0x08,                                                                                     // Shortened local name (4 bytes)
    'M', 'I', 'D', 'I'};

// Scan response data for better discoverability
static uint8_t scan_rsp_data[] = {
    0x0A, 0x09, // Complete local name (10 bytes)
    'P', 'i', 'a', 'n', 'o', ' ', 'C', 't', 'r', 'l'};

// Forward declarations
static esp_err_t ws2812_init(void);
static esp_err_t ws2812_send_pixel(uint8_t r, uint8_t g, uint8_t b);
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

// MIDI message parsing functions
void parse_midi_message(uint8_t *data, uint8_t len)
{
  if (len < 2)
    return;

  uint8_t status = data[0];
  uint8_t channel = status & 0x0F;
  uint8_t command = status & 0xF0;

  switch (command)
  {
  case 0x80: // Note Off
    if (len >= 3)
    {
      printf("MIDI Note OFF - Channel: %d, Note: %d, Velocity: %d\n",
             channel, data[1], data[2]);
      ESP_LOGI(TAG, "Note OFF - Channel: %d, Note: %d, Velocity: %d",
               channel, data[1], data[2]);
      // Visual feedback - turn off LED
      ESP_ERROR_CHECK(ws2812_send_pixel(0, 0, 0));
    }
    break;

  case 0x90: // Note On
    if (len >= 3)
    {
      printf("MIDI Note ON - Channel: %d, Note: %d, Velocity: %d\n",
             channel, data[1], data[2]);
      ESP_LOGI(TAG, "Note ON - Channel: %d, Note: %d, Velocity: %d",
               channel, data[1], data[2]);
      // Visual feedback - turn on LED (green)
      ESP_ERROR_CHECK(ws2812_send_pixel(0, 255, 0));
    }
    break;

  case 0xB0: // Control Change
    if (len >= 3)
    {
      printf("MIDI Control Change - Channel: %d, Control: %d, Value: %d\n",
             channel, data[1], data[2]);
      ESP_LOGI(TAG, "Control Change - Channel: %d, Control: %d, Value: %d",
               channel, data[1], data[2]);
    }
    break;

  case 0xC0: // Program Change
    if (len >= 2)
    {
      printf("MIDI Program Change - Channel: %d, Program: %d\n",
             channel, data[1]);
      ESP_LOGI(TAG, "Program Change - Channel: %d, Program: %d",
               channel, data[1]);
    }
    break;

  case 0xE0: // Pitch Bend
    if (len >= 3)
    {
      int bend = (data[2] << 7) | data[1];
      printf("MIDI Pitch Bend - Channel: %d, Bend: %d\n", channel, bend);
      ESP_LOGI(TAG, "Pitch Bend - Channel: %d, Bend: %d", channel, bend);
    }
    break;

  case 0xF0: // System messages
    switch (status)
    {
    case 0xF8: // Clock
      printf("MIDI Clock\n");
      ESP_LOGI(TAG, "Clock");
      break;
    case 0xFA: // Start
      printf("MIDI Start\n");
      ESP_LOGI(TAG, "Start");
      break;
    case 0xFB: // Continue
      printf("MIDI Continue\n");
      ESP_LOGI(TAG, "Continue");
      break;
    case 0xFC: // Stop
      printf("MIDI Stop\n");
      ESP_LOGI(TAG, "Stop");
      break;
    case 0xFE: // Active Sensing
      printf("MIDI Active Sensing\n");
      ESP_LOGI(TAG, "Active Sensing");
      break;
    case 0xFF: // System Reset
      printf("MIDI System Reset\n");
      ESP_LOGI(TAG, "System Reset");
      break;
    default:
      printf("MIDI System Message: 0x%02X\n", status);
      ESP_LOGI(TAG, "System Message: 0x%02X", status);
      break;
    }
    break;

  default:
    printf("MIDI Unknown Command: 0x%02X\n", command);
    ESP_LOGI(TAG, "Unknown Command: 0x%02X", command);
    break;
  }
}

// Initialize RMT for WS2812
static esp_err_t ws2812_init(void)
{
  rmt_tx_channel_config_t tx_chan_config = {
      .gpio_num = WS2812_PIN,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10000000, // 10MHz
      .mem_block_symbols = 64,
      .trans_queue_depth = 4,
  };

  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &ws2812_rmt_handle));
  ESP_ERROR_CHECK(rmt_enable(ws2812_rmt_handle));

  return ESP_OK;
}

// Send WS2812 data using RMT
static esp_err_t ws2812_send_pixel(uint8_t r, uint8_t g, uint8_t b)
{
  // WS2812 expects GRB order
  uint32_t color = (b << 16) | (r << 8) | g;

  // Create RMT encoder for WS2812
  rmt_encoder_handle_t ws2812_encoder = NULL;
  rmt_bytes_encoder_config_t bytes_encoder_config = {
      .bit0 = {
          .level0 = 1,
          .duration0 = 4, // 0.4us at 10MHz
          .level1 = 0,
          .duration1 = 8, // 0.8us at 10MHz
      },
      .bit1 = {
          .level0 = 1,
          .duration0 = 8, // 0.8us at 10MHz
          .level1 = 0,
          .duration1 = 4, // 0.4us at 10MHz
      },
      .flags.msb_first = 1,
  };

  ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &ws2812_encoder));

  // Create transmission configuration
  rmt_transmit_config_t tx_config = {
      .loop_count = 0,
  };

  // Send the color data
  ESP_ERROR_CHECK(rmt_transmit(ws2812_rmt_handle, ws2812_encoder, &color, sizeof(color), &tx_config));
  ESP_ERROR_CHECK(rmt_tx_wait_all_done(ws2812_rmt_handle, 1000));

  // Clean up
  ESP_ERROR_CHECK(rmt_del_encoder(ws2812_encoder));

  return ESP_OK;
}

// BLE GAP event handler
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  ESP_LOGI(TAG, "GAP Event: %d", event);
  switch (event)
  {
  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ESP_LOGI(TAG, "Advertising data set complete, starting advertising immediately...");
    ESP_LOGI(TAG, "Starting BLE MIDI advertising with service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
    esp_err_t adv_start_ret = esp_ble_gap_start_advertising(&adv_params);
    if (adv_start_ret)
    {
      ESP_LOGE(TAG, "Failed to start advertising, error: %x", adv_start_ret);
    }
    else
    {
      ESP_LOGI(TAG, "Advertising start command sent successfully");
    }
    break;
  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
    {
      ESP_LOGE(TAG, "Advertising start failed, status: %d", param->adv_start_cmpl.status);
    }
    else
    {
      ESP_LOGI(TAG, "Advertising started successfully");
      advertising_started = true;
      // Get and log device address
      uint8_t device_addr[6];
      esp_ble_gap_get_local_used_addr(device_addr, BLE_ADDR_TYPE_PUBLIC);
      ESP_LOGI(TAG, "Device MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
               device_addr[0], device_addr[1], device_addr[2],
               device_addr[3], device_addr[4], device_addr[5]);
    }
    break;
  case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
    if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
    {
      ESP_LOGE(TAG, "Advertising stop failed");
    }
    else
    {
      ESP_LOGI(TAG, "Stop adv successfully");
    }
    break;
  case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
    ESP_LOGI(TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
             param->update_conn_params.status,
             param->update_conn_params.min_int,
             param->update_conn_params.max_int,
             param->update_conn_params.conn_int,
             param->update_conn_params.latency,
             param->update_conn_params.timeout);
    break;
  default:
    break;
  }
}

// BLE GATT server event handler
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
  switch (event)
  {
  case ESP_GATTS_REG_EVT:
    ESP_LOGI(TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
    gatts_handle_table[0] = 0;

    // Don't set GAP device name - use only raw advertising data
    // esp_ble_gap_set_device_name("Player Piano Controller");

    // Set advertising data
    ESP_LOGI(TAG, "Setting advertising data, length: %d", sizeof(adv_data));
    ESP_LOG_BUFFER_HEX(TAG, adv_data, sizeof(adv_data));
    ESP_LOGI(TAG, "BLE MIDI Service UUID in advertising: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
    esp_err_t adv_ret = esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));
    if (adv_ret)
    {
      ESP_LOGE(TAG, "config adv data failed, error code = %x", adv_ret);
    }
    else
    {
      ESP_LOGI(TAG, "Advertising data set successfully");
    }

    // Start advertising immediately instead of waiting for GAP event
    ESP_LOGI(TAG, "Starting advertising immediately...");
    esp_err_t adv_start_ret = esp_ble_gap_start_advertising(&adv_params);
    if (adv_start_ret)
    {
      ESP_LOGE(TAG, "Failed to start advertising immediately, error: %x", adv_start_ret);
    }
    else
    {
      ESP_LOGI(TAG, "Advertising start command sent immediately");
    }

    // Create GATT service
    esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 2);
    if (ret)
    {
      ESP_LOGE(TAG, "create service failed, error code = %x", ret);
    }
    break;
  case ESP_GATTS_READ_EVT:
    ESP_LOGI(TAG, "GATT_READ_EVT, conn_id %u, trans_id %u, handle %u", (unsigned int)param->read.conn_id, (unsigned int)param->read.trans_id, (unsigned int)param->read.handle);
    ESP_LOGI(TAG, "Synthesia is reading from the device - this is good!");
    break;
  case ESP_GATTS_WRITE_EVT:
    ESP_LOGI(TAG, "GATT_WRITE_EVT, conn_id %u, trans_id %u, handle %u", (unsigned int)param->write.conn_id, (unsigned int)param->write.trans_id, (unsigned int)param->write.handle);
    ESP_LOG_BUFFER_HEX(TAG, param->write.value, param->write.len);

    // Check if this is a CCCD write (handle 41 = CCCD)
    if (param->write.handle == 41)
    {
      if (param->write.len == 2 && param->write.value[0] == 0x01 && param->write.value[1] == 0x00)
      {
        ESP_LOGI(TAG, "CCCD enabled - notifications are now active!");
      }
      else
      {
        ESP_LOGI(TAG, "CCCD write: %02x %02x", param->write.value[0], param->write.value[1]);
      }
    }

    if (!param->write.is_prep)
    {
      ESP_LOGI(TAG, "GATT_WRITE_EVT, value = %s", param->write.value);
      // Parse MIDI data
      parse_midi_message(param->write.value, param->write.len);
    }
    break;
  case ESP_GATTS_EXEC_WRITE_EVT:
    ESP_LOGI(TAG, "GATT_EXEC_WRITE_EVT, conn_id %u, trans_id %u", (unsigned int)param->exec_write.conn_id, (unsigned int)param->exec_write.trans_id);
    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
    break;
  case ESP_GATTS_MTU_EVT:
    ESP_LOGI(TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
    break;
  case ESP_GATTS_CONF_EVT:
    ESP_LOGI(TAG, "ESP_GATTS_CONF_EVT, status %d", param->conf.status);
    if (param->conf.status != ESP_GATT_OK)
    {
      ESP_LOGE(TAG, "send confirm failed, error status = %x", param->conf.status);
    }
    break;
  case ESP_GATTS_START_EVT:
    ESP_LOGI(TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
    break;
  case ESP_GATTS_CONNECT_EVT:
    ESP_LOGI(TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x", param->connect.conn_id,
             param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
             param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
    conn_id = param->connect.conn_id;
    is_connected = true;
    ESP_LOGI(TAG, "Device connected! Synthesia should now see this as a MIDI device.");
    break;
  case ESP_GATTS_DISCONNECT_EVT:
    ESP_LOGI(TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
    is_connected = false;
    esp_ble_gap_start_advertising(&adv_params);
    break;
  case ESP_GATTS_CREATE_EVT:
    ESP_LOGI(TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d", param->create.status, param->create.service_handle);
    gatts_handle_table[0] = param->create.service_handle;

    // Create GATT characteristic with proper BLE MIDI properties
    esp_err_t ret2 = esp_ble_gatts_add_char(gatts_handle_table[0], &char_uuid,
                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                                            NULL, NULL);
    if (ret2)
    {
      ESP_LOGE(TAG, "add char failed, error code = %x", ret2);
    }
    else
    {
      ESP_LOGI(TAG, "BLE MIDI characteristic added successfully");
    }
    break;
  case ESP_GATTS_ADD_INCL_SRVC_EVT:
    break;
  case ESP_GATTS_ADD_CHAR_EVT:
    ESP_LOGI(TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d, char_uuid %x",
             param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle, param->add_char.char_uuid.uuid.uuid16);
    gatts_handle_table[1] = param->add_char.attr_handle;

    // Add Client Characteristic Configuration Descriptor (CCCD) for notifications
    esp_bt_uuid_t cccd_uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG};

    esp_gatt_perm_t cccd_perm = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE;

    esp_err_t cccd_ret = esp_ble_gatts_add_char_descr(gatts_handle_table[0], &cccd_uuid, cccd_perm, NULL, NULL);
    if (cccd_ret)
    {
      ESP_LOGE(TAG, "add CCCD failed, error code = %x", cccd_ret);
    }
    else
    {
      ESP_LOGI(TAG, "CCCD added successfully");
    }

    // Start the service
    esp_err_t ret3 = esp_ble_gatts_start_service(gatts_handle_table[0]);
    if (ret3)
    {
      ESP_LOGE(TAG, "start service failed, error code = %x", ret3);
    }
    break;
  case ESP_GATTS_ADD_CHAR_DESCR_EVT:
    ESP_LOGI(TAG, "ADD_CHAR_DESCR_EVT, status %d, attr_handle %d, service_handle %d, descr_uuid %x",
             param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle, param->add_char_descr.descr_uuid.uuid.uuid16);
    break;
  case ESP_GATTS_DELETE_EVT:
    break;
  case ESP_GATTS_STOP_EVT:
    break;
  case ESP_GATTS_OPEN_EVT:
    break;
  case ESP_GATTS_CANCEL_OPEN_EVT:
    break;
  case ESP_GATTS_CLOSE_EVT:
    break;
  case ESP_GATTS_LISTEN_EVT:
    break;
  case ESP_GATTS_CONGEST_EVT:
    break;
  case ESP_GATTS_UNREG_EVT:
    break;
  default:
    break;
  }
}

// GATT profile definition (removed unused variable)

void app_main(void)
{
  // Set log level to INFO to see all logs
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set(TAG, ESP_LOG_DEBUG);

  ESP_LOGI(TAG, "Player Piano BLE MIDI Controller started!");
  printf("Player Piano BLE MIDI Controller!\n");
  printf("Device will appear as 'Player Piano Controller' in BLE MIDI apps\n");
  printf("Connect with Synthesia or other MIDI apps to play notes!\n\n");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize WS2812 RMT driver for visual feedback
  ESP_ERROR_CHECK(ws2812_init());

  // Turn LED off initially
  ESP_ERROR_CHECK(ws2812_send_pixel(0, 0, 0));

  // Initialize Bluetooth
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret)
  {
    ESP_LOGE(TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
    return;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret)
  {
    ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
    return;
  }

  ret = esp_bluedroid_init();
  if (ret)
  {
    ESP_LOGE(TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
    return;
  }

  ret = esp_bluedroid_enable();
  if (ret)
  {
    ESP_LOGE(TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
    return;
  }

  // Set static address for consistent MAC
  uint8_t static_addr[6] = {0x70, 0x05, 0xCB, 0x3F, 0x18, 0x61};
  ret = esp_ble_gap_set_rand_addr(static_addr);
  if (ret)
  {
    ESP_LOGW(TAG, "Failed to set static address: %x", ret);
  }
  else
  {
    ESP_LOGI(TAG, "Static address set: %02x:%02x:%02x:%02x:%02x:%02x",
             static_addr[0], static_addr[1], static_addr[2],
             static_addr[3], static_addr[4], static_addr[5]);
  }

  // Register GATT callback
  ret = esp_ble_gatts_register_callback(gatts_profile_event_handler);
  if (ret)
  {
    ESP_LOGE(TAG, "gatts register error, error code = %x", ret);
    return;
  }

  // Register GAP callback
  ret = esp_ble_gap_register_callback(gap_event_handler);
  if (ret)
  {
    ESP_LOGE(TAG, "gap register error, error code = %x", ret);
    return;
  }

  // Register GATT application
  ret = esp_ble_gatts_app_register(0);
  if (ret)
  {
    ESP_LOGE(TAG, "gatts app register error, error code = %x", ret);
    return;
  }

  // MTU size will be negotiated automatically during connection
  ESP_LOGI(TAG, "MTU will be negotiated during connection");

  // Note: GATT service and characteristic creation will be handled in the callback
  // when the GATTS_REG_EVT event is received

  ESP_LOGI(TAG, "BLE MIDI initialized successfully!");
  printf("BLE MIDI is ready! Looking for connections...\n");
  printf("Open Synthesia on your phone and connect to 'Player Piano Controller'\n\n");

  // Main loop
  int loop_count = 0;
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
    loop_count++;

    // Fallback: try to start advertising if it hasn't started after 5 seconds
    if (!advertising_started && loop_count >= 5)
    {
      ESP_LOGW(TAG, "Advertising not started, attempting fallback...");
      esp_err_t ret = esp_ble_gap_start_advertising(&adv_params);
      if (ret)
      {
        ESP_LOGE(TAG, "Fallback advertising start failed: %x", ret);
      }
      else
      {
        ESP_LOGI(TAG, "Fallback advertising start command sent");
      }
    }

    ESP_LOGI(TAG, "BLE MIDI loop - Advertising: %s, Connected: %s",
             advertising_started ? "Active" : "Inactive", is_connected ? "Yes" : "No");

    // Every 10 seconds, log the device info for debugging
    if (loop_count % 10 == 0)
    {
      ESP_LOGI(TAG, "=== DEVICE INFO ===");
      ESP_LOGI(TAG, "Device Name: MIDI");
      ESP_LOGI(TAG, "MAC Address: 30:08:cb:3f:44:63");
      ESP_LOGI(TAG, "Service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
      ESP_LOGI(TAG, "Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3");
      ESP_LOGI(TAG, "Advertising: %s", advertising_started ? "YES" : "NO");
      ESP_LOGI(TAG, "==================");
    }
  }
}