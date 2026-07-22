#pragma once
// Senses: inputs that emit events. Each sense owns its polling task and
// publishes to the bus — no sense ever calls another driver directly.

namespace buddy {

// Capacitive petting pad. PoC: a bare jumper wire on a touch-capable pin.
// Emits: touch.pet (payload: pad name)
void touch_sense_start(int gpio_touch_pad);

// RC522 RFID reader (SPI). Reads ISO14443A UIDs (MIFARE fobs/cards, NTAG).
// Emits: nfc.tag (payload: lowercase hex uid, e.g. "04a1b2c9")
// PoC hardware note: exercises the UID-registry layer of the NFC design;
// the production PN532 adds NDEF payloads on top, the events stay the same.
struct Rc522Pins {
  int sck, miso, mosi, cs, rst;
};
void rc522_start(const Rc522Pins& pins);

}  // namespace buddy
