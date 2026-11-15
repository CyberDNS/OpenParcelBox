#include "letter_flap_task.h"

#include <Arduino.h>

#include "events.h"
#include "letter_flap_config.h"

static bool readFlapOpen() {
    return digitalRead(LETTER_FLAP_PIN) == LETTER_FLAP_OPEN_LEVEL;
}

static void sendFlapState(bool open) {
    Serial.printf("letterFlap: %s (GPIO%d=%s)\n", open ? "open" : "closed", LETTER_FLAP_PIN,
                  digitalRead(LETTER_FLAP_PIN) == HIGH ? "HIGH" : "LOW");
    AppMessage msg{};
    msg.event = AppEvent::LETTER_FLAP_CHANGED;
    msg.flapOpen = open;
    // Short wait rather than 0: a dropped "opened" would silently lose a
    // delivery from the inbox list.
    xQueueSend(logicQueue, &msg, pdMS_TO_TICKS(100));
}

void letterFlapTaskFn(void *pvParameters) {
    pinMode(LETTER_FLAP_PIN, INPUT_PULLUP);
    vTaskDelay(pdMS_TO_TICKS(10)); // let the pull-up settle before the first read

    bool stable = readFlapOpen();
    bool candidate = stable;
    uint32_t candidateSinceMs = millis();
    sendFlapState(stable);

    for (;;) {
        bool now = readFlapOpen();
        if (now != candidate) {
            candidate = now;
            candidateSinceMs = millis();
        } else if (candidate != stable &&
                   millis() - candidateSinceMs >= LETTER_FLAP_DEBOUNCE_MS) {
            stable = candidate;
            sendFlapState(stable);
        }
        vTaskDelay(pdMS_TO_TICKS(LETTER_FLAP_POLL_MS));
    }
}
