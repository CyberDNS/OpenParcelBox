#pragma once

// One servo, two gate positions: a partial rotation exposes only the parcel
// compartment, a full rotation exposes both parcel and letter compartments.
// Adjust these to match your actual mechanism, then reflash.
#define SERVO_ANGLE_PARCEL_ONLY 70
#define SERVO_ANGLE_FULL 110

// Ease-in/ease-out sweep timing. Total time to glide from one angle to
// another; step interval trades smoothness for time spent blocking the task.
#define SERVO_SWEEP_MS 1000
#define SERVO_STEP_INTERVAL_MS 15
