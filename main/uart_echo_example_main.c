/* ZPHS01B Example
   This software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include <string.h>

/**
 * ESP-IDF v5.0.7
 * - Port: configured UART, baudrate 9600
 * - Pin assignment: see defines below (See Kconfig)
 */

#define UART_TXD_PIN (CONFIG_EXAMPLE_UART_TXD)
#define UART_RXD_PIN (CONFIG_EXAMPLE_UART_RXD)
#define UART_RTS (UART_PIN_NO_CHANGE)
#define UART_CTS (UART_PIN_NO_CHANGE)

#define UART_PORT_NUM      (CONFIG_EXAMPLE_UART_PORT_NUM)
#define UART_BAUD_RATE     (CONFIG_EXAMPLE_UART_BAUD_RATE)
#define TASK_STACK_SIZE    (CONFIG_EXAMPLE_TASK_STACK_SIZE)

uint8_t check_response(uint8_t *response, int response_length);
void reset_response_buffer_and_counter(uint8_t *response, int *response_len);
void print_response(uint8_t *response, int response_len);

static const char *TAG = "UART";
const uint8_t ZPHS01B_DATA_REQUEST[] = {0xff, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
const uint8_t ZPHS01B_DATA_REQUEST_LEN = sizeof(ZPHS01B_DATA_REQUEST)/sizeof(uint8_t);

#define BUF_SIZE (1024)
#define RESPONSE_LENGTH (26)
#define READ_DATA_PAUSE_MS (30000)   //pause before next read in milliseconds

static void echo_task(void *arg)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TXD_PIN, UART_RXD_PIN, UART_RTS, UART_CTS));

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    int len = 0;
    reset_response_buffer_and_counter(data, &len);

    while (1) {

        // Write data to the ZPHS01B module
        uart_write_bytes(UART_PORT_NUM, ZPHS01B_DATA_REQUEST, ZPHS01B_DATA_REQUEST_LEN);

        // Read data from the ZPHS01B module
        len = uart_read_bytes(UART_PORT_NUM, data, (BUF_SIZE - 1), pdMS_TO_TICKS(120));

        if (check_response(data, len)) {
            reset_response_buffer_and_counter(data, &len);
            ESP_LOGI(TAG, "error in response data checksum");
            ESP_LOGI(TAG, "response data %s ; response length %d", data, len);
            continue;
        } else {
            ESP_LOGI(TAG, "response check is passed");
        }

        data[len] = '\0';
        print_response(data, len);
        reset_response_buffer_and_counter(data, &len);

        vTaskDelay(pdMS_TO_TICKS(READ_DATA_PAUSE_MS));
    }
}

void print_response(uint8_t *response, int response_len) {
    if (response_len != RESPONSE_LENGTH) {
        ESP_LOGI(TAG, "wrong reponse length in data print_response()");
        return;
    }

    uint16_t pm1_0 = response[2]*256 + response[3]; //0..1000 ug/m3
    uint16_t pm2_5 = response[4]*256 + response[5]; //0..1000 ug/m3
    uint16_t pm10 = response[6]*256 + response[7];  //0..1000 ug/m3
    uint16_t co2 = response[8]*256 + response[9];   //0..5000 ppm
    uint8_t  voc = response[10];                    //0..3 grades

    int temp1 = response[11]*256;
    int temp2 = response[12];
    double temp = ((temp1 + temp2) - 500) * 0.1;   //-20.0 .. 65.0 Celsius in 0.1

    uint16_t humidity = response[13]*256 + response[14]; //0..100 %RH

    uint32_t ch2o_part1 = response[15]*256;
    uint32_t ch2o_part2 = response[16];
    double ch2o = (ch2o_part1 + ch2o_part2) * 0.001; //0.000..6.250 ug/m3

    uint32_t co_part1 = response[17]*256;
    uint32_t co_part2 = response[18];
    double co = (co_part1 + co_part2) * 0.1; //0.0 .. 500.0 in 0.1 ppm

    uint32_t o3_part1 = response[19]*256;
    uint32_t o3_part2 = response[20];
    double o3 = (o3_part1 + o3_part2) * 0.01; //0.00 .. 10.00 ppm in 0.01 ug/m3

    uint32_t no2_part1 = response[21]*256;
    uint32_t no2_part2 = response[22];
    double no2 = (no2_part1 + no2_part2) * 0.01; //0.00 .. 10.00 ppm in 0.05 ug/m3

    ESP_LOGI(TAG, "\n    all pm are in ug/m3: pm1.0 %d, pm2.5 %d, pm10 %d,"
            "\n    CO2 %d ppm, TVOC %d in levels 0..3,"
            "\n    temperature %.1f in Celsius, humidity %d in percents of relative humidity,"
            "\n    CH2O %.3f in ug/m3, CO %.1f in ppm, O3 %.2f in ug/m3, NO2 %.2f in ug/m3;" ,
            pm1_0, pm2_5, pm10, co2, voc, temp, humidity, ch2o, co, o3, no2);
}

void reset_response_buffer_and_counter(uint8_t *response, int *response_len) {
    if (*response_len < 0) {
        *response_len = BUF_SIZE;
    }
    memset(response, 0, *response_len);
    *response_len = 0;
}

/*In accordance to ZPHS01B datasheet 
Check value=(invert( Byte1+Byte2+...+Byte24))+1
Return: 1 if there are errors and 0 if there are no errors.
*/
uint8_t check_response(uint8_t *response, int response_length)
{
    uint8_t checksum = 0;
    const char *TAG = "RESPONSE CHECK";

    if (response_length != RESPONSE_LENGTH) { // RESPONSE_LENGTH is in accordance to datasheet of ZPHS01B, pages 5 and 6 
        ESP_LOGI(TAG, "error in response length: actual %d, expected %d", response_length, RESPONSE_LENGTH);
        return 1;
    }

    for (int i = 1; i < 25; i++) {
        checksum += response[i];
    }
    checksum = ~checksum + 1;

    if (checksum != response[25]) { //25th byte is sent by module a response's checksum
        ESP_LOGI(TAG, "error in response data checksum: actual %02x, calculated %02x", response[25], checksum);
        return 1;
    }
    return 0;
}

void app_main(void)
{
    xTaskCreate(echo_task, "uart_echo_task", TASK_STACK_SIZE, NULL, 10, NULL);
    while(1);
}
