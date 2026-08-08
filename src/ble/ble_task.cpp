#include "ble_task.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "events.h"

// TODO: replace with the real allow-listed phone address (or switch to
// matching a service UUID instead of a fixed MAC, since many phones use BLE
// address randomization).
static const char *kKnownPhoneAddress = "00:00:00:00:00:00";

static const int kRssiWindowSize = 5;
static const int kRssiHitsRequired = 3;
static const int kRssiThresholdDbm = -70;
static const uint32_t kScanDurationSec = 2;

static int8_t s_rssiWindow[kRssiWindowSize] = {0};
static uint8_t s_rssiWindowLen = 0;
static uint8_t s_rssiWindowPos = 0;

static void pushRssiSample(int8_t rssi) {
    s_rssiWindow[s_rssiWindowPos] = rssi;
    s_rssiWindowPos = (s_rssiWindowPos + 1) % kRssiWindowSize;
    if (s_rssiWindowLen < kRssiWindowSize) {
        s_rssiWindowLen++;
    }
}

static bool isPhoneNear() {
    if (s_rssiWindowLen < kRssiWindowSize) {
        return false;
    }
    uint8_t hits = 0;
    for (uint8_t i = 0; i < kRssiWindowSize; i++) {
        if (s_rssiWindow[i] > kRssiThresholdDbm) {
            hits++;
        }
    }
    return hits >= kRssiHitsRequired;
}

class ProximityScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *device) override {
        if (device->getAddress().toString() != kKnownPhoneAddress) {
            return;
        }
        pushRssiSample(device->getRSSI());
    }
};

void bleTaskFn(void *pvParameters) {
    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    static ProximityScanCallbacks callbacks;
    scan->setAdvertisedDeviceCallbacks(&callbacks);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    for (;;) {
        // Blocking for the scan's own duration is fine here — this is this
        // task's own work, not a busy-wait, and it never holds any queue.
        scan->start(kScanDurationSec, false);
        scan->clearResults();

        if (isPhoneNear()) {
            AppMessage msg{};
            msg.event = AppEvent::BLE_PROXIMITY_DETECTED;
            xQueueSend(logicQueue, &msg, 0);
        }
    }
}
