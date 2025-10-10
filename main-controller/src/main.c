#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_err.h"

// WS2812 RGB LED configuration
#define WS2812_PIN GPIO_NUM_48 // Data pin for WS2812 LED

static const char *TAG = "ws2812_rmt";

// RMT handle
static rmt_channel_handle_t ws2812_rmt_handle = NULL;

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

void app_main(void)
{
  ESP_LOGI(TAG, "WS2812 RMT Test started!");
  printf("WS2812 RMT Test! Testing on GPIO %d\n", WS2812_PIN);
  printf("Watch for LED activity!\n\n");

  // Initialize WS2812 RMT driver
  ESP_ERROR_CHECK(ws2812_init());

  // Test: Try to turn LED off first
  printf("Testing LED OFF...\n");
  ESP_ERROR_CHECK(ws2812_send_pixel(0, 0, 0));
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Test: Try to turn LED red
  printf("Testing LED RED...\n");
  ESP_ERROR_CHECK(ws2812_send_pixel(255, 0, 0));
  vTaskDelay(pdMS_TO_TICKS(2000));

  int color_count = 0;
  while (1)
  {
    // Cycle through different colors
    switch (color_count % 4)
    {
    case 0: // Red
      ESP_LOGI(TAG, "RED - Color #%d", ++color_count);
      printf("Setting RED color (R=255, G=0, B=0)\n");
      ESP_ERROR_CHECK(ws2812_send_pixel(255, 0, 0));
      break;
    case 1: // Green
      ESP_LOGI(TAG, "GREEN - Color #%d", ++color_count);
      printf("Setting GREEN color (R=0, G=255, B=0)\n");
      ESP_ERROR_CHECK(ws2812_send_pixel(0, 255, 0));
      break;
    case 2: // Blue
      ESP_LOGI(TAG, "BLUE - Color #%d", ++color_count);
      printf("Setting BLUE color (R=0, G=0, B=255)\n");
      ESP_ERROR_CHECK(ws2812_send_pixel(0, 0, 255));
      break;
    case 3: // White
      ESP_LOGI(TAG, "WHITE - Color #%d", ++color_count);
      printf("Setting WHITE color (R=255, G=255, B=255)\n");
      ESP_ERROR_CHECK(ws2812_send_pixel(255, 255, 255));
      break;
    }

    // Wait
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Turn LED off
    ESP_LOGI(TAG, "LED OFF");
    printf("Turning LED OFF\n");
    ESP_ERROR_CHECK(ws2812_send_pixel(0, 0, 0));

    // Wait
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}