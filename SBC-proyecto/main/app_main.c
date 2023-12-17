/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <ultrasonic.h>
#include <ultrasonic.c>

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include <esp_err.h>
#include "esp_adc_cal.h"

#include <sys/time.h>
#include <time.h>

#include "sdkconfig.h"

#include "nvs_flash.h"

#include "soc/soc_caps.h"
#include "soc/rtc.h"

#include "protocol_examples_common.h"

#include "ulp.h"



#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"


#include "mqtt_client.h"

#include "cJSON.h"


#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/rtc_io.h"
#include "driver/ledc.h"


#include <inttypes.h>
#include "rc522.h"

#define LDR GPIO_NUM_34
#define VALUE_PORT GPIO_NUM_33

#define SERVO1_GPIO 13
#define SERVO2_GPIO 26

#define MOTOR1_PIN1 14
#define MOTOR1_PIN2 12

// Motor B
#define MOTOR2_PIN1 15
#define MOTOR2_PIN2 21

#define MAX_DISTANCE_CM 500 // 5m max
#define TRIGGER_GPIO 25
#define ECHO_GPIO 4

bool avoidObstacle = false;
int codigo;
static RTC_DATA_ATTR struct timeval sleep_enter_time;

static const char *TAG = "MQTT_EXAMPLE";

static const char *TAG3 = "servo";


//NFC
static const char* TAG1 = "rc522-demo";
static rc522_handle_t scanner;

static void rc522_handler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data)
{
    rc522_event_data_t* data = (rc522_event_data_t*) event_data;

    switch(event_id) {
        case RC522_EVENT_TAG_SCANNED: {
                rc522_tag_t* tag = (rc522_tag_t*) data->ptr;

                codigo = tag->serial_number;
                
                //ESP_LOGI(TAG, "Tag scanned (sn: %" PRIu64 ")", tag->serial_number);
                if(tag->serial_number == 1086381554325)
                    ESP_LOGI(TAG1, "Tag scanned (sn: %" PRIu64 "). A ti te toca el Ibuprofeno", tag->serial_number);

                    //printf("A ti te toca el Ibuprofeno");
                else if(tag->serial_number == 1006959714304)
                    ESP_LOGI(TAG1, "Tag scanned (sn: %" PRIu64 "). A ti te toca el Paracetamol", tag->serial_number);

                    //printf("A ti te toca el Paracetamol");
            }
            break;
    }
    
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void init_servo(int gpio_num, ledc_channel_t channel) {
    ledc_timer_config_t timer_conf = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ledc_conf = {
        .channel = channel,
        .duty = 0,
        .gpio_num = gpio_num,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0,
    };
    ledc_channel_config(&ledc_conf);
}

void set_servo_angle(ledc_channel_t channel, uint32_t duty_us) {
    uint32_t duty = (duty_us * (1 << LEDC_TIMER_13_BIT)) / 20000;
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, channel);
}

void motorControlTask(void *pvParameter)
{
    while (1)
    {
        if (avoidObstacle)
        {
            // Acción: Gira 90 grados a la izquierda
            gpio_set_level(MOTOR1_PIN1, 0);
            gpio_set_level(MOTOR1_PIN2, 0);

            gpio_set_level(MOTOR2_PIN1, 1);
            gpio_set_level(MOTOR2_PIN2, 0);
        }
        else
        {
            // Acción: Los dos motores giran hacia adelante

            printf("Los dos motores giran hacia adelante");
            gpio_set_level(MOTOR2_PIN1, 0);
            gpio_set_level(MOTOR2_PIN2, 1);
            
            
            gpio_set_level(MOTOR1_PIN1, 0);
            gpio_set_level(MOTOR1_PIN2, 1);

            
        }

        vTaskDelay(70 / portTICK_PERIOD_MS);
    }
}

void ultrasonicAndMotorControlTask(void *pvParameters)
{
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };

    ultrasonic_init(&sensor);

    while (true)
    {
        float distance;
        esp_err_t res = ultrasonic_measure(&sensor, MAX_DISTANCE_CM, &distance);

        if (res != ESP_OK)
        {
            printf("Error %d: ", res);
            switch (res)
            {
            case ESP_ERR_ULTRASONIC_PING:
                printf("Cannot ping (device is in an invalid state)\n");
                break;
            case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                printf("Ping timeout (no device found)\n");
                break;
            case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                printf("Echo timeout (i.e., distance too big)\n");
                break;
            default:
                printf("%s\n", esp_err_to_name(res));
            }
        }
        else
        {
            if (distance * 100 < 20)
            {
                // Acción: Gira 90 grados a la izquierda
                avoidObstacle = true;
                printf("Objeto detectado a menos de 20 cm. Girando a la izquierda.\n");
            }
            else
            {
                // Acción: Continúa con el movimiento normal
                avoidObstacle = false;
                printf("Distancia: %0.04f cm\n", distance * 100);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void servoTask(void *pvParameter)
{
    ESP_LOGI(TAG, "Configuring servos");

    init_servo(SERVO1_GPIO, LEDC_CHANNEL_0);
    init_servo(SERVO2_GPIO, LEDC_CHANNEL_1);

    while (1) {
        ESP_LOGI(TAG3, "Servo 1: Set angle to 0"); //se mueve el servo 1 a 0 grados para seleccionar la pastilla 1
        set_servo_angle(LEDC_CHANNEL_0, 500);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG3, "Servo 2: Set angle to 180");
        set_servo_angle(LEDC_CHANNEL_1, 2000);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG3, "Servo 2: Set angle to 0"); //en estas dos líneas (esta y la de arriba) el servo 2 baja y sube, para que la pastilla 1 caiga
        set_servo_angle(LEDC_CHANNEL_1, 500);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG3, "Servo 1: Set angle to 90"); //se mueve el servo 1 a 90 grados para seleccionar la pastilla 2
        set_servo_angle(LEDC_CHANNEL_0, 1500);
        vTaskDelay(pdMS_TO_TICKS(1000));


        ESP_LOGI(TAG3, "Servo 2: Set angle to 180");
        set_servo_angle(LEDC_CHANNEL_1, 2000);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG3, "Servo 2: Set angle to 0"); //en estas dos líneas (esta y la de arriba) el servo 2 baja y sube, para que la pastilla 2 caiga
        set_servo_angle(LEDC_CHANNEL_1, 500);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void mqttTask(void *pvParameter)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://demo.thingsboard.io/dashboards/fca5e570-7d8f-11ee-bc19-a533898c4c2a", 
        .broker.address.port = 1883,
        .credentials.username = "QjScjkusQI3bhdnGYXzi", // token del dispositivo ej. dispositivos->LDR->Copiar access token
    };

#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128)
        {
            int c = fgetc(stdin);
            if (c == '\n')
            {
                line[count] = '\0';
                break;
            }
            else if (c > 0 && c < 127)
            {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    while (1){
        
        // Crear json que se quiere enviar al ThingsBoard
        cJSON *root = cJSON_CreateObject();
        
        int value = codigo;

        cJSON_AddNumberToObject(root, "LDR", value); // En la telemetría de Thingsboard aparecerá key = key y value = 0.336
        char *post_data = cJSON_PrintUnformatted(root);
        // Enviar los datos
        if(value>0 || value < 0){
                    esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0); // v1/ devices/me/telemetry sale de la MQTT Device API Reference de ThingsBoard    

                    codigo = 0;
        }    
        cJSON_Delete(root);    // Free is intentional, it's client responsibility to free the result of cJSON_Print    
        free(post_data);
        printf("\n%d\n",value);
         vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void deepsleepTask(void *pvParameter){
    struct timeval now;
    gettimeofday(&now, NULL);
    int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;

    const int ext_wakeup_pin_0 = 2;

        printf("Enabling EXT0 wakeup on pin GPIO%d\n", ext_wakeup_pin_0);
        esp_sleep_enable_ext0_wakeup(ext_wakeup_pin_0, 1);

        
        rtc_gpio_pullup_dis(ext_wakeup_pin_0);
        rtc_gpio_pulldown_en(ext_wakeup_pin_0);

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("Entering deep sleep\n");
        gettimeofday(&sleep_enter_time, NULL);

        esp_deep_sleep_start();
}

void infrarrojosTask(void *pvParameters)
{
    gpio_set_direction(LDR,GPIO_MODE_INPUT);
    
    while (1) {
        int val = gpio_get_level(LDR);
        printf("Valor: %d\n",val);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    //NFC
    rc522_config_t config = {
        .spi.host = VSPI_HOST,
        .spi.miso_gpio = 19,
        .spi.mosi_gpio = 23,
        .spi.sck_gpio = 18,
        .spi.sda_gpio = 5,
    };

    rc522_create(&config, &scanner);
    rc522_register_events(scanner, RC522_EVENT_ANY, rc522_handler, NULL);
    rc522_start(scanner);

    //end NFC
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR1_PIN1) | (1ULL << MOTOR1_PIN2) | (1ULL << MOTOR2_PIN1) | (1ULL << MOTOR2_PIN2),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_0);
    gpio_set_direction(VALUE_PORT, GPIO_MODE_INPUT);

    gpio_set_direction(VALUE_PORT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(VALUE_PORT, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    // Inicia la tarea de control del motor
    xTaskCreate(motorControlTask, "motorControlTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);

    // Inicia la tarea de control del ultrasonido y los motores
    xTaskCreate(ultrasonicAndMotorControlTask, "ultrasonicAndMotorControlTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);

    xTaskCreate(servoTask, "servoTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);

    xTaskCreate(mqttTask, "mqttTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);

    xTaskCreate(deepsleepTask, "deepsleepTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);

    xTaskCreate(infrarrojosTask, "infrarrojosTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}