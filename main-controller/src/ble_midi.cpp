#include "ble_midi.h"
#include "led_control.h"

BleMidiModule::BleMidiModule() : pServer(nullptr), pCharacteristic(nullptr), deviceConnected(false), ledControl(nullptr)
{
}

BleMidiModule::~BleMidiModule()
{
  // Cleanup if needed
}

bool BleMidiModule::init(const char *deviceName, LedControl *ledControl)
{
  this->ledControl = ledControl;

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
}

void BleMidiModule::onNoteOff(byte channel, byte note, byte velocity)
{
  Serial.printf("Note Off - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
}

void BleMidiModule::onControlChange(byte channel, byte control, byte value)
{
  Serial.printf("Control Change - Channel: %d, Control: %d, Value: %d\n", channel, control, value);
}

void BleMidiModule::onProgramChange(byte channel, byte program)
{
  Serial.printf("Program Change - Channel: %d, Program: %d\n", channel, program);
}

void BleMidiModule::onPitchBend(byte channel, int bend)
{
  Serial.printf("Pitch Bend - Channel: %d, Bend: %d\n", channel, bend);
}

void BleMidiModule::processMidiData(std::string data)
{
  // Process BLE MIDI data using FortySevenEffects library
  // BLE MIDI data format: [timestamp_high, timestamp_low, status, data1, data2, ...]
  if (data.length() < 3)
    return;

  // Skip timestamp bytes and get MIDI status byte
  uint8_t status = data[2];

  // Check if it's a valid MIDI status byte
  if (status < 0x80)
    return;

  uint8_t channel = status & 0x0F;
  uint8_t command = status & 0xF0;

  switch (command)
  {
  case midi::NoteOff:
    if (data.length() >= 5)
    {
      onNoteOff(channel, data[3], data[4]);
    }
    break;

  case midi::NoteOn:
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

  case midi::ControlChange:
    if (data.length() >= 5)
    {
      onControlChange(channel, data[3], data[4]);
    }
    break;

  case midi::ProgramChange:
    if (data.length() >= 4)
    {
      onProgramChange(channel, data[3]);
    }
    break;

  case midi::PitchBend:
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
  if (module->ledControl)
  {
    module->ledControl->set(CRGB::White);
  }
}

void BleMidiModule::MyServerCallbacks::onDisconnect(BLEServer *pServer)
{
  module->deviceConnected = false;
  Serial.println("BLE MIDI Disconnected!");
  if (module->ledControl)
  {
    module->ledControl->set(CRGB::Black);
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
    // Process MIDI data using FortySevenEffects library
    module->processMidiData(rxValue);
  }
}
