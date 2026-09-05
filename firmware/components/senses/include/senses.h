#pragma once
// Senses: inputs that emit events. Each sense owns its polling task and
// publishes to the bus — no sense ever calls another driver, and none of them
// subscribes to anything.
//
// Both start functions return false instead of aborting: a sense is optional
// hardware and a loose wire must not cost the creature its face.

namespace buddy {

// Capacitive petting pad.
// Emits: touch.down · touch.poke (release < 400 ms) · touch.pet (>= 400 ms)
//        payload: pad_name
bool touch_start(int gpio_touch_pad, const char* pad_name = "pad0");

// MFRC522 RFID reader (SPI3, exclusively). ISO14443A UIDs and NDEF text.
// Emits: nfc.tag  · lowercase hex uid, e.g. "04a1b2c9"
//        nfc.text · decoded text, only when the tag carries readable content
//        nfc.gone · empty
//
// The firmware never interprets what a tag says (#24): these are three facts,
// and a pack reflex decides what they mean. There is no UID registry here.
// nfc.tag always precedes nfc.text for the same presentation.
struct Rc522Pins {
  int sck, miso, mosi, cs, rst;
};
bool nfc_start(const Rc522Pins& pins);

}  // namespace buddy
