#ifndef _EVENT_H_
#define _EVENT_H_

#include <mouse.h>
#include <keyboard.h>
#include <FreeRTOS/task.h>

typedef union Event {
  uint8_t type;
  MouseEvent_t mouse;
  KeyEvent_t key;
} Event_t;

typedef struct ReadWriteEvent {
  char *buf;
  size_t nbyte;
  xTaskHandle taskhandle;
} ReadWriteEvent_t;

void QueuesInit(void);
void QueuesKill(void);

void PushKeyEventFromISR(const KeyEvent_t *ev);
void PushMouseEventFromISR(const MouseEvent_t *ev);
bool PopKeyEvent(Event_t *ev);
bool PopEventTtyRead(ReadWriteEvent_t *ev);
bool PopEventTtyWrite(ReadWriteEvent_t *ev);
void InitTtyDriverHandle(xTaskHandle h);
void PushEventTtyRead(ReadWriteEvent_t *ev);
void PushEventTtyWrite(ReadWriteEvent_t *ev);

#endif /* !_EVENT_H_ */
