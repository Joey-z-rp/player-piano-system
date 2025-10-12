#ifndef BLE_MIDI_H
#define BLE_MIDI_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <MIDI.h>

// Forward declaration
class LedControl;

// BLE MIDI Service UUID (standard MIDI over BLE)
#define BLE_MIDI_SERVICE_UUID "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define BLE_MIDI_CHARACTERISTIC_UUID "7772E5DB-3868-4112-A1A9-F2669D106BF3"

class BleMidiModule
{
public:
  BleMidiModule();
  ~BleMidiModule();

  // Initialize BLE MIDI service
  bool init(const char *deviceName, LedControl *ledControl);

  // Check if device is connected
  bool isConnected() const;

  // MIDI message callback functions
  void onNoteOn(byte channel, byte note, byte velocity);
  void onNoteOff(byte channel, byte note, byte velocity);
  void onControlChange(byte channel, byte control, byte value);
  void onProgramChange(byte channel, byte program);
  void onPitchBend(byte channel, int bend);

  // Process BLE MIDI data
  void processMidiData(std::string data);

private:
  BLEServer *pServer;
  BLECharacteristic *pCharacteristic;
  bool deviceConnected;
  LedControl *ledControl;

  // BLE Server Callbacks
  class MyServerCallbacks : public BLEServerCallbacks
  {
  public:
    MyServerCallbacks(BleMidiModule *module) : module(module) {}
    void onConnect(BLEServer *pServer) override;
    void onDisconnect(BLEServer *pServer) override;

  private:
    BleMidiModule *module;
  };

  // BLE Characteristic Callbacks
  class MyCallbacks : public BLECharacteristicCallbacks
  {
  public:
    MyCallbacks(BleMidiModule *module) : module(module) {}
    void onWrite(BLECharacteristic *pCharacteristic) override;

  private:
    BleMidiModule *module;
  };
};

#endif // BLE_MIDI_H
