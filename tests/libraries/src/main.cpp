#include <Arduino.h>

// Compile test for the libraries bundled with the framework: every library is
// included and used, so CI builds and links each of them for every board.
// Nothing here is meant to run on hardware; the pins are just valid numbers.
//
// Left out:
// - BLEAdafruitService needs Adafruit sensor libraries from the registry.
// - PDM and Servo stop with #error on anything but nRF52, and BLEHomekit
//   doesn't compile here: none of the three has been ported to the nRF54L.

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <RotaryEncoder.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <bluefruit.h>
#include <nRF54Crypto.h>

using namespace Adafruit_LittleFS_Namespace;

BLEUart bleuart;
SoftwareSerial swSerial(PIN_SERIAL1_RX, PIN_SERIAL1_TX);

void setup() {
    Serial.begin(115200);
    Serial.println("libraries");
    Serial.flush();
    Serial.end();

    uint8_t buf[16];
    nRF54Crypto.begin();
    nRF54Crypto.random(buf, sizeof(buf));
    nRF54Crypto.end();

    InternalFS.begin();
    File file = InternalFS.open("/test", FILE_O_WRITE);
    if (file) {
        file.write(buf, sizeof(buf));
        file.close();
    }

    Bluefruit.begin();
    bleuart.begin();
    Bluefruit.Advertising.addService(bleuart);
    Bluefruit.Advertising.start(0);

    Wire.begin();
    SPI.begin();
    swSerial.begin(9600);
    RotaryEncoder.begin(PIN_SERIAL1_RX, PIN_SERIAL1_TX);
}

void loop() {
    delay(1000);
}
