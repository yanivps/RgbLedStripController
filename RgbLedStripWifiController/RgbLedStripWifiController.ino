#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

// Pins definitions
const int TRIGGER_PIN = 0;
const int PIN_LED = 2;

// Reset button definitions
unsigned long lastTriggerPressed = 0;
#define RESET_THRESHOLD 3000 // 3 Seconds

// Client definitions
#define CLIENT_RECEIVE_TIMEOUT 30000 // 30 Seconds
unsigned long clientTimeoutMillis = 0;

// WiFi Definitions
const char WiFiAPPSK[] = "12345678";
char hostString[16] = {0};

// Wifi Manager config file saving
bool shouldSaveConfig = false;
bool staStaticIpFlag = false;
#define STATIC_IP_DEFAULT_VALUE ""
#define GATEWAY_DEFAULT_VALUE ""
#define SUBNET_DEFAULT_VALUE "255.255.255.0"
char staStaticIp[16] = STATIC_IP_DEFAULT_VALUE;
char staGateway[16] = GATEWAY_DEFAULT_VALUE;
char staSubnet[16] = SUBNET_DEFAULT_VALUE;

// Determines whether to run in STA mode as server or AP mode as Wifi manager
bool initialConfig = false;

// MDNS service definitions
#define MDNS_SERVICE_TYPE "ledstrip"
#define MDNS_SERVICE_PROTOCOL "tcp"

// Wifi server definitions
#define SERVER_PORT 4000
WiFiServer server(SERVER_PORT);
WiFiClient client; // Store client object connected to the server

void setup()
{
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_LED, OUTPUT);

  Serial.begin(57600);
  Serial.println("\n Starting");

  mountFileSystemAndReadConfig();
  initializeServer(); // Can be called before wifi is connected. Then, server will be available as soon as connected to a wifi network
  initializeWifi();
}

void loop()
{
  delay(1);
  startConfigurationPortalIfRequested();

  // Check if a client has connected
  while (!client.connected()) {
    client = server.available();
    if (client.connected()) {
        clientTimeoutMillis = millis();
    }
    return;
  }

  readDataFromClient(client);
}

void initializeServer() {
  mDNSSetup();
  server.begin();
}

void initializeWifi() {
  if (WiFi.SSID() == "") {
    Serial.println("We haven't got any access point credentials, so get them now");
    initialConfig = true;
  } else {
    WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    if (staStaticIpFlag) { // Set static ip from configuration file
      IPAddress ip, gateway, subnet;
      if (ip.fromString(staStaticIp) && gateway.fromString(staGateway) && subnet.fromString(staSubnet)) {
        Serial.print("Using Static IP: ");
        Serial.println(ip);
        WiFi.config(ip, gateway, subnet); // different from arduino order
        WiFi.begin();
      }
    }
    Serial.print("Connecting network: ");
    Serial.println(WiFi.SSID());
  }
}

void startConfigurationPortalIfRequested() {
  byte triggerPinState = digitalRead(TRIGGER_PIN);
  if (triggerPinState == HIGH) {
    lastTriggerPressed = 0;
  }
  if (triggerPinState == LOW || initialConfig) {
    if (lastTriggerPressed == 0)
      lastTriggerPressed = millis();

    if (millis() - lastTriggerPressed < RESET_THRESHOLD && !initialConfig)
      return;

    Serial.println("Configuration portal requested.");
    digitalWrite(PIN_LED, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.

    if (!setWifiManagerAP()) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected!");
    }
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    Serial.println("Reset ESP device");
    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up
    // so resetting the device allows to go back into config mode again when it reboots.
  }
}

bool setWifiManagerAP() {
  WiFiManager wifiManager;

  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString = "Led Strip " + macID;

  char AP_NameChar[AP_NameString.length() + 1];
  memset(AP_NameChar, 0, AP_NameString.length() + 1);

  for (int i=0; i<AP_NameString.length(); i++)
    AP_NameChar[i] = AP_NameString.charAt(i);


  char staStaticIpFlagCustomHtml[63] = "type=\"checkbox\" style=\"width: auto;\" onclick=\"toggle()\"";

  if (staStaticIpFlag) {
    strcat(staStaticIpFlagCustomHtml, " checked");
  }
  WiFiManagerParameter p_staStaticIpFlag("staStaticIpFlag", "Static IP", "T", 2, staStaticIpFlagCustomHtml);
  WiFiManagerParameter p_staStaticIpLabel("<label for=\"staStaticIpFlag\">Static IP</label>");
  WiFiManagerParameter p_staticIp("staStaticIp", "Static IP", staStaticIp, 16, "minlength=\"7\" maxlength=\"15\"");
  WiFiManagerParameter p_gateway("staGateway", "Gateway", staGateway, 16, "minlength=\"7\" maxlength=\"15\"");
  WiFiManagerParameter p_subnet("staSubnet", "Subnet", staSubnet, 16, "minlength=\"7\" maxlength=\"15\"");
  WiFiManagerParameter p_scriptTag("<script>function toggle(){var cb=document.getElementById(\"staStaticIpFlag\"),ip=document.getElementById(\"staStaticIp\"),gw=document.getElementById(\"staGateway\"),sn=document.getElementById(\"staSubnet\");var d=cb.checked?\"block\":\"none\";ip.style.display=gw.style.display=sn.style.display=d};toggle();</script>");

  wifiManager.addParameter(&p_staStaticIpFlag);
  wifiManager.addParameter(&p_staStaticIpLabel);
  wifiManager.addParameter(&p_staticIp);
  wifiManager.addParameter(&p_gateway);
  wifiManager.addParameter(&p_subnet);
  wifiManager.addParameter(&p_scriptTag);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFi.disconnect(true); // TODO: Workaround because wifi manager thinks we are already connected and do not change to new selected wifi network. remove upon new version
  if (wifiManager.startConfigPortal(AP_NameChar, WiFiAPPSK)) { // blocking operation until we configure wifi
    // Wifi was configured. Read updated parameters
    staStaticIpFlag = (strncmp(p_staStaticIpFlag.getValue(), "T", 1) == 0);
    strcpy(staStaticIp, staStaticIpFlag ? p_staticIp.getValue() : STATIC_IP_DEFAULT_VALUE);
    strcpy(staGateway, staStaticIpFlag ? p_gateway.getValue() : GATEWAY_DEFAULT_VALUE);
    strcpy(staSubnet, staStaticIpFlag ? p_subnet.getValue() : SUBNET_DEFAULT_VALUE);

    //save the custom parameters to FS
    if (shouldSaveConfig) {
      Serial.println("saving config");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["staStaticIpFlag"] = staStaticIpFlag;
      json["staStaticIp"] = staStaticIp;
      json["staGateway"] = staGateway;
      json["staSubnet"] = staSubnet;

      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }

      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      //end save
    }
    return true;
  }
  return false;
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void mountFileSystemAndReadConfig() {
  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          if (json.containsKey("staStaticIpFlag")) {
            staStaticIpFlag = json["staStaticIpFlag"];
          }

          if (json.containsKey("staStaticIp")) {
            strncpy(staStaticIp, json["staStaticIp"], 16);
            staStaticIp[15] = '\0';
          }

          if (json.containsKey("staGateway")) {
            strncpy(staGateway, json["staGateway"], 16);
            staGateway[15] = '\0';
          }

          if (json.containsKey("staSubnet")) {
            strncpy(staSubnet, json["staSubnet"], 16);
            staSubnet[15] = '\0';
          }

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

void mDNSSetup() {
  sprintf(hostString, "LEDStrip-%06X", ESP.getChipId());
  Serial.print("Hostname: ");
  Serial.println(hostString);
  WiFi.hostname(hostString);

  if (!MDNS.begin(hostString)) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  MDNS.addService("ledstrip", "tcp", SERVER_PORT); // Announce esp tcp service on port 8080
}

void readDataFromClient(WiFiClient client) {
  if (client.available()) {
    // Reset timeout if we receive data
    clientTimeoutMillis = millis();
  }
  while (client.available()) {
    Serial.write(client.read());
  }
  if (millis() - clientTimeoutMillis >= CLIENT_RECEIVE_TIMEOUT) {
    // Client timed out... Disconnect client to let other clients connect
    client.stop();
  }
}

