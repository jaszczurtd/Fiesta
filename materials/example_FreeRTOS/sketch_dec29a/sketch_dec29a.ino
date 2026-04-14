
#include <FreeRTOS.h>
#include <queue.h>
#include "task.h"
#include "timers.h"

static QueueHandle_t xQueue = NULL;

static bool alertBlink = false;
void led_task(void *arg) {

  pinMode(LED_BUILTIN, OUTPUT);
  int val = 0;

  while(true) {
    alertBlink = !alertBlink;
    digitalWrite(LED_BUILTIN, alertBlink);
    
    val = (alertBlink) ? 1 : 0;

    xQueueSend(xQueue, &val, 0);
    vTaskDelay(500);
  }
}

void usb_task(void *arg) {
  int recv;

  while(true) {
    xQueueReceive(xQueue, &recv, portMAX_DELAY);
    Serial.print("led");
    Serial.print(recv);
    Serial.print("\n");
  }
}

void vTimerCallback(TimerHandle_t xTimer) {
    // Wykonaj operacje po upływie czasu timera
    Serial.print("Timer callback executed!\n");
}

void setup() {

  xQueue = xQueueCreate(1, sizeof(int));

  TaskHandle_t xTaskHandle;
  xTaskCreate(led_task, "LED_Task", 256, NULL, 1, &xTaskHandle);
  xTaskCreate(usb_task, "USB_Task", 256, NULL, 4, NULL);

  TimerHandle_t xTimer = xTimerCreate("MyTimer", pdMS_TO_TICKS(2000), pdTRUE, 0, vTimerCallback);

  if (xTimer != NULL) {
    // Uruchom timer
    if (xTimerStart(xTimer, 0) == pdPASS) {
        Serial.print("Timer started!\n");
    }
  }

}

void loop() { }
