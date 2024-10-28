/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ssd1306.h"
#include "wifi.h"
#include "timeMgmt.h"
#include "mqtt.h"
#include <driver/adc.h>
#include "main.h"
#include <sys/time.h>
#include <platform_api.c>
#include <platform_api.h>

#define LIGHT_SIGNAL_1 2 // 5 ROOM
#define LIGHT_SIGNAL_2 4 // 6 OUTSIDE

// GUIDE FOR barriersStatus:
//  0 - 00 - No activated barrier
//  1 - 01 - Only outer barrier is broken
//  2 - 10 - Only inside barrier is broken
//  3 - 11 - Both barriers activated

// event object holds barrier status and the time of the status
struct Event
{
    double timestamp;
    int barriersStatus;
} event;

// to put events and process them
static QueueHandle_t xDetectionQueue;
// number of the people in seminar room
volatile uint8_t counter = 0;
// prediction of the counter
volatile uint8_t prediction = 0;

static const char *TAG1 = "TEST_SENSOR";

static TaskHandle_t xReceiveSignalHandle;
static TaskHandle_t xProcessSignalHandle;

static const char *IOT_PLATFORM_TOKEN = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2ODc2MjI1ODMsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiMzEvNzgifQ.PGdwz1vCHsUPBe69bXF0teqEJ3jlt3UbjD4fBOG36XyQpc73G5xq-9rx3cWph0iMYzRxLL-D4mmfgo8zA0gA5NrSAlThF1GxMvd3mK3oXdANHpoi8WKh-l6wIF2u_KAL40xRvJr0KKcdCps0uNG9qLPaDtBYjtEsMJyXWTEVvMTsdHrNpLlkZ81g2EZtuko5_bOaZi-tpmRA9SwCi-ka_HoYq-TzAxPNsZ1grTu9V3vQFsf91VSbRUdQ0XtyNuuHvwQzC24kT_isadGhuVVX3pMJxcahwgJLLY6-aFg9m_Mpgfmw4hspPxNRkTK_tiPxSLWHpQDUsnI_kEaLbccNGJhj9BSPBhmnGJmNthSVl8FAdi8c6VBYJ8_eEqI4f02ioXMR2I4bil9BpEYaB9rVX3TZ-wlksvRJldv8ecd3i-vCeTPdMDem8tvGD_IzcMZ3l-7JgbzJN8xaVR4EDaZxTku9yKCR8CmIDWFpvnmDLC6afQ9QZQBZmRgnhAhukubCNiKtnd2R20vFDroP4nBZUkb8drDJwdzuVxlzLhP3TlfKu6Ds3FzlcwIngYq0gSdRL1GVwuMTQ8FTHOC-WmmeYzc8kJVifCVwNiw3XRVvHNVv1A7XXiNENaTcjYlFQ2jQ8DYS4Zms7QSbwFw_9XWQCh6fpWushvOMD3mhWs5pUzM";
static const char *IOT_PLATFORM_FETCH = "http://caps-platform.live:3000/api/users/31/config/device/fetch";

// to handle debouncing, put events only if they are different than the previous
static int prevState = 0;

int64_t get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec) * 1000LL + (tv.tv_usec / 1000LL);
}

static int state = 0;

void decreaseCounter()
{
    if (counter > 0)
    {
        counter--;
    }
}

// a b c d e f g
// 0 1 2 3 4 5 6
// function to decide on the next state
// FSM can be found in report
void stateTransition(int input)
{

    if (state == 0 && input == 1)
    {
        state = 1;
    }
    else if (state == 1 && input == 3)
    {
        state = 2;
    }
    else if (state == 2 && input == 1)
    {
        state = 1;
    }
    else if (state == 2 && input == 2)
    {
        state = 3;
    }
    else if (state == 3 && input == 3)
    {
        state = 2;
    }
    else if (state == 3 && input == 0)
    {
        state = 0;
        counter++;
        ESP_LOGI(TAG1, "Someone entered the room! Maow!");
    }

    // a b c d e f g
    // 0 1 2 3 4 5 6

    else if (state == 0 && input == 2)
    {
        state = 4;
    }
    else if (state == 4 && input == 3)
    {
        state = 5;
    }
    else if (state == 5 && input == 1)
    {
        state = 6;
    }
    else if (state == 6 && input == 0)
    {
        state = 0;
        decreaseCounter();
        ESP_LOGI(TAG1, "Someone left the room!");
    }
    else if (state == 5 && input == 2)
    {
        state = 4;
    }
    else if (state == 6 && input == 3)
    {
        state = 5;
    }

    // impossible corner case
    // 01 -> 10 or 10 -> 01 happens
    else if (state == 1 && input == 2)
    {
        state = 4;
    }
    else if (state == 4 && input == 1)
    {
        state = 1;
    }

    else if (input == 0)
    {
        state = 0;
    }
    else
    {
        state = 0;
    }
}

// MQTT message sending function
void vSendMessages()
{
    for (;;)
    {
        char *mqtt_msg;
        int qos_test = 1;
        int bytes = asprintf(&mqtt_msg, "{\"sensors\": [{\"name\": \"%s\", \"values\": [{\"timestamp\":%lld, \"count\":%d}]}]}", "s2", get_timestamp(), counter);
        ESP_LOGI("MQTT_SEND", "Topic %s: %s\n", TOPIC, mqtt_msg);
        int msg_id = esp_mqtt_client_publish(mqttClient, TOPIC, mqtt_msg, bytes, qos_test, 0);
        if (msg_id == -1)
        {
            ESP_LOGE(TAG1, "msg_id returned by publish is -1!\n");
        }

        vTaskDelay(300000 / portTICK_PERIOD_MS);
    }
}

// updates prediction by taking the value from IOT Platform
void vUpdatePrediction()
{
    for (;;)
    {
        platform_api_init(IOT_PLATFORM_FETCH);
        platform_api_set_token(IOT_PLATFORM_TOKEN);
        platform_api_set_query_string("type", "device");
        platform_api_set_query_string("deviceId", "78");
        platform_api_set_query_string("keys", "prediction");
        platform_api_perform_request();
        char *result;
        platform_api_retrieve_val("prediction", STRING, true, NULL, (void **)&result);
        prediction = atoi(result);

        platform_api_cleanup();

        vTaskDelay(300000 / portTICK_PERIOD_MS);
    }
}

// puts receiving states into queue by creating events only if the prev is not same
void vReceivingSignal(void *pvParameters)
{

    ESP_LOGI(TAG1, "Receiving test");
    struct Event signal;

    for (;;)
    {
        vTaskSuspend(NULL);
        if (gpio_get_level(LIGHT_SIGNAL_1) == 0 && gpio_get_level(LIGHT_SIGNAL_2) == 0 && prevState != 0)
        {
            struct Event tmp;
            tmp.barriersStatus = 0;
            tmp.timestamp = get_timestamp();
            ESP_LOGI(TAG1, "Sent: %d", tmp.barriersStatus);

            prevState = 0;
            xQueueSendToBack(xDetectionQueue, (void *)&tmp, (TickType_t)0);
        }
        else if (gpio_get_level(LIGHT_SIGNAL_1) == 1 && gpio_get_level(LIGHT_SIGNAL_2) == 1 && prevState != 3)
        {

            struct Event tmp;
            tmp.barriersStatus = 3;
            tmp.timestamp = get_timestamp();
            ESP_LOGI(TAG1, "Sent: %d", tmp.barriersStatus);

            prevState = 3;
            xQueueSendToBack(xDetectionQueue, (void *)&tmp, (TickType_t)0);
        }
        else if (gpio_get_level(LIGHT_SIGNAL_1) == 1 && gpio_get_level(LIGHT_SIGNAL_2) == 0 && prevState != 2)
        {
            struct Event tmp;
            tmp.barriersStatus = 2;
            tmp.timestamp = get_timestamp();
            ESP_LOGI(TAG1, "Sent: %d", tmp.barriersStatus);
            prevState = 2;

            xQueueSendToBack(xDetectionQueue, (void *)&tmp, (TickType_t)0);
        }
        else if (gpio_get_level(LIGHT_SIGNAL_1) == 0 && gpio_get_level(LIGHT_SIGNAL_2) == 1 && prevState != 1) // 01
        {
            struct Event tmp;
            tmp.barriersStatus = 1;
            tmp.timestamp = get_timestamp();
            ESP_LOGI(TAG1, "Sent: %d", tmp.barriersStatus);
            prevState = 1;
            xQueueSendToBack(xDetectionQueue, (void *)&tmp, (TickType_t)0);
        }
    }
}

// processes the queue by calling state transition function
void vProcessingSignal(void *pvParameters)
{
    ESP_LOGI(TAG1, "Process test");
    struct Event signal;

    for (;;)
    {
        vTaskSuspend(NULL);
        if (xQueueReceive(xDetectionQueue, &signal, (TickType_t)10))
        {
            if (signal.barriersStatus == 0) // 00
            {
                stateTransition(0);
                ESP_LOGI(TAG1, "Counter: %d", counter);
            }
            else if (signal.barriersStatus == 1) // 01
            {
                stateTransition(1);
                ESP_LOGI(TAG1, "Counter: %d", counter);
            }
            else if (signal.barriersStatus == 2) // 10
            {
                stateTransition(2);
                ESP_LOGI(TAG1, "Counter: %d", counter);
            }
            else if (signal.barriersStatus == 3) // 11
            {
                stateTransition(3);
                ESP_LOGI(TAG1, "Counter: %d", counter);
            }
        }
    }
}

void IRAM_ATTR gpio_isr_handler(void *arg)
{
    xTaskResumeFromISR(xReceiveSignalHandle);
    xTaskResumeFromISR(xProcessSignalHandle);
}

#ifdef WITH_DISPLAY

void initDisplay()
{
    ssd1306_128x64_i2c_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
}

// print on screen
void textDemo()
{
    for (;;)
    {
        ssd1306_clearScreen();

        ssd1306_printFixedN(0, 8, "G2", STYLE_NORMAL, 1);

        time_t timer;
        char buffer[26];
        struct tm *tm_info;

        timer = time(NULL);
        tm_info = localtime(&timer);

        strftime(buffer, 26, "%H:%M", tm_info);

        ssd1306_printFixedN(48, 8, buffer, STYLE_NORMAL, 1);

        char str[16];
        sprintf(str, "%d", counter);
        ssd1306_printFixedN(0, 36, str, STYLE_NORMAL, 2);

        char predictionStr[16];
        sprintf(predictionStr, "%d", prediction);

        ssd1306_printFixedN(80, 36, predictionStr, STYLE_NORMAL, 2);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ssd1306_clearScreen();
    }
}
#endif

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT", ESP_LOG_INFO);
    esp_log_level_set("MQTT_SEND", ESP_LOG_INFO);
    esp_log_level_set("PROGRESS", ESP_LOG_INFO);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // initialize tools
    initWifi();
    initSNTP();
    initMQTT();

    ESP_LOGI(TAG1, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG1, "[APP] IDF version: %s", esp_get_idf_version());

    xDetectionQueue = xQueueCreate(1024, sizeof(event));

    gpio_pad_select_gpio(LIGHT_SIGNAL_1);
    gpio_pad_select_gpio(LIGHT_SIGNAL_2);
    ESP_ERROR_CHECK(gpio_set_direction(LIGHT_SIGNAL_1, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_direction(LIGHT_SIGNAL_2, GPIO_MODE_INPUT));

    gpio_set_intr_type(LIGHT_SIGNAL_1, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(LIGHT_SIGNAL_2, GPIO_INTR_ANYEDGE);

    gpio_install_isr_service(0);

    // signals binded to interrupt handlers
    gpio_isr_handler_add(LIGHT_SIGNAL_1, gpio_isr_handler, NULL);
    gpio_isr_handler_add(LIGHT_SIGNAL_2, gpio_isr_handler, NULL);

    ESP_LOGI(TAG1, "Counter: %d", counter);

    // task creation by assigning priority levels
    // reasoning of priority levels can be found in report
    xTaskCreate(vReceivingSignal, "Receiving Signal", 2048, NULL, 15, &xReceiveSignalHandle);
    xTaskCreate(vProcessingSignal, "Processing Signal", 2048, NULL, 14, &xProcessSignalHandle);
    xTaskCreate(vSendMessages, "Monitoring Signal", 2048, NULL, 10, NULL);
    xTaskCreate(vUpdatePrediction, "Update Prediction", 2048 * 4, NULL, 20, NULL);

#ifdef WITH_DISPLAY
    initDisplay();
    xTaskCreate(textDemo, "Display", 2048, NULL, 13, NULL);
#endif
}
