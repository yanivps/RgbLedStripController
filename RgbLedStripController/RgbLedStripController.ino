#define startMarker 254
#define endMarker 255
#define specialByte 253
#define maxMessage 15 // Timer packet is the longest we can get and is up to 15 bytes not including start and end markers

#define SET_COLOR_COMMAND 0x01
#define TIMER_ON_COMMAND 0x02
#define TIMER_OFF_COMMAND 0x03
#define ANIMATION_COMMAND 0x04
#define FADE_ANIMATION 0x01
#define BLINK_ANIMATION 0x02

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

// Animations definitions
unsigned long animationStartTime = 0;
unsigned long animationPeriod = 0;
byte animationType;
boolean randomAnimationColors;
boolean isFadeInitialized;
boolean fadeIn;
float redPinFadeValue, redFadeDiff;
float greenPinFadeValue, greenFadeDiff;
float bluePinFadeValue, blueFadeDiff;
#define FADE_STEP_MILLIS_INTERVAL 10

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

byte currentRedPinValue = 255, lastRedPinValue = 0, redPinBeforeAnimation = 0;
byte currentGreenPinValue = 255, lastGreenPinValue = 0, greenPinBeforeAnimation = 0;
byte currentBluePinValue = 255, lastBluePinValue = 0, bluePinBeforeAnimation = 0;

byte colors[3] = {0, 0, 0}; // array to store led brightness values
byte timerOnColors[3] = {0, 0, 0}; // array to store led brightness values of future timer



void setup()
{
  Serial.begin(115200);
  randomSeed(analogRead(0));

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
  currentTime = millis();
  getSerialData();
  processData();
  processTimers();
  animate();
}

void buttonStateChanged() {
  if (digitalRead(onOffButtonPin) == BUTTON_PRESSED) {
    delayMicroseconds(10000); // Delay for push button debounce. Cannot use delay in interrupt
    if (digitalRead(onOffButtonPin) == BUTTON_PRESSED) {
      stopAnimation(false); // Stop the current animation
      toggleLed();
    }
  }
}

void toggleLed() {
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
      case ANIMATION_COMMAND:
        animationCommand();
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
  startTimerOn = currentTime;
}

void timerOffCommand() {
  if (dataRecvCount != 4) return; // Invalid command data

  timerOff = bytesToInt32(commandDataPtr);
  startTimerOff = currentTime;
}

void animationCommand() {
  if (dataRecvCount != 1 && dataRecvCount != 4) return; // Invalid command data

  if (animationType == NULL) { // No current animation is running
    redPinBeforeAnimation = currentRedPinValue;
    greenPinBeforeAnimation = currentGreenPinValue;
    bluePinBeforeAnimation = currentBluePinValue;
  }

  stopAnimation(true); // Stop the current animation
  if (currentRedPinValue == 255 && currentGreenPinValue == 255  && currentBluePinValue == 255) { // if turned off, don't start animation
    return;
  }

  animationPeriod = bytesToWord(commandDataPtr + 1);
  if (animationPeriod == 0) { // if animationPeriod is 0, don't start animation
    return;
  }

  animationType = commandDataPtr[0];
  randomAnimationColors = commandDataPtr[3];
  animationStartTime = currentTime;
  isFadeInitialized = false;
}

void animate() {
  switch (animationType) {
    case FADE_ANIMATION:
      fade();
      break;
    case BLINK_ANIMATION:
      blink();
      break;
  }
}

void fade() {
  if (!isFadeInitialized) {
    redPinFadeValue = currentRedPinValue;
    greenPinFadeValue = currentGreenPinValue;
    bluePinFadeValue = currentBluePinValue;
    redFadeDiff = (255 - currentRedPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
    greenFadeDiff = (255 - currentGreenPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
    blueFadeDiff = (255 - currentBluePinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
    isFadeInitialized = true;
  }

  if (currentTime - animationStartTime > FADE_STEP_MILLIS_INTERVAL) {
    if (redPinFadeValue == 255 && greenPinFadeValue == 255 && bluePinFadeValue == 255) {
      fadeIn = true;
      if (randomAnimationColors) { // change color and recalculate fade diffs
        do {
          currentRedPinValue = random(255 - max_red, 255);
          currentGreenPinValue = random(255 - max_green, 255);
          currentBluePinValue = random(255 - max_blue, 255);
        } while (currentRedPinValue == 0 && currentGreenPinValue == 0 && currentBluePinValue == 0);
        redFadeDiff = (255 - currentRedPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
        greenFadeDiff = (255 - currentGreenPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
        blueFadeDiff = (255 - currentBluePinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
      }
    }
    if (redPinFadeValue == currentRedPinValue && greenPinFadeValue == currentGreenPinValue && bluePinFadeValue == currentBluePinValue) {
      fadeIn = false;
    }

    redPinFadeValue = fadeIn ? redPinFadeValue - redFadeDiff : redPinFadeValue + redFadeDiff;
    if (redPinFadeValue < currentRedPinValue) redPinFadeValue = currentRedPinValue;
    if (redPinFadeValue > 255) redPinFadeValue = 255;

    greenPinFadeValue = fadeIn ? greenPinFadeValue - greenFadeDiff : greenPinFadeValue + greenFadeDiff;
    if (greenPinFadeValue < currentGreenPinValue) greenPinFadeValue = currentGreenPinValue;
    if (greenPinFadeValue > 255) greenPinFadeValue = 255;

    bluePinFadeValue = fadeIn ? bluePinFadeValue - blueFadeDiff : bluePinFadeValue + blueFadeDiff;
    if (bluePinFadeValue < currentBluePinValue) bluePinFadeValue = currentBluePinValue;
    if (bluePinFadeValue > 255) bluePinFadeValue = 255;

    analogWrite(rgbRedPin, redPinFadeValue);
    analogWrite(rgbGreenPin, greenPinFadeValue);
    analogWrite(rgbBluePin, bluePinFadeValue);

    animationStartTime = currentTime;
  }
}

void blink() {
  if (currentTime - animationStartTime > animationPeriod) {
    // If random animation colors and led is turned off, generate random colors
    if (randomAnimationColors && currentRedPinValue == 255 && currentGreenPinValue == 255  && currentBluePinValue == 255) { // if turned off, and we random animation colors
      do {
        currentRedPinValue = random(255 - max_red, 255);
        currentGreenPinValue = random(255 - max_green, 255);
        currentBluePinValue = random(255 - max_blue, 255);
      } while (currentRedPinValue == 0 && currentGreenPinValue == 0 && currentBluePinValue == 0);
      analogWrite(rgbRedPin, currentRedPinValue);
      analogWrite(rgbGreenPin, currentGreenPinValue);
      analogWrite(rgbBluePin, currentBluePinValue);
    } else { // No random animation colors or led is turned on, toggle led normally
      toggleLed();
    }
    animationStartTime = currentTime;
  }
}

void stopAnimation(boolean revertColors) {
  if (revertColors) {
    // Revert led state to before animation
    analogWrite(rgbRedPin, redPinBeforeAnimation);
    analogWrite(rgbGreenPin, greenPinBeforeAnimation);
    analogWrite(rgbBluePin, bluePinBeforeAnimation);
    currentRedPinValue = redPinBeforeAnimation;
    currentGreenPinValue = greenPinBeforeAnimation;
    currentBluePinValue = bluePinBeforeAnimation;
  }

  animationType = NULL;
}

void resetAnimation() {
  animationStartTime = currentTime;
  isFadeInitialized = false;
}

void processTimers() {
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
  if (red == 0 && green == 0 && blue == 0) { // If I'm about to turn off
    stopAnimation(false); // Stop the current animation
    if (currentRedPinValue != 255 || currentGreenPinValue != 255 || currentBluePinValue != 255) { // save current values only if not currently turend off
      lastRedPinValue = currentRedPinValue;
      lastGreenPinValue = currentGreenPinValue;
      lastBluePinValue = currentBluePinValue;
    }
  } else { // If set new color
    resetAnimation(); // Resets the current animation
  }

  // scale the values with map() so that the R, G, and B brightnesses are balanced.
  currentRedPinValue = 255 - map(red, 0, 255, 0, max_red);
  currentGreenPinValue = 255 - map(green, 0, 255, 0, max_green);
  currentBluePinValue = 255 - map(blue, 0, 255, 0, max_blue);

  analogWrite(rgbRedPin, currentRedPinValue);
  analogWrite(rgbGreenPin, currentGreenPinValue);
  analogWrite(rgbBluePin, currentBluePinValue);

  // Save new color for animation revert
  redPinBeforeAnimation = currentRedPinValue;
  greenPinBeforeAnimation = currentGreenPinValue;
  bluePinBeforeAnimation = currentBluePinValue;
}

unsigned long bytesToInt32(byte* bytes) {
  unsigned long value = 0;

  value = (value << 8) + bytes[3];
  value = (value << 8) + bytes[2];
  value = (value << 8) + bytes[1];
  value = (value << 8) + bytes[0];
  return value;
}

word bytesToWord(byte* bytes) {
  word value = 0;

  value = (value << 8) + bytes[1];
  value = (value << 8) + bytes[0];
  return value;
}

