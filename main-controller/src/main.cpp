#include <Arduino.h>
#include <FastLED.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// RGB LED on GPIO 48
#define LED_PIN 48
#define NUM_LEDS 1

// Create LED array
CRGB leds[NUM_LEDS];

// BLE MIDI Service UUID (standard MIDI over BLE)
#define SERVICE_UUID "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define CHARACTERISTIC_UUID "7772E5DB-3868-4112-A1A9-F2669D106BF3"

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;

// MIDI message handling functions
void OnNoteOn(byte channel, byte note, byte velocity)
{
  Serial.printf("Note On - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);

  // Visual feedback - LED turns green for note on
  leds[0] = CRGB::Green;
  FastLED.show();
}

void OnNoteOff(byte channel, byte note, byte velocity)
{
  Serial.printf("Note Off - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);

  // Visual feedback - LED turns red for note off
  leds[0] = CRGB::Red;
  FastLED.show();
}

void OnControlChange(byte channel, byte control, byte value)
{
  Serial.printf("Control Change - Channel: %d, Control: %d, Value: %d\n", channel, control, value);

  // Visual feedback - LED turns blue for control change
  leds[0] = CRGB::Blue;
  FastLED.show();
}

void OnProgramChange(byte channel, byte program)
{
  Serial.printf("Program Change - Channel: %d, Program: %d\n", channel, program);

  // Visual feedback - LED turns yellow for program change
  leds[0] = CRGB::Yellow;
  FastLED.show();
}

void OnPitchBend(byte channel, int bend)
{
  Serial.printf("Pitch Bend - Channel: %d, Bend: %d\n", channel, bend);

  // Visual feedback - LED turns purple for pitch bend
  leds[0] = CRGB::Purple;
  FastLED.show();
}

// MIDI parsing function
void parseMIDI(std::string data)
{
  if (data.length() < 2)
    return;

  uint8_t status = data[0];
  uint8_t channel = status & 0x0F;
  uint8_t command = status & 0xF0;

  switch (command)
  {
  case 0x80: // Note Off
    if (data.length() >= 3)
    {
      OnNoteOff(channel, data[1], data[2]);
    }
    break;
  case 0x90: // Note On
    if (data.length() >= 3)
    {
      if (data[2] == 0)
      {
        OnNoteOff(channel, data[1], 0);
      }
      else
      {
        OnNoteOn(channel, data[1], data[2]);
      }
    }
    break;
  case 0xB0: // Control Change
    if (data.length() >= 3)
    {
      OnControlChange(channel, data[1], data[2]);
    }
    break;
  case 0xC0: // Program Change
    if (data.length() >= 2)
    {
      OnProgramChange(channel, data[1]);
    }
    break;
  case 0xE0: // Pitch Bend
    if (data.length() >= 3)
    {
      int bend = (data[2] << 7) | data[1];
      OnPitchBend(channel, bend);
    }
    break;
  }
}

// BLE Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
    Serial.println("BLE MIDI Connected!");
    leds[0] = CRGB::White;
    FastLED.show();
  };
  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    Serial.println("BLE MIDI Disconnected!");
    leds[0] = CRGB::Black;
    FastLED.show();

    // Restart advertising after disconnection
    delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("Restarting BLE advertising...");
  }
};

// BLE Characteristic Callbacks
class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0)
    {
      // Parse MIDI data
      parseMIDI(rxValue);
    }
  }
};

void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(9600);

  // Initialize FastLED with GPIO 48
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50); // Set brightness (0-255)

  // Print hello world message
  Serial.println("Hello World! ESP32-S3 RGB LED on GPIO 48");
  Serial.println("Initializing BLE MIDI device...");

  // Initialize BLE
  BLEDevice::init("ESP32-S3 Piano");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE_NR);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();

  // Configure advertising data to include the MIDI service UUID
  BLEAdvertisementData advertisementData;
  advertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advertisementData.setName("ESP32-S3 Piano");
  advertisementData.setFlags(0x06);

  pAdvertising->setAdvertisementData(advertisementData);

  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06); // helps some devices connect
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE MIDI device 'ESP32-S3 Piano' is ready!");
  Serial.println("Connect from Synthesia app to start receiving MIDI messages");
  Serial.println("LED will show different colors for different MIDI events:");
  Serial.println("  Green = Note On");
  Serial.println("  Red = Note Off");
  Serial.println("  Blue = Control Change");
  Serial.println("  Yellow = Program Change");
  Serial.println("  Purple = Pitch Bend");
  Serial.println("  White = Connected");
  Serial.println("  Black = Disconnected");

  // Clear LED initially
  leds[0] = CRGB::Black;
  FastLED.show();
}

void loop()
{
  // BLE MIDI messages are handled via callbacks
  // No need to poll for messages

  // Small delay to prevent overwhelming the system
  delay(10);
}