#pragma once

#include <stddef.h>

#include <Arduino.h>

// At-rest obfuscation for secrets written to LittleFS (WiFi/MQTT passwords)
// so pulling the flash chip doesn't hand over a plaintext password directly.
// The key is derived from this chip's factory-burned eFuse MAC (see
// secure_store.cpp) rather than stored anywhere in flash — so reading the
// ciphertext back out of the JSON file isn't enough on its own, unlike a
// fixed/hardcoded key sitting right next to it in the same firmware image.
//
// NOT a substitute for the ESP32's hardware flash encryption: the eFuse MAC
// is itself readable via the same physical/JTAG access that gets the flash
// dump, so this only raises the bar above "grep the file for a password" —
// it doesn't stop a determined attacker with the device in hand. Deliberately
// chosen over flash encryption because that's an irreversible, eFuse-burning
// step that can brick a misconfigured device; this is a plain, reversible
// code-only change.

// Encrypts `plain` (a null-terminated secret, expected to be short — see
// kSecretPlainMaxLen in secure_store.cpp) and returns it as a base64 string
// (random IV || AES-256-CBC ciphertext) safe to store directly as a JSON
// string value. Returns an empty string for a null/empty input (so "no
// password set" round-trips as itself rather than becoming a mystery blob),
// and also on any internal failure — callers should treat encrypting a
// non-empty secret to "" as an error worth surfacing, not silently persisting.
String encryptSecret(const char *plain);

// Reverses encryptSecret(). `encoded` empty -> outPlain[0] = '\0', returns
// true (the "no password set" case). Returns false (outPlain untouched
// beyond being null-terminated) if the input isn't validly-formed
// (corrupt/truncated base64, bad padding, wrong length) — most likely
// because the eFuse MAC used to derive the key doesn't match the one it was
// encrypted under, e.g. this config file was copied onto a different board.
bool decryptSecret(const char *encoded, char *outPlain, size_t outCap);
