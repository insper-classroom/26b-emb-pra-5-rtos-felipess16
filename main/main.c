/*
 * Lab RTOS - Sensor de ultrassom HC-SR04 com OLED e LED RGB
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

ssd1306_t disp;

const int TRIGGER_PIN = 15;
const int ECHO_PIN = 16;

// Distancia (cm) que corresponde a barra cheia no display
const int DIST_MAX_BARRA = 200;

// Acima disso o LED fica amarelo, abaixo fica verde
const int DIST_LIMITE_LED = 100;

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
    ssd1306_init(&disp, 128, 32, 0x3C, i2c1);
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

// Logica invertida: 0 acende, 1 apaga
void led_rgb_set(int r, int g, int b) {
    gpio_put(LED_PIN_R, !r);
    gpio_put(LED_PIN_G, !g);
    gpio_put(LED_PIN_B, !b);
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    while (true) {
        // descarta sobras da medicao anterior
        xQueueReset(xQueueTime);

        // o HC-SR04 pede um pulso de 10 us
        gpio_put(TRIGGER_PIN, 1);
        sleep_us(10);
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

                // som a 0,034 cm/us, dividido por 2 (ida e volta) -> t / 58
                int distancia = (int)(duracao / 58);

                xQueueSend(xQueueDistance, &distancia, 0);
            }
        }
    }
}

void oled_task(void *p) {
    oled_display_init();
    led_rgb_init();

    int distancia;
    int largura;
    char buffer[20];

    while (true) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &distancia, pdMS_TO_TICKS(100))) {
                // regra de tres: DIST_MAX_BARRA cm equivale a 128 pixels
                largura = distancia * 128 / DIST_MAX_BARRA;
                if (largura > 128) {
                    largura = 128;
                }
                if (largura < 0) {
                    largura = 0;
                }

                sprintf(buffer, "Dist: %d cm", distancia);

                ssd1306_clear(&disp);
                ssd1306_draw_string(&disp, 0, 0, 1, buffer);
                ssd1306_draw_square(&disp, 0, 20, largura, 6);
                ssd1306_show(&disp);

                if (distancia <= DIST_LIMITE_LED) {
                    led_rgb_set(0, 1, 0);  // verde
                } else {
                    led_rgb_set(1, 1, 0);  // amarelo = vermelho + verde
                }
            } else {
                ssd1306_clear(&disp);
                ssd1306_draw_string(&disp, 0, 0, 1, "SENSOR FALHOU");
                ssd1306_show(&disp);

                led_rgb_set(1, 0, 0);  // vermelho
            }
        }
    }
}

int main() {
    stdio_init_all();

    xQueueTime = xQueueCreate(32, sizeof(uint32_t));
    xQueueDistance = xQueueCreate(32, sizeof(int));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "TRIGGER_Task", 512, NULL, 1, NULL);
    xTaskCreate(echo_task, "ECHO_Task", 512, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED_Task", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}