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
bool runAsAccessPoint = false; // Allow using server without connecting to a wireless network
char accessPointSSID[32] = "";
#define AP_DEFAULT_PASSWORD "12345678"
char accessPointPassword[64] = AP_DEFAULT_PASSWORD;
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
  if (runAsAccessPoint) {
    initializeAccessPoint();
    return;
  }

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

void initializeAccessPoint() {
  if (strnlen(accessPointSSID, sizeof(accessPointSSID)) == 0) {
    uint8_t mac[WL_MAC_ADDR_LENGTH];
    WiFi.softAPmacAddress(mac);
    String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                  String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
    macID.toUpperCase();
    snprintf(accessPointSSID, sizeof(accessPointSSID), "Led Strip AP %s", macID.c_str());
  }

  if (strnlen(accessPointPassword, sizeof(accessPointPassword)) == 0) {
    strncpy(accessPointPassword, AP_DEFAULT_PASSWORD, sizeof(accessPointPassword));
  }

  WiFi.softAP(accessPointSSID, accessPointPassword);
  IPAddress IP = WiFi.softAPIP();
  Serial.println("Running as Acess Point:");
  Serial.print("SSID: ");
  Serial.println(accessPointSSID);
  Serial.print("IP address: ");
  Serial.println(IP);
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


  char staStaticIpFlagCustomHtml[82] = "type=\"checkbox\" style=\"width: auto; margin-right: 5px\" onclick=\"toggle()\"";
  char runAsAccessPointCustomHtml[82] = "type=\"checkbox\" style=\"width: auto; margin-right: 5px\" onclick=\"toggle()\"";

  if (staStaticIpFlag) {
    strcat(staStaticIpFlagCustomHtml, " checked");
  }
  if (runAsAccessPoint) {
    strcat(runAsAccessPointCustomHtml, " checked");
  }

  WiFiManagerParameter lineBreak("<br>");
  WiFiManagerParameter p_runAsAccessPoint("runAsAccessPoint", "Run as Access Point", "T", 2,
                                         runAsAccessPointCustomHtml, WFM_LABEL_AFTER);
  if (strnlen(accessPointSSID, sizeof(accessPointSSID)) == 0) {
    snprintf(accessPointSSID, sizeof(accessPointSSID), "Led Strip AP %s", macID.c_str());
  }
  WiFiManagerParameter p_accessPointSSID("accessPointSSID", "AP SSID", accessPointSSID, 32,
                                  "minlength=\"3\" maxlength=\"31\"");
  WiFiManagerParameter p_accessPointPassword("accessPointPassword", "AP Password", accessPointPassword, 64,
                                  "minlength=\"8\" maxlength=\"63\"");
  WiFiManagerParameter p_staStaticIpFlag("staStaticIpFlag", "Static IP", "T", 2,
                                         staStaticIpFlagCustomHtml, WFM_LABEL_AFTER);
  WiFiManagerParameter p_staticIp("staStaticIp", "Static IP", staStaticIp, 16,
                                  "minlength=\"7\" maxlength=\"15\"");
  WiFiManagerParameter p_gateway("staGateway", "Gateway", staGateway, 16,
                                 "minlength=\"7\" maxlength=\"15\"");
  WiFiManagerParameter p_subnet("staSubnet", "Subnet", staSubnet, 16,
                                "minlength=\"7\" maxlength=\"15\"");
  WiFiManagerParameter p_scriptTag(
      "<script>function toggle(){var "
      "cb=document.getElementById(\"staStaticIpFlag\"),cb2=document.getElementById(\"runAsAccessPoint\"),ip=document.getElementById(\"staStaticIp\"),"
      "ipLabel=document.querySelector('label[for=\"staStaticIp\"]'),"
      "gw=document.getElementById(\"staGateway\"),gwLabel=document.querySelector('label[for="
      "\"staGateway\"]'),sn=document.getElementById(\"staSubnet\"),snLabel=document.querySelector("
      "'label[for=\"staSubnet\"]'),apSSID=document.getElementById(\"accessPointSSID\"),apSSIDLabel=document.querySelector('label[for=\"accessPointSSID\"]'),"
      "apPass=document.getElementById(\"accessPointPassword\"),apPassLabel=document.querySelector('label[for=\"accessPointPassword\"]');var "
      "d=cb.checked?\"block\":\"none\",dl=cb.checked?\"inline-block\":\"none\";"
      "d2=cb2.checked?\"block\":\"none\",dl2=cb2.checked?\"inline-block\":\"none\";"
      "ip.style.display=gw.style.display=sn.style.display=d;"
      "apSSID.style.display=apPass.style.display=d2;"
      "ipLabel.style.display=gwLabel.style.display=snLabel.style.display=dl;"
      "apSSIDLabel.style.display=apPassLabel.style.display=dl2;"
      "ipLabel.nextElementSibling.style.display=gwLabel.nextElementSibling.style.display=snLabel.nextElementSibling.style.display=d;"
      "apSSIDLabel.nextElementSibling.style.display=apPassLabel.nextElementSibling.style.display=d2;"
      "};toggle();</script>");

  wifiManager.addParameter(&p_runAsAccessPoint);
  wifiManager.addParameter(&lineBreak);
  wifiManager.addParameter(&p_accessPointSSID);
  wifiManager.addParameter(&p_accessPointPassword);
  wifiManager.addParameter(&lineBreak);
  wifiManager.addParameter(&p_staStaticIpFlag);
  wifiManager.addParameter(&lineBreak);
  wifiManager.addParameter(&p_staticIp);
  wifiManager.addParameter(&p_gateway);
  wifiManager.addParameter(&p_subnet);
  wifiManager.addParameter(&p_scriptTag);
  wifiManager.addParameter(&lineBreak);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFi.disconnect(true); // TODO: Workaround because wifi manager thinks we are already connected and do not change to new selected wifi network. remove upon new version

  wifiManager.setBreakAfterConfig(true);
  // blocking operation until we configure wifi
  bool isWifiConfigured = wifiManager.startConfigPortal(AP_NameChar, WiFiAPPSK);

  runAsAccessPoint = (strncmp(p_runAsAccessPoint.getValue(), "T", 1) == 0);
  if (runAsAccessPoint) {
    strncpy(accessPointSSID, p_accessPointSSID.getValue(), sizeof(accessPointSSID));
    strncpy(accessPointPassword, p_accessPointPassword.getValue(), sizeof(accessPointPassword));
  }

  if (isWifiConfigured) {
    // Wifi was configured. Read updated parameters
    staStaticIpFlag = (strncmp(p_staStaticIpFlag.getValue(), "T", 1) == 0);
    strcpy(staStaticIp, staStaticIpFlag ? p_staticIp.getValue() : STATIC_IP_DEFAULT_VALUE);
    strcpy(staGateway, staStaticIpFlag ? p_gateway.getValue() : GATEWAY_DEFAULT_VALUE);
    strcpy(staSubnet, staStaticIpFlag ? p_subnet.getValue() : SUBNET_DEFAULT_VALUE);
  }

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["runAsAccessPoint"] = runAsAccessPoint;
    json["accessPointSSID"] = accessPointSSID;
    json["accessPointPassword"] = accessPointPassword;
    if (isWifiConfigured) {
      json["staStaticIpFlag"] = staStaticIpFlag;
      json["staStaticIp"] = staStaticIp;
      json["staGateway"] = staGateway;
      json["staSubnet"] = staSubnet;
    }

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  return isWifiConfigured;
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

          if (json.containsKey("runAsAccessPoint")) {
            runAsAccessPoint = json["runAsAccessPoint"];
          }

          if (json.containsKey("accessPointSSID")) {
            strncpy(accessPointSSID, json["accessPointSSID"], 32);
            accessPointSSID[31] = '\0';
          }

          if (json.containsKey("accessPointPassword")) {
            strncpy(accessPointPassword, json["accessPointPassword"], 64);
            accessPointPassword[63] = '\0';
          }

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

