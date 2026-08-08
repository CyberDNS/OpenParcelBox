#include "servo_task.h"

#include <Arduino.h>
#include <ESP32Servo.h>
#include <math.h>

#include "events.h"
#include "servo_config.h"

// Lock servo is an external actuator not covered by the vendor board demos.
// GPIO21 confirmed free against the board's pinout diagram.
static const int kServoPin = 21;
static const uint32_t kUnlockHoldMs = 2000; // TODO: source from AccessConfig.unlockDurationMs

static Servo s_servo;
static int s_currentAngle = 0;

// Glides from the servo's last known angle to targetAngle using a cosine
// ease-in/ease-out curve, so the mechanism accelerates and decelerates
// smoothly instead of snapping to position.
static void moveServoEased(int targetAngle) {
    int startAngle = s_currentAngle;
    int delta = targetAngle - startAngle;
    if (delta == 0) {
        return;
    }

    uint32_t steps = SERVO_SWEEP_MS / SERVO_STEP_INTERVAL_MS;
    for (uint32_t i = 1; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float eased = (1.0f - cosf(t * PI)) / 2.0f; // 0..1 ease-in-out
        int angle = startAngle + (int)lroundf(delta * eased);
        s_servo.write(angle);
        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_INTERVAL_MS));
    }

    s_servo.write(targetAngle);
    s_currentAngle = targetAngle;
}

static void unlockAndRelock(AccessScope scope) {
    int angle = (scope == AccessScope::FULL) ? SERVO_ANGLE_FULL : SERVO_ANGLE_PARCEL_ONLY;
    moveServoEased(angle);
    vTaskDelay(pdMS_TO_TICKS(kUnlockHoldMs));
    moveServoEased(0);
}

void servoTaskFn(void *pvParameters) {
    s_servo.attach(kServoPin);
    s_servo.write(0);
    s_currentAngle = 0;

    AppMessage msg;
    for (;;) {
        if (xQueueReceive(servoQueue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.event == AppEvent::ACCESS_PARCELBOX_GRANTED) {
                unlockAndRelock(msg.scope);
            }
        }
    }
}
