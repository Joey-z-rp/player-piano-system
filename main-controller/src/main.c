#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_PIN GPIO_NUM_48 // Built-in LED on ESP32-S3-DevKitC-1
#define BLINK_DELAY_MS 1000

static const char *TAG = "hello_world_blink";

void app_main(void)
{
  // Configure the LED pin as output
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << LED_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&io_conf);

  ESP_LOGI(TAG, "Hello World Blink started!");
  printf("Hello World! Starting LED blink...\n");

  int blink_count = 0;
  while (1)
  {
    // Turn LED on
    gpio_set_level(LED_PIN, 1);
    ESP_LOGI(TAG, "LED ON - Blink #%d", ++blink_count);

    // Wait
    vTaskDelay(pdMS_TO_TICKS(BLINK_DELAY_MS));

    // Turn LED off
    gpio_set_level(LED_PIN, 0);
    ESP_LOGI(TAG, "LED OFF");

    // Wait
    vTaskDelay(pdMS_TO_TICKS(BLINK_DELAY_MS));
  }
}