#include "ArduinoJson.h"
#include "EEPROM.h"
#include <Preferences.h>
#define EEPROM_SIZE 128

#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


/**
 * TODOs:
 * ---------------------------------------
 * [ ] New UUIDs
 * [ ] Remove EEPROM.h and replace with Preferences.h
 * [ ] Create Connection State Management for loop()
 * [ ] Refactoring Code
 * [ ] Sending JSON via BLE to Device 
 * [ ] Retriving / parsing JSON from app and connect to network
 * [ ] Saving network details to Preferences 
 * [ ] Making a secure connection with authentication??
 * [ ] Webserver API setup and routes 
 * [ ] Static IP and option to save it
 * [ ] Response to app after establishing wifi connection
 */


// TODO: NEW UUID
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEVICE_NAME "Open Light SSSL"

Preferences preferences;

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool BLE_deviceConnected = false;
bool BLE_oldDeviceConnected = false;

const int ledPin = LED_BUILTIN;
int modeIdx;        // Mode Index (0 == BLE & 1 == WIFI)

//EEPROM ADDRESSES
const int modeAddr = 0;
const int wifiAddr = 10;



class ol_BTServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      BLE_deviceConnected = true;
      Serial.println("BLE Device connected");
      BLEDevice::startAdvertising();
    };

    void onDisconnect(BLEServer* pServer) {
      BLE_deviceConnected = false;
      Serial.println("BLE Device disconnected");
    }
};

class ol_BTCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.print("Value : ");
        Serial.println(value.c_str());
        writeString(wifiAddr, value.c_str());
      }
    }

    void writeString(int add, String data) {
      int _size = data.length();
      for (int i = 0; i < _size; i++) {
        EEPROM.write(add + i, data[i]);
      }
      EEPROM.write(add + _size, '\0');
      EEPROM.commit();
    }
};


/*
   BLEUTOOTH TASK: Creating and enabling a Bluetooth server, ready for connecting
   @return void
*/
void bleTask() {
  // Create the BLE Device
  BLEDevice::init(DEVICE_NAME);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ol_BTServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  pCharacteristic->setCallbacks(new ol_BTCallbacks());
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}


/*
   handleBLEConnections:
   Checking the Bluetooth connection and starting advertising if not connected
   @return void
*/
void handleBLEConnections () {
  if (!BLE_deviceConnected && BLE_oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      Serial.println("start advertising");
      BLE_oldDeviceConnected = BLE_deviceConnected;
  }
  // connecting
  if (BLE_deviceConnected && !BLE_oldDeviceConnected) {
      // do stuff here on connecting
      BLE_oldDeviceConnected = BLE_deviceConnected;
  }
}


/*
   notifyClients: Sending (Notifying) the client that's listening into the characteristic.  
   @return void
*/
void notifyClients (String msg) {
  if (!BLE_deviceConnected) {
    return;
  }

  pCharacteristic->setValue((uint8_t*)&msg, 4);
  pCharacteristic->notify();
}


/*
   WIFI TASK: Setting up WiFi connection with recieved and stored credentials in NVS
   @return void
*/
void wifiTask() {
  String receivedData;
  receivedData = read_String(wifiAddr);

  if (receivedData.length() > 0) {
    String wifiName = getValue(receivedData, ',', 0);
    String wifiPassword = getValue(receivedData, ',', 1);

    if (wifiName.length() > 0 && wifiPassword.length() > 0) {
      Serial.print("WifiName : ");
      Serial.println(wifiName);

      Serial.print("wifiPassword : ");
      Serial.println(wifiPassword);

      WiFi.begin(wifiName.c_str(), wifiPassword.c_str());
      Serial.print("Connecting to Wifi");
      while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(300);
      }
      Serial.println();
      Serial.print("Connected with IP: ");
      Serial.println(WiFi.localIP());
    }
  }
}

/**
 * scanForWifiNetworks: Scanning for wifi networks and outputting a json string to serial
 */
DynamicJsonDocument getWifiNetworks () {
  WiFi.mode(WIFI_STA);
  //WiFi.disconnect();
  //delay(100);

  // Creatiung JSON Document
  DynamicJsonDocument doc(4096);
  JsonArray networks = doc.createNestedArray("networks");

  Serial.println("WiFi scan started");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("WiFi scan done");
    
  if (n == 0) {
    //Serial.println("no networks found");
    doc["code"] = 204;
    doc["status"] = "No networks found";
  } else {
    doc["code"] = 200;
    doc["status"] = n + String(" Networks found");

    // Looping over found networks and creating a new object for each network
    for (int i = 0; i < n; ++i) {     
      StaticJsonDocument<300> element;
      
      element["ssid"] = WiFi.SSID(i);
      element["rssi"] = WiFi.RSSI(i);
      element["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? false : true;
      
      networks.add(element);
      delay(10);
    }
  }
  return doc;
}

/**
 * getWifiMacAddress: Get MAC Address from WiFi Shield and print as string
 * @return String MAC Address
 */
String getWifiMacAddress() {

	uint8_t baseMac[6];

	WiFi.macAddress(baseMac);

	char baseMacChr[18] = {0};
	sprintf(baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
	return String(baseMacChr);
}



/*
   Read String: Returning string that was recived and stored in NVS
   @param int add Address where to read from
   @return String
*/
String read_String(int add) {
  char data[100];
  int len = 0;
  unsigned char k;
  k = EEPROM.read(add);
  while (k != '\0' && len < 500) {
    k = EEPROM.read(add + len);
    data[len] = k;
    len++;
  }
  data[len] = '\0';
  return String(data);
}


/*
 * getValue: Returning string that was recived and stored in NVS
 * @param String data Data that was sent and stored in NVS
 * @param char seperator Seperator that was used to delimiter the SSID & PW
 * @param int index Index of which data string to gather
 * @return String
*/
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}





/**
 * -------------------------------------------------
 * MAIN THREAD
 * -------------------------------------------------
 */ 

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  // Open Preferences with open-light namespace.
  preferences.begin("open-light", false);

  modeIdx = preferences.getInt("modeIdx", 1);

  //preferences.putInt("modeIdx", modeIdx);
  Serial.printf("Current Mode: %s\n", modeIdx != 0 ? "BLE" : "WiFi");

  // close preferences
  preferences.end();

  //scanForWifiNetworks();
  if(modeIdx != 0){
    //BLE MODE
    digitalWrite(ledPin, true);
    Serial.println("BLE MODE");
    bleTask();
  }else{
    //WIFI MODE
    digitalWrite(ledPin, false);
    Serial.println("WIFI MODE");
    //wifiTask();
  }
  
}


void loop() {
  // put your main code here, to run repeatedly:

  if(modeIdx == 0) { 
    // BLE Mode 
    handleBLEConnections();

    if (BLE_deviceConnected) {
      while(BLE_deviceConnected) {

        char output[2048];
        serializeJson(getWifiNetworks(), output);
        notifyClients(output);
        delay(5000);
              
      }
    }

  } else {
    // WiFi Mode


  }
  //scanForWifiNetworks();
    // Print SSID and RSSI for each network found
  
  getWifiMacAddress();
  Serial.println("");
  

}
