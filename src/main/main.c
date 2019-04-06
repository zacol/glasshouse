#include <stdio.h>
#include <stdlib.h>

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

#define DEFAULT_VREF 1128 // Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES 64  // Multisampling

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t gpio34 = ADC_CHANNEL_6;
static const adc_channel_t gpio35 = ADC_CHANNEL_7;
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

void identify_task(void *_args)
{
    vTaskDelete(NULL);
}

void identify(homekit_value_t _value)
{
    printf("Identify\n");
    // xTaskCreate(identify_task, "identify", 128, NULL, 2, NULL);
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
            adc_reading += adc1_get_raw((adc1_channel_t)gpio34);
        }
        adc_reading /= NO_OF_SAMPLES;

        // Convert adc_reading to voltage in mV
        int voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

        printf("Soil moisture\tRaw: %d\tVoltage: %dmV\n", adc_reading, voltage);

        soil_moisture.value.float_value = 10.00;

        homekit_characteristic_notify(&soil_moisture, HOMEKIT_FLOAT(10.00));

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
            adc_reading += adc1_get_raw((adc1_channel_t)gpio35);
        }
        adc_reading /= NO_OF_SAMPLES;

        // Convert adc_reading to voltage in mV
        int voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

        printf("Light\t\tRaw: %d\tVoltage: %dmV\n", adc_reading, voltage);

        light.value.float_value = 20.00;

        homekit_characteristic_notify(&light, HOMEKIT_FLOAT(20.00));

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
    adc1_config_channel_atten(gpio34, atten);
    adc1_config_channel_atten(gpio35, atten);

    // Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    // Wi-Fi initialize
    wifi_init();
}
