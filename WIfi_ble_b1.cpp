// Default Arduino includes
#include <Arduino.h>
#include <WiFi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1331.h>

// Includes for JSON object handling
// Requires ArduinoJson library
// https://arduinojson.org
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>

// Includes for BLE
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <Preferences.h>

/** Build time */
const char compileDate[] = __DATE__ " " __TIME__;

/** Unique device name */
char apName[] = "ESP32-xxxxxxxxxxxx";
/** Selected network
    true = use primary network
		false = use secondary network
*/
bool usePrimAP = true;
/** Flag if stored AP credentials are available */
bool hasCredentials = false;
/** Connection status */
volatile bool isConnected = false;
/** Connection change status */
bool connStatusChanged = false;

/**
 * Create unique device name from MAC address
 **/
 // You can use any (4 or) 5 pins
 #define sclk 18
 #define mosi 23
 #define cs   17
 #define rst  4
 #define dc   16

 // Color definitions
 #define BLACK           0x0000
 #define BLUE            0x001F
 #define RED             0xF800
 #define GREEN           0x07E0
 #define CYAN            0x07FF
 #define MAGENTA         0xF81F
 #define YELLOW          0xFFE0
 #define WHITE           0xFFFF


 #define CLK 32
 #define DT 21
 #define SW 25
 #include "GyverEncoder.h"
 Adafruit_SSD1331 display = Adafruit_SSD1331(cs, dc, mosi, sclk, rst);
 //Ucglib_SSD1331_18x96x64_UNIVISION_SWSPI ucg(/*sclk=*/ 18, /*data=*/ 23, /*cd=*/ 16, /*cs=*/ 17, /*reset=*/ 4);

 byte menuCount = 1;
 byte menuCount2 = 1;
 int valA;
 int valB;
 int valC;
 int valD;
 int valT;
 int valM;

 Encoder enc1(CLK, DT, SW);


void createName() {
	uint8_t baseMac[6];
	// Get MAC address for WiFi station
	esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
	// Write unique name into apName
	sprintf(apName, "ESP32-%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
}

// List of Service and Characteristic UUIDs
#define SERVICE_UUID  "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_UUID     "00005555-ead2-11e7-80c1-9a214cf093ae"

/** SSIDs of local WiFi networks */
String ssidPrim;
String ssidSec;
/** Password for local WiFi network */
String pwPrim;
String pwSec;

/** Characteristic for digital output */
BLECharacteristic *pCharacteristicWiFi;
/** BLE Advertiser */
BLEAdvertising* pAdvertising;
/** BLE Service */
BLEService *pService;
/** BLE Server */
BLEServer *pServer;

/** Buffer for JSON string */
// MAx size is 51 bytes for frame:
// {"ssidPrim":"","pwPrim":"","ssidSec":"","pwSec":""}
// + 4 x 32 bytes for 2 SSID's and 2 passwords
StaticJsonBuffer<200> jsonBuffer;

/**
 * MyServerCallbacks
 * Callbacks for client connection and disconnection
 */
class MyServerCallbacks: public BLEServerCallbacks {
	// TODO this doesn't take into account several clients being connected
	void onConnect(BLEServer* pServer) {
		Serial.println("BLE client connected");
	};

	void onDisconnect(BLEServer* pServer) {
		Serial.println("BLE client disconnected");
		pAdvertising->start();
	}
};

/**
 * MyCallbackHandler
 * Callbacks for BLE client read/write requests
 */
class MyCallbackHandler: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string value = pCharacteristic->getValue();
		if (value.length() == 0) {
			return;
		}
		Serial.println("Received over BLE: " + String((char *)&value[0]));

		// Decode data
		int keyIndex = 0;
		for (int index = 0; index < value.length(); index ++) {
			value[index] = (char) value[index] ^ (char) apName[keyIndex];
			keyIndex++;
			if (keyIndex >= strlen(apName)) keyIndex = 0;
		}

		/** Json object for incoming data */
		JsonObject& jsonIn = jsonBuffer.parseObject((char *)&value[0]);
		if (jsonIn.success()) {
			if (jsonIn.containsKey("ssidPrim") &&
					jsonIn.containsKey("pwPrim") &&
					jsonIn.containsKey("ssidSec") &&
					jsonIn.containsKey("pwSec")) {
				ssidPrim = jsonIn["ssidPrim"].as<String>();
				pwPrim = jsonIn["pwPrim"].as<String>();
				ssidSec = jsonIn["ssidSec"].as<String>();
				pwSec = jsonIn["pwSec"].as<String>();

				Preferences preferences;
				preferences.begin("WiFiCred", false);
				preferences.putString("ssidPrim", ssidPrim);
				preferences.putString("ssidSec", ssidSec);
				preferences.putString("pwPrim", pwPrim);
				preferences.putString("pwSec", pwSec);
				preferences.putBool("valid", true);
				preferences.end();

				Serial.println("Received over bluetooth:");
				Serial.println("primary SSID: "+ssidPrim+" password: "+pwPrim);
				Serial.println("secondary SSID: "+ssidSec+" password: "+pwSec);
				connStatusChanged = true;
				hasCredentials = true;
			} else if (jsonIn.containsKey("erase")) {
				Serial.println("Received erase command");
				Preferences preferences;
				preferences.begin("WiFiCred", false);
				preferences.clear();
				preferences.end();
				connStatusChanged = true;
				hasCredentials = false;
				ssidPrim = "";
				pwPrim = "";
				ssidSec = "";
				pwSec = "";

				int err;
				err=nvs_flash_init();
				Serial.println("nvs_flash_init: " + err);
				err=nvs_flash_erase();
				Serial.println("nvs_flash_erase: " + err);
			} else if (jsonIn.containsKey("reset")) {
				WiFi.disconnect();
				esp_restart();
			}
		} else {
			Serial.println("Received invalid JSON");
		}
		jsonBuffer.clear();
	};



	void onRead(BLECharacteristic *pCharacteristic) {
		Serial.println("BLE onRead request");
		String wifiCredentials;

		/** Json object for outgoing data */
		JsonObject& jsonOut = jsonBuffer.createObject();
		jsonOut["ssidPrim"] = ssidPrim;
		jsonOut["pwPrim"] = pwPrim;
		jsonOut["ssidSec"] = ssidSec;
		jsonOut["pwSec"] = pwSec;
		// Convert JSON object into a string
		jsonOut.printTo(wifiCredentials);

		// encode the data
		int keyIndex = 0;
		Serial.println("Stored settings: " + wifiCredentials);
		for (int index = 0; index < wifiCredentials.length(); index ++) {
			wifiCredentials[index] = (char) wifiCredentials[index] ^ (char) apName[keyIndex];
			keyIndex++;
			if (keyIndex >= strlen(apName)) keyIndex = 0;
		}
		pCharacteristicWiFi->setValue((uint8_t*)&wifiCredentials[0],wifiCredentials.length());
		jsonBuffer.clear();
	}
};

/**
 * initBLE
 * Initialize BLE service and characteristic
 * Start BLE server and service advertising
 */
void initBLE() {
	// Initialize BLE and set output power
	BLEDevice::init(apName);
	BLEDevice::setPower(ESP_PWR_LVL_P7);

	// Create BLE Server
	pServer = BLEDevice::createServer();

	// Set server callbacks
	pServer->setCallbacks(new MyServerCallbacks());

	// Create BLE Service
	pService = pServer->createService(BLEUUID(SERVICE_UUID),20);

	// Create BLE Characteristic for WiFi settings
	pCharacteristicWiFi = pService->createCharacteristic(
		BLEUUID(WIFI_UUID),
		// WIFI_UUID,
		BLECharacteristic::PROPERTY_READ |
		BLECharacteristic::PROPERTY_WRITE
	);
	pCharacteristicWiFi->setCallbacks(new MyCallbackHandler());

	// Start the service
	pService->start();

	// Start advertising
	pAdvertising = pServer->getAdvertising();
	pAdvertising->start();
}

/** Callback for receiving IP address from AP */
void gotIP(system_event_id_t event) {
	isConnected = true;
	connStatusChanged = true;
}

/** Callback for connection loss */
void lostCon(system_event_id_t event) {
	isConnected = false;
	connStatusChanged = true;
}

/**
	 scanWiFi
	 Scans for available networks
	 and decides if a switch between
	 allowed networks makes sense

	 @return <code>bool</code>
	        True if at least one allowed network was found
*/
bool scanWiFi() {
	/** RSSI for primary network */
	int8_t rssiPrim;
	/** RSSI for secondary network */
	int8_t rssiSec;
	/** Result of this function */
	bool result = false;

	Serial.println("Start scanning for networks");

	WiFi.disconnect(true);
	WiFi.enableSTA(true);
	WiFi.mode(WIFI_STA);

	// Scan for AP
	int apNum = WiFi.scanNetworks(false,true,false,1000);
	if (apNum == 0) {
		Serial.println("Found no networks?????");
		return false;
	}

	byte foundAP = 0;
	bool foundPrim = false;

	for (int index=0; index<apNum; index++) {
		String ssid = WiFi.SSID(index);
		Serial.println("Found AP: " + ssid + " RSSI: " + WiFi.RSSI(index));
		if (!strcmp((const char*) &ssid[0], (const char*) &ssidPrim[0])) {
			Serial.println("Found primary AP");
			foundAP++;
			foundPrim = true;
			rssiPrim = WiFi.RSSI(index);
		}
		if (!strcmp((const char*) &ssid[0], (const char*) &ssidSec[0])) {
			Serial.println("Found secondary AP");
			foundAP++;
			rssiSec = WiFi.RSSI(index);
		}
	}

	switch (foundAP) {
		case 0:
			result = false;
			break;
		case 1:
			if (foundPrim) {
				usePrimAP = true;
			} else {
				usePrimAP = false;
			}
			result = true;
			break;
		default:
			Serial.printf("RSSI Prim: %d Sec: %d\n", rssiPrim, rssiSec);
			if (rssiPrim > rssiSec) {
				usePrimAP = true; // RSSI of primary network is better
			} else {
				usePrimAP = false; // RSSI of secondary network is better
			}
			result = true;
			break;
	}
	return result;
}

/**
 * Start connection to AP
 */
void connectWiFi() {
	// Setup callback function for successful connection
	WiFi.onEvent(gotIP, SYSTEM_EVENT_STA_GOT_IP);
	// Setup callback function for lost connection
	WiFi.onEvent(lostCon, SYSTEM_EVENT_STA_DISCONNECTED);

	WiFi.disconnect(true);
	WiFi.enableSTA(true);
	WiFi.mode(WIFI_STA);

	Serial.println();
	Serial.print("Start connection to ");
	if (usePrimAP) {
		Serial.println(ssidPrim);
		WiFi.begin(ssidPrim.c_str(), pwPrim.c_str());
	} else {
		Serial.println(ssidSec);
		WiFi.begin(ssidSec.c_str(), pwSec.c_str());
	}
}

void setup() {
	enc1.setType(TYPE1); // тип энкодера TYPE1 одношаговый, TYPE2 двухшаговый. Если ваш энкодер работает странно, смените тип
	//enc1.setTickMode(AUTO);
	display.begin();
	display.fillScreen(BLACK);

	// Create unique device name
	createName();

	// Initialize Serial port
	Serial.begin(115200);
	// Send some device info
	Serial.print("Build: ");
	Serial.println(compileDate);

	Preferences preferences;
	preferences.begin("WiFiCred", false);
	bool hasPref = preferences.getBool("valid", false);
	if (hasPref) {
		ssidPrim = preferences.getString("ssidPrim","");
		ssidSec = preferences.getString("ssidSec","");
		pwPrim = preferences.getString("pwPrim","");
		pwSec = preferences.getString("pwSec","");

		if (ssidPrim.equals("")
				|| pwPrim.equals("")
				|| ssidSec.equals("")
				|| pwPrim.equals("")) {
			Serial.println("Found preferences but credentials are invalid");
		} else {
			Serial.println("Read from preferences:");
			Serial.println("primary SSID: "+ssidPrim+" password: "+pwPrim);
			Serial.println("secondary SSID: "+ssidSec+" password: "+pwSec);
			hasCredentials = true;
		}
	} else {
		Serial.println("Could not find preferences, need send data over BLE");
	}
	preferences.end();

	// Start BLE server
	initBLE();

	if (hasCredentials) {
		// Check for available AP's
		if (!scanWiFi) {
			Serial.println("Could not find any AP");
		} else {
			// If AP was found, start connection
			connectWiFi();
		}
	}
}
void pressKey(){
  enc1.tick();


  if (enc1.isLeft()) { //было нажитие переходим к след.пункту
    //  display.drawRect(0, 0, 96, 64, GREEN);
          display.fillScreen(BLACK);
          //display.drawRect(0, 0, 96, 64, YELLOW);
                 display.setTextColor(WHITE,BLACK);
                 display.setCursor(15, 5);
                 display.print("T/On:");
                 display.setCursor(58, 5);

                 display.setTextColor(WHITE,BLACK);
                 display.println(valA);
                 display.setCursor(75, 5);
								  display.setTextColor(YELLOW,BLACK);
                 display.println("hou");

                 display.setCursor(75,15);
                 display.print("min");

                 display.setTextColor(WHITE,BLACK);
                 display.setCursor(58,15);
                 display.print(valB);

          //-------------------------------------------------------------------------------------------------displayon

                 display.setCursor(15, 28);
                 display.print("T/Off:");
                 display.setCursor(58, 28);

                 display.setTextColor(WHITE,BLACK);
                 display.println(valC);
                 display.setCursor(75, 28);
								  display.setTextColor(YELLOW,BLACK);
                 display.println("hou");

                 display.setCursor(75, 38);
                 display.print("min");

                 display.setTextColor(WHITE,BLACK);
                 display.setCursor(58, 38);
                 display.print(valD);

								 display.setTextColor(RED,BLACK);
								display.setCursor(15, 46);
											display.print("<<< save >>>");
           display.setTextColor(GREEN,BLACK);
                display.setCursor(15, 56);
                 display.print(">>>monitor>>>");


          //-------------------------------------------------------------------------------------------------displayoff
            display.setTextColor(RED,BLACK);
          display.setCursor(0, (menuCount*10)-3); //выбор пункта

          menuCount++;

          display.print(">>");

  }

  if (menuCount == 2 &&enc1.isPress() ) { //поворот налево
          valA++;
          display.setCursor(58, 5);

          display.setTextColor(WHITE,BLACK);
          display.println(valA);


          if (valA>24) {
                  valA=0;
                  //EEPROM.write(0, valA);//пишем в eeprom
                  // EEPROM.commit();
                  display.fillScreen(BLACK);
                  display.setTextColor(RED,BLACK);
                  display.setCursor(0, 5);
                  display.print(">>");

          }
  }


  if (menuCount == 3 && enc1.isPress() ) {  //поворот налево
          valB++;
          display.setTextColor(WHITE,BLACK);
          display.setCursor(58,15);
          display.print(valB);

          if (valB>59) {
                  valB=0;
                  //EEPROM.write(1, valB); //пишем в eeprom
                  //EEPROM.commit();
                  display.fillScreen(BLACK);
                  display.setTextColor(RED,BLACK);
                  display.setCursor(0, 15);
                  display.print(">>");
          }
  }

  if (menuCount == 4 && enc1.isPress() ) {
    valC++;
    display.setCursor(58, 28);

    display.setTextColor(WHITE,BLACK);
    display.println(valC);

    if (valC>24) {
            valC=0;
            //EEPROM.write(0, valA);//пишем в eeprom
            // EEPROM.commit();
            display.fillScreen(BLACK);
            display.setTextColor(RED,BLACK);
            display.setCursor(0, 28);
            display.print(">>");

    }

  }
  if (menuCount == 5&& enc1.isPress() ) {
    valD++;
    display.setTextColor(WHITE,BLACK);
    display.setCursor(58, 38);
    display.print(valD);

    if (valD>24) {
            valD=0;
            //EEPROM.write(0, valA);//пишем в eeprom
            // EEPROM.commit();
            display.fillScreen(BLACK);
            display.setTextColor(RED,BLACK);
            display.setCursor(0, 38);
            display.print(">>");

    }

  }

	if (menuCount == 6&& enc1.isPress() ) {



	}
  if (menuCount == 7&& enc1.isPress() ) {
    display.fillScreen(MAGENTA );

		 display.setTextColor(RED,MAGENTA);
		display.setCursor(5, 5);
     display.print("Voltage: 220 V");
		 display.setTextColor(BLUE,MAGENTA);
		display.setCursor(5, 20);
		  display.print("Current: 220 A");
			display.setTextColor(YELLOW,MAGENTA);
			display.setCursor(5, 35);
			 display.print("Power: 220 kW");
			 display.setCursor(5, 50);
			  display.print("Energy: 220 kWh");

  }

  if (menuCount >= 8) { // >6 пунктов
          menuCount = 1;
          display.fillScreen(BLACK);

  }

}//displayon

void loop() {

	enc1.tick(); // обязательная функция отработки. Должна постоянно опрашиваться

	 pressKey();


	if (connStatusChanged) {
		if (isConnected) {
			Serial.print("Connected to AP: ");
			Serial.print(WiFi.SSID());
			Serial.print(" with IP: ");
			Serial.print(WiFi.localIP());
			Serial.print(" RSSI: ");
			Serial.println(WiFi.RSSI());
		} else {
			if (hasCredentials) {
				Serial.println("Lost WiFi connection");
				// Received WiFi credentials
				if (!scanWiFi) { // Check for available AP's
					Serial.println("Could not find any AP");
				} else { // If AP was found, start connection
					connectWiFi();
				}
			}
		}
		connStatusChanged = false;
	}
}
