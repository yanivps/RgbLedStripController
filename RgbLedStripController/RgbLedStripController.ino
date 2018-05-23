#define startMarker 254
#define endMarker 255
#define specialByte 253
#define maxMessage 15 // Timer packet is the longest we can get and is up to 15 bytes not including start and end markers

#define SET_COLOR_COMMAND 0x01
#define TIMER_ON_COMMAND 0x02
#define TIMER_OFF_COMMAND 0x03

byte bytesRecvd = 0;
byte dataRecvCount = 0;
byte* commandDataPtr;
byte dataRecvd[maxMessage];
byte tempBuffer[maxMessage];

boolean dataReceiveInProgress = false;
boolean allDataReceived = false;

unsigned long currentTime; // save millis()
unsigned long startTimerOff = 0;
unsigned long timerOff = 0;
unsigned long startTimerOn = 0;
unsigned long timerOn = 0;

#define BUTTON_PRESSED LOW
const int onOffButtonPin = 2;

// RGB Led definitions
//pin definitions.  must be PWM-capable pins!
const int rgbRedPin = 5;
const int rgbGreenPin = 6;
const int rgbBluePin = 9;

const int max_red = 255;
const int max_green = 255;
const int max_blue = 100;

byte currentRedPinValue = 255, lastRedPinValue = 0;
byte currentGreenPinValue = 255, lastGreenPinValue = 0;
byte currentBluePinValue = 255, lastBluePinValue = 0;

byte colors[3] = {0, 0, 0}; // array to store led brightness values
byte timerOnColors[3] = {0, 0, 0}; // array to store led brightness values of future timer



void setup()
{
  Serial.begin(115200);

  pinMode(onOffButtonPin, INPUT_PULLUP);
  attachInterrupt(0, buttonStateChanged, FALLING);

  // Set pins to HIGH first so the rgb led which is 'active low' won't briefly flash on power on
  digitalWrite(rgbRedPin, HIGH);
  digitalWrite(rgbGreenPin, HIGH);
  digitalWrite(rgbBluePin, HIGH);
  analogWrite(rgbRedPin, 255);
  analogWrite(rgbGreenPin, 255);
  analogWrite(rgbBluePin, 255);
}

void loop()
{
  getSerialData();
  processData();
  processTimers();
}

void buttonStateChanged() {
  if (digitalRead(onOffButtonPin) == BUTTON_PRESSED) {
    delayMicroseconds(10000); // Cannot use delay in interrupt
    if (digitalRead(onOffButtonPin) == BUTTON_PRESSED) {
      if (currentRedPinValue == 255 && currentGreenPinValue == 255  && currentBluePinValue == 255) { // if turned off, turn on
        currentRedPinValue = lastRedPinValue;
        currentGreenPinValue = lastGreenPinValue;
        currentBluePinValue = lastBluePinValue;
        lastRedPinValue = lastGreenPinValue = lastBluePinValue = 255;
      } else { // if turned on, turn off.
        lastRedPinValue = currentRedPinValue;
        lastGreenPinValue = currentGreenPinValue;
        lastBluePinValue = currentBluePinValue;
        currentRedPinValue = currentGreenPinValue = currentBluePinValue = 255;
      }
      analogWrite(rgbRedPin, currentRedPinValue);
      analogWrite(rgbGreenPin, currentGreenPinValue);
      analogWrite(rgbBluePin, currentBluePinValue);
    }
  }
}

//////////////////////
//  SeriaL Methods  //
//////////////////////

void getSerialData() {

  if(Serial.available() > 0) {

    byte x = Serial.read();
    if (x == endMarker && dataReceiveInProgress) {
      dataReceiveInProgress = false;
      allDataReceived = true;

      decodeHighBytes();

      // UNCOMMENT FOR DEBUGING
       Serial.print("Received: ");
       for (int i = 0; i < dataRecvCount; i++) {
         Serial.print(dataRecvd[i], DEC);
         Serial.print(" ");
       }
       Serial.println();
    }

    if(dataReceiveInProgress) {
      if (bytesRecvd >= maxMessage) {
          bytesRecvd = 0;
          dataReceiveInProgress = false;
      } else {
        tempBuffer[bytesRecvd] = x;
        bytesRecvd ++;
      }
    }

    if (x == startMarker) {
      bytesRecvd = 0;
      dataReceiveInProgress = true;
    }
  }
}

void decodeHighBytes() {
  //  copies to dataRecvd[] only the data bytes i.e. excluding the marker bytes
  //  and converts any bytes which is the 'special byte' etc into the intended numbers
  dataRecvCount = 0;
  for (byte n = 0; n < bytesRecvd; n++) {
    byte x = tempBuffer[n];
    if (x == specialByte) {
       n++;
       x = x + tempBuffer[n];
    }
    dataRecvd[dataRecvCount] = x;
    dataRecvCount ++;
  }
}

void processData() {
  if (!allDataReceived || dataRecvCount == 0) return;

  // Extract command byte
  byte command = dataRecvd[0];
  commandDataPtr = &dataRecvd[1];
  dataRecvCount--;

  switch (command) {
      case SET_COLOR_COMMAND:
        setColorCommand();
        break;
      case TIMER_ON_COMMAND:
        timerOnCommand();
        break;
      case TIMER_OFF_COMMAND:
        timerOffCommand();
        break;
  }
  allDataReceived = false;
}

void setColorCommand() {
  if (dataRecvCount != 3) return; // Invalid command data

  for (int i = 0; i < 3; i++) {
      colors[i] = commandDataPtr[i];
  }

  //set the three PWM pins according to the data read from the Serial port
  analogWriteColors(colors[0], colors[1], colors[2]);
}

void timerOnCommand() {
  if (dataRecvCount != 7) return; // Invalid command data

  for (int i = 0; i < 3; i++) {
      timerOnColors[i] = commandDataPtr[i];
  }

  timerOn = bytesToInt32(commandDataPtr + 3); // skip 3 color bytes
  startTimerOn = millis();
}

void timerOffCommand() {
  if (dataRecvCount != 4) return; // Invalid command data

  timerOff = bytesToInt32(commandDataPtr);
  startTimerOff = millis();
}

void processTimers() {
  currentTime = millis();
  if (timerOff != 0 && currentTime - startTimerOff >= timerOff) {
    // Turn off led
    analogWriteColors(0, 0, 0);
    timerOff = 0;
  }

  if (timerOn != 0 && currentTime - startTimerOn >= timerOn) {
    // Turn on led
    analogWriteColors(timerOnColors[0], timerOnColors[1], timerOnColors[2]);
    timerOn = 0;
  }
}

void analogWriteColors(byte red, byte green, byte blue) {
  if (red == 0 && green == 0 && blue == 0) { // If i about to turn off
    if (currentRedPinValue != 255 || currentGreenPinValue != 255 || currentBluePinValue != 255) { // save current values only if not currently already turend off
      lastRedPinValue = currentRedPinValue;
      lastGreenPinValue = currentGreenPinValue;
      lastBluePinValue = currentBluePinValue;
    }
  }

  // scale the values with map() so that the R, G, and B brightnesses are balanced.
  currentRedPinValue = 255 - map(red, 0, 255, 0, max_red);
  currentGreenPinValue = 255 - map(green, 0, 255, 0, max_green);
  currentBluePinValue = 255 - map(blue, 0, 255, 0, max_blue);
  analogWrite(rgbRedPin, currentRedPinValue);
  analogWrite(rgbGreenPin, currentGreenPinValue);
  analogWrite(rgbBluePin, currentBluePinValue);
}

unsigned long bytesToInt32(byte* bytes) {
  unsigned long value = 0;

  value = (value << 8) + bytes[3];
  value = (value << 8) + bytes[2];
  value = (value << 8) + bytes[1];
  value = (value << 8) + bytes[0];
  return value;
}

