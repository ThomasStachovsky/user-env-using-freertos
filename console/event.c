#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/queue.h>

#include "event.h"

#define EV_MAXNUM 16

static QueueHandle_t EventQueue;
static QueueHandle_t TtyReadQueue;
static QueueHandle_t TtyWriteQueue;

xTaskHandle tty_driver_handle;

void QueuesInit(void) {
  EventQueue = xQueueCreate(EV_MAXNUM, sizeof(Event_t));
  TtyReadQueue = xQueueCreate(EV_MAXNUM, sizeof(ReadWriteEvent_t));
  TtyWriteQueue = xQueueCreate(EV_MAXNUM, sizeof(ReadWriteEvent_t));
}

void QueuesKill(void) {
  vQueueDelete(EventQueue);
  vQueueDelete(TtyReadQueue);
  vQueueDelete(TtyWriteQueue);
}

void InitTtyDriverHandle(xTaskHandle h) {
  tty_driver_handle = h;
}

void PushKeyEventFromISR(const KeyEvent_t *ev) {
  xQueueSendToBackFromISR(EventQueue, (const void *)ev, &xNeedRescheduleTask);
  xTaskNotifyFromISR(tty_driver_handle, 0x1, eSetBits, &xNeedRescheduleTask);
}

void PushMouseEventFromISR(const MouseEvent_t *ev) {
  xQueueSendToBackFromISR(EventQueue, (const void *)ev, &xNeedRescheduleTask);
}

void PushEventTtyRead(ReadWriteEvent_t *ev) {

  xQueueSend(TtyReadQueue, (void *)ev, portMAX_DELAY);
  xTaskNotify(tty_driver_handle, 0x01, eSetBits);
}

void PushEventTtyWrite(ReadWriteEvent_t *ev) {

  xQueueSend(TtyWriteQueue, (void *)ev, portMAX_DELAY);
  xTaskNotify(tty_driver_handle, 0x01, eSetBits);
}

bool PopKeyEvent(Event_t *ev) {
  return xQueueReceive(EventQueue, (void *)ev, 0);
}

bool PopEventTtyRead(ReadWriteEvent_t *ev) {
  return xQueueReceive(TtyReadQueue, (void *)ev, 0);
}

bool PopEventTtyWrite(ReadWriteEvent_t *ev) {
  return xQueueReceive(TtyWriteQueue, (void *)ev, 0);
}
