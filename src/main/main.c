#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "esp_system.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#define DEVICE_NAME "Glasshouse Station"
#define DEVICE_MANUFACTURER "Jacek Spławski"
#define DEVICE_SERIAL "12345678"
#define DEVICE_MODEL "1"
#define DEVICE_FIRMWARE_REVISION "0.1"

#define DEFAULT_VREF 1128
#define NO_OF_SAMPLES 64  // Multisampling

#define DRY_VALUE 4095
#define WET_VALUE 1128

#define RAW_RANGE 4095
#define LOG_RANGE 5.0

/* 
 * Can run 'make menuconfig' to choose the led GPIO and sensors channels,
 * or you can edit the following line and set a number here.
 */
#define LED_GPIO CONFIG_LED_GPIO
#define SOIL_MOISTURE_CHANNEL CONFIG_SOIL_MOISTURE_CHANNEL
#define LIGHT_CHANNEL CONFIG_LIGHT_CHANNEL

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_atten_t atten = ADC_ATTEN_DB_11;

static const char *TAG = "glasshouse";

void on_wifi_ready();

/*
 * Homekit characteristics
 */

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER, DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model = HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, DEVICE_FIRMWARE_REVISION);

homekit_characteristic_t soil_moisture = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);
homekit_characteristic_t light = HOMEKIT_CHARACTERISTIC_(CURRENT_AMBIENT_LIGHT_LEVEL, 0);

/*
 * Identify
 */

void led_init() {
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 1);
}

void identify_task(void *_args)
{
    for (int i=0; i<3; i++) {
        for (int j=0; j<2; j++) {
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    gpio_set_level(LED_GPIO, 1);

    vTaskDelete(NULL);
}

void identify(homekit_value_t _value)
{
    printf("Identify\n");
    xTaskCreate(identify_task, "identify", 8000, NULL, 2, NULL);
}

/*
 * Homekit accessory
 */

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id = 1, 
        .category = homekit_accessory_category_sensor, 
        .services = (homekit_service_t *[]){
            HOMEKIT_SERVICE(
                ACCESSORY_INFORMATION, 
                .characteristics = (homekit_characteristic_t *[]){
                    &name, 
                    &manufacturer, 
                    &serial, 
                    &model, 
                    &revision, 
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, identify), 
                    NULL
                }
            ), 
            HOMEKIT_SERVICE(
                HUMIDITY_SENSOR,
                .primary=true, 
                .characteristics = (homekit_characteristic_t *[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "Soil Moisture Sensor"), 
                    &soil_moisture, 
                    NULL
                }
            ), 
            HOMEKIT_SERVICE(
                LIGHT_SENSOR,
                .primary=true, 
                .characteristics = (homekit_characteristic_t *[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "Light Sensor"), 
                    &light, 
                    NULL
                }
            ), 
            NULL
        }
    ),
    NULL
};

/*
 * Homekit server
 */

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111",
    .setupId="1QJ8",};

/*
 * Reset configuration
 */

void reset_configuration_task()
{
    // printf("Resetting Wifi Config\n");

    // wifi_config_reset();

    // vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Resetting HomeKit Config\n");

    homekit_server_reset();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    printf("Restarting\n");

    esp_restart();

    vTaskDelete(NULL);
}

void reset_configuration()
{
    printf("Resetting Glasshouse configuration\n");
    xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}

/*
 * Event handler
 */

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        on_wifi_ready();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        break;
    default:
        break;
    }
    return ESP_OK;
}

/*
 * Wi-Fi setup
 */

static void wifi_init()
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/*
 * Soil moisture sensor
 */

void soil_moisture_sensor_task(void *_args)
{
    while (1)
    {
        uint32_t adc_reading = 0;

        // Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++)
        {
            adc_reading += adc1_get_raw((adc1_channel_t)SOIL_MOISTURE_CHANNEL);
        }
        adc_reading /= NO_OF_SAMPLES;

        // Convert adc_reading to voltage in mV
        int voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

        // Convert adc_reading to percentage value
        float per_percent = (DRY_VALUE - WET_VALUE) / 100;
        float percentage = (adc_reading - WET_VALUE) / per_percent;

        percentage = 100 - percentage;
        percentage = percentage < 0 ? 0 : percentage;
        percentage = percentage > 100 ? 100 : percentage;

        printf("Soil moisture\tRaw: %d\tVoltage: %dmV\tPercentage: %.2f\n", adc_reading, voltage, percentage);

        soil_moisture.value.float_value = percentage;

        homekit_characteristic_notify(&soil_moisture, HOMEKIT_FLOAT(percentage));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void soil_moisture_sensor_init()
{
    xTaskCreate(soil_moisture_sensor_task, "Soil Moisture", 8000, NULL, 2, NULL);
}

/*
 * Light sensor
 */

void light_sensor_task(void *_args)
{
    while (1)
    {
        uint32_t adc_reading = 0;

        // Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++)
        {
            adc_reading += adc1_get_raw((adc1_channel_t)LIGHT_CHANNEL);
        }
        adc_reading /= NO_OF_SAMPLES;

        // Convert adc_reading to voltage in mV
        int voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

        // Convert adc_reading to lux value
        float lux = adc_reading * LOG_RANGE / RAW_RANGE;
        lux = powf(10, lux);

        printf("Light\t\tRaw: %d\tVoltage: %dmV\tLux: %.2f\n", adc_reading, voltage, lux);

        light.value.float_value = lux;

        homekit_characteristic_notify(&light, HOMEKIT_FLOAT(lux));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void light_sensor_init()
{
    xTaskCreate(light_sensor_task, "Light", 8000, NULL, 2, NULL);
}

/*
 * Initialize Homekit server and sensors
 */

void on_wifi_ready() {
    homekit_server_init(&config);

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    soil_moisture_sensor_init();
    light_sensor_init();
}

/*
 * App main
 */

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SOIL_MOISTURE_CHANNEL, atten);
    adc1_config_channel_atten(LIGHT_CHANNEL, atten);

    // Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    // Wi-Fi initialize
    wifi_init();

    // Led initialize
    led_init();
}
