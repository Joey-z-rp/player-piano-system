#include "ble_midi.h"

BleMidiModule::BleMidiModule() : pServer(nullptr), pCharacteristic(nullptr), deviceConnected(false), leds(nullptr), numLeds(0)
{
}

BleMidiModule::~BleMidiModule()
{
  // Cleanup if needed
}

bool BleMidiModule::begin(const char *deviceName, CRGB *leds, uint8_t numLeds)
{
  this->leds = leds;
  this->numLeds = numLeds;

  // Initialize BLE
  BLEDevice::init(deviceName);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks(this));

  // Create BLE Service
  BLEService *pService = pServer->createService(BLE_MIDI_SERVICE_UUID);

  // Create BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
      BLE_MIDI_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE_NR);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks(this));

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();

  // Configure advertising data to include the MIDI service UUID
  BLEAdvertisementData advertisementData;
  advertisementData.setCompleteServices(BLEUUID(BLE_MIDI_SERVICE_UUID));
  advertisementData.setName(deviceName);
  advertisementData.setFlags(0x06);

  pAdvertising->setAdvertisementData(advertisementData);

  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06); // helps some devices connect
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE MIDI device initialized successfully");
  return true;
}

bool BleMidiModule::isConnected() const
{
  return deviceConnected;
}

void BleMidiModule::onNoteOn(byte channel, byte note, byte velocity)
{
  Serial.printf("Note On - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);

  // Visual feedback - LED turns green for note on
  if (leds && numLeds > 0)
  {
    leds[0] = CRGB::Green;
    FastLED.show();
  }
}

void BleMidiModule::onNoteOff(byte channel, byte note, byte velocity)
{
  Serial.printf("Note Off - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);

  // Visual feedback - LED turns red for note off
  if (leds && numLeds > 0)
  {
    leds[0] = CRGB::Red;
    FastLED.show();
  }
}

void BleMidiModule::onControlChange(byte channel, byte control, byte value)
{
  Serial.printf("Control Change - Channel: %d, Control: %d, Value: %d\n", channel, control, value);

  // Visual feedback - LED turns blue for control change
  if (leds && numLeds > 0)
  {
    leds[0] = CRGB::Blue;
    FastLED.show();
  }
}

void BleMidiModule::onProgramChange(byte channel, byte program)
{
  Serial.printf("Program Change - Channel: %d, Program: %d\n", channel, program);

  // Visual feedback - LED turns yellow for program change
  if (leds && numLeds > 0)
  {
    leds[0] = CRGB::Yellow;
    FastLED.show();
  }
}

void BleMidiModule::onPitchBend(byte channel, int bend)
{
  Serial.printf("Pitch Bend - Channel: %d, Bend: %d\n", channel, bend);

  // Visual feedback - LED turns purple for pitch bend
  if (leds && numLeds > 0)
  {
    leds[0] = CRGB::Purple;
    FastLED.show();
  }
}

void BleMidiModule::processMidiData(std::string data)
{
  parseMIDI(data);
}

void BleMidiModule::parseMIDI(std::string data)
{
  if (data.length() < 3)
    return;

  uint8_t status = data[2];

  uint8_t channel = status & 0x0F;
  uint8_t command = status & 0xF0;

  switch (command)
  {
  case 0x80: // Note Off
    if (data.length() >= 5)
    {
      onNoteOff(channel, data[3], data[4]);
    }
    break;
  case 0x90: // Note On
    if (data.length() >= 5)
    {
      if (data[4] == 0)
      {
        onNoteOff(channel, data[3], 0);
      }
      else
      {
        onNoteOn(channel, data[3], data[4]);
      }
    }
    break;
  case 0xB0: // Control Change
    if (data.length() >= 5)
    {
      onControlChange(channel, data[3], data[4]);
    }
    break;
  case 0xC0: // Program Change
    if (data.length() >= 4)
    {
      onProgramChange(channel, data[3]);
    }
    break;
  case 0xE0: // Pitch Bend
    if (data.length() >= 5)
    {
      int bend = (data[4] << 7) | data[3];
      onPitchBend(channel, bend);
    }
    break;
  default:
    Serial.printf("Unknown MIDI command: 0x%02X\n", command);
    break;
  }
}

// BLE Server Callbacks Implementation
void BleMidiModule::MyServerCallbacks::onConnect(BLEServer *pServer)
{
  module->deviceConnected = true;
  Serial.println("BLE MIDI Connected!");
  if (module->leds && module->numLeds > 0)
  {
    module->leds[0] = CRGB::White;
    FastLED.show();
  }
}

void BleMidiModule::MyServerCallbacks::onDisconnect(BLEServer *pServer)
{
  module->deviceConnected = false;
  Serial.println("BLE MIDI Disconnected!");
  if (module->leds && module->numLeds > 0)
  {
    module->leds[0] = CRGB::Black;
    FastLED.show();
  }

  // Restart advertising after disconnection
  delay(500);                  // give the bluetooth stack the chance to get things ready
  pServer->startAdvertising(); // restart advertising
  Serial.println("Restarting BLE advertising...");
}

// BLE Characteristic Callbacks Implementation
void BleMidiModule::MyCallbacks::onWrite(BLECharacteristic *pCharacteristic)
{
  std::string rxValue = pCharacteristic->getValue();
  if (rxValue.length() > 0)
  {
    // Parse MIDI data
    module->parseMIDI(rxValue);
  }
}
