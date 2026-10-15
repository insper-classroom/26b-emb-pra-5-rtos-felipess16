/*
 * Lab RTOS - Sensor de ultrassom HC-SR04 com OLED
 */
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <stdio.h>
#include <task.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pins.h"
#include "ssd1306.h"

// === Definicoes para SSD1306 ===
ssd1306_t disp;
const int TRIGGER_PIN = 15;
const int ECHO_PIN = 16;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

void oled_display_init(void) {
    i2c_init(i2c1, 400000);
    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);
    ssd1306_show(&disp);

    gpio_init(SSD1306_PIN_LITE);
    gpio_set_dir(SSD1306_PIN_LITE, GPIO_OUT);
    gpio_put(SSD1306_PIN_LITE, 0);
}

void led_rgb_init(void) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);
    gpio_put(LED_PIN_R, 1);

    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);
    gpio_put(LED_PIN_G, 1);

    gpio_init(LED_PIN_B);
    gpio_set_dir(LED_PIN_B, GPIO_OUT);
    gpio_put(LED_PIN_B, 1);
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (true) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_put(TRIGGER_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void pin_callback(uint gpio, uint32_t events) {
    uint32_t tempo_atual = to_us_since_boot(get_absolute_time());
    xQueueSendFromISR(xQueueTime, &tempo_atual, 0);
}

void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);

    gpio_set_irq_enabled_with_callback(ECHO_PIN,
                                       GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
                                       true, &pin_callback);

    uint32_t t_subida, t_descida;

    while (true) {
        if (xQueueReceive(xQueueTime, &t_subida, portMAX_DELAY)) {
            // 30 ms cobre o alcance maximo do sensor (~4 m = ~23 ms)
            if (xQueueReceive(xQueueTime, &t_descida, pdMS_TO_TICKS(30))) {
                uint32_t duracao = t_descida - t_subida;
                int distancia = (int)(duracao / 58);
                printf("dist: %d cm\n", distancia);
                xQueueSend(xQueueDistance, &distancia, 0);
            }
        }
    }
}

void oled_task(void *p) {
    oled_display_init();
    led_rgb_init();

    int distancia;
    char buffer[20];

    while (true) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &distancia, pdMS_TO_TICKS(100))) {
                sprintf(buffer, "%d cm", distancia);

                ssd1306_clear(&disp);
                ssd1306_draw_string(&disp, 0, 0, 1, buffer);
                ssd1306_show(&disp);
            } else {
                ssd1306_clear(&disp);
                ssd1306_draw_string(&disp, 0, 0, 1, "SENSOR FALHOU");
                ssd1306_show(&disp);
            }
        }
    }
}

int main() {
    stdio_init_all();
    printf("Start RTOS\n");

    xQueueTime = xQueueCreate(32, sizeof(uint32_t));
    xQueueDistance = xQueueCreate(32, sizeof(int));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    printf("trigger\n");
    xTaskCreate(trigger_task, "TRIGGER_Task", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "ECHO_Task", 256, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED_Task", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}