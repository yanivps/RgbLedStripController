#define startMarker 254
#define endMarker 255
#define specialByte 253
#define maxMessage 15 // Timer packet is the longest we can get and is up to 15 bytes not including start and end markers

#define SET_COLOR_COMMAND 0x01
#define TIMER_ON_COMMAND 0x02
#define TIMER_OFF_COMMAND 0x03
#define ANIMATION_COMMAND 0x04
#define SET_COLOR_WITH_FADE_COMMAND 0x05
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
unsigned long singleFadePeriod = 0;
byte animationType;
boolean randomAnimationColors;
boolean isFadeInitialized;
boolean fadeIn;
boolean singleFadeRequested;
float redPinFadeValue, redFadeInDiff, redFadeOutDiff;
float greenPinFadeValue, greenFadeInDiff, greenFadeOutDiff;
float bluePinFadeValue, blueFadeInDiff, blueFadeOutDiff;
#define FADE_STEP_MILLIS_INTERVAL 10

#define BUTTON_PRESSED LOW
const int onOffButtonPin = 2;

// RGB Led definitions
//pin definitions.  must be PWM-capable pins!
const int rgbRedPin = 5;
const int rgbGreenPin = 6;
const int rgbBluePin = 3;

const int max_red = 255;
const int max_green = 255;
const int max_blue = 100;

byte currentRedPinValue = 0, lastRedPinValue = 255, redPinBeforeAnimation = 255, singleFadeRedPinValue = 0;
byte currentGreenPinValue = 0, lastGreenPinValue = 255, greenPinBeforeAnimation = 255, singleFadeGreenPinValue = 0;
byte currentBluePinValue = 0, lastBluePinValue = 255, bluePinBeforeAnimation = 255, singleFadeBluePinValue = 0;

byte colors[3] = {0, 0, 0}; // array to store led brightness values
byte timerOnColors[3] = {0, 0, 0}; // array to store led brightness values of future timer



void setup()
{
  Serial.begin(57600);
  randomSeed(analogRead(0));

  pinMode(onOffButtonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(onOffButtonPin), buttonStateChanged, FALLING);

  // Set pins to HIGH first so the rgb led which is 'active low' won't briefly flash on power on
  digitalWrite(rgbRedPin, HIGH);
  digitalWrite(rgbGreenPin, HIGH);
  digitalWrite(rgbBluePin, HIGH);
  analogWrite(rgbRedPin, 0);
  analogWrite(rgbGreenPin, 0);
  analogWrite(rgbBluePin, 0);
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
  if (currentRedPinValue == 0 && currentGreenPinValue == 0  && currentBluePinValue == 0) { // if turned off, turn on
    currentRedPinValue = lastRedPinValue;
    currentGreenPinValue = lastGreenPinValue;
    currentBluePinValue = lastBluePinValue;
    lastRedPinValue = lastGreenPinValue = lastBluePinValue = 0;
  } else { // if turned on, turn off.
    lastRedPinValue = currentRedPinValue;
    lastGreenPinValue = currentGreenPinValue;
    lastBluePinValue = currentBluePinValue;
    currentRedPinValue = currentGreenPinValue = currentBluePinValue = 0;
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
      case SET_COLOR_WITH_FADE_COMMAND:
        setColorWithFadeCommand();
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

void setColorWithFadeCommand() {
  if (dataRecvCount != 5) return; // Invalid command data

  for (int i = 0; i < 3; i++) {
      colors[i] = commandDataPtr[i];
  }

  // When there is an active animation, change color as if it were a setColorCommand
  if (animationType != NULL) {
    analogWriteColors(colors[0], colors[1], colors[2]);
    return;
  }

  singleFadeRequested = true;
  singleFadePeriod = bytesToWord(commandDataPtr + 3);

  singleFadeRedPinValue = colors[0];
  singleFadeGreenPinValue = colors[1];
  singleFadeBluePinValue = colors[2];

  animationType = FADE_ANIMATION;
  animationStartTime = currentTime;
  isFadeInitialized = false;
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
  if (currentRedPinValue == 0 && currentGreenPinValue == 0  && currentBluePinValue == 0) { // if turned off, don't start animation
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
  singleFadeRequested = false;
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

    if (singleFadeRequested) {
      // Calculate fade in to the destination we want to reach with the color of the single fade
      redFadeInDiff = (singleFadeRedPinValue) / (float) singleFadePeriod * FADE_STEP_MILLIS_INTERVAL;
      greenFadeInDiff = (singleFadeGreenPinValue) / (float) singleFadePeriod * FADE_STEP_MILLIS_INTERVAL;
      blueFadeInDiff = (singleFadeBluePinValue) / (float) singleFadePeriod * FADE_STEP_MILLIS_INTERVAL;
      // Calculate fade out based on the current color that we want to slowly turn off
      redFadeOutDiff = (currentRedPinValue) / (float) singleFadePeriod * FADE_STEP_MILLIS_INTERVAL;
      greenFadeOutDiff = (currentGreenPinValue) / (float) singleFadePeriod * FADE_STEP_MILLIS_INTERVAL;
      blueFadeOutDiff = (currentBluePinValue) / (float) singleFadePeriod * FADE_STEP_MILLIS_INTERVAL;

      if (redPinFadeValue == 0 && greenPinFadeValue == 0 && bluePinFadeValue == 0) {
        // If starting single fade when turned off:
        fadeIn = true;
      } else {
        // If starting single fade when already turned on (with some color):
        fadeIn = false;
      }
      // Set the destination we want to reach with the color of the single fade
      currentRedPinValue = singleFadeRedPinValue;
      currentGreenPinValue = singleFadeGreenPinValue;
      currentBluePinValue = singleFadeBluePinValue;
    } else {
      redFadeInDiff = (currentRedPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
      greenFadeInDiff = (currentGreenPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
      blueFadeInDiff = (currentBluePinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
      // Same diff for fade in and fade out when animating same color (not running single animation)
      redFadeOutDiff = redFadeInDiff;
      greenFadeOutDiff = greenFadeInDiff;
      blueFadeOutDiff = blueFadeInDiff;
    }
    isFadeInitialized = true;
  }

  if (currentTime - animationStartTime > FADE_STEP_MILLIS_INTERVAL) {
    if (redPinFadeValue == 0 && greenPinFadeValue == 0 && bluePinFadeValue == 0) {
      fadeIn = true;
      if (randomAnimationColors) { // change color and recalculate fade diffs
        do {
          currentRedPinValue = random(0, max_red);
          currentGreenPinValue = random(0, max_green);
          currentBluePinValue = random(0, max_blue);
        } while (currentRedPinValue == 0 && currentGreenPinValue == 0 && currentBluePinValue == 0);
        redFadeInDiff = (currentRedPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
        greenFadeInDiff = (currentGreenPinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
        blueFadeInDiff = (currentBluePinValue) / (float) animationPeriod * FADE_STEP_MILLIS_INTERVAL;
        redFadeOutDiff = redFadeInDiff;
        greenFadeOutDiff = greenFadeInDiff;
        blueFadeOutDiff = blueFadeInDiff;
      }
    }
    if (redPinFadeValue == currentRedPinValue && greenPinFadeValue == currentGreenPinValue && bluePinFadeValue == currentBluePinValue) {
      fadeIn = false;
      if (singleFadeRequested) {
        animationType = NULL;
        return;
      }
    }

    redPinFadeValue = fadeIn ? redPinFadeValue + redFadeInDiff : redPinFadeValue - redFadeOutDiff;
    if (fadeIn && redPinFadeValue > currentRedPinValue) redPinFadeValue = currentRedPinValue;
    if (!fadeIn && redPinFadeValue < 0) redPinFadeValue = 0;

    greenPinFadeValue = fadeIn ? greenPinFadeValue + greenFadeInDiff : greenPinFadeValue - greenFadeOutDiff;
    if (fadeIn && greenPinFadeValue > currentGreenPinValue) greenPinFadeValue = currentGreenPinValue;
    if (!fadeIn && greenPinFadeValue < 0) greenPinFadeValue = 0;

    bluePinFadeValue = fadeIn ? bluePinFadeValue + blueFadeInDiff : bluePinFadeValue - blueFadeOutDiff;
    if (fadeIn && bluePinFadeValue > currentBluePinValue) bluePinFadeValue = currentBluePinValue;
    if (!fadeIn && bluePinFadeValue < 0) bluePinFadeValue = 0;

    analogWrite(rgbRedPin, redPinFadeValue);
    analogWrite(rgbGreenPin, greenPinFadeValue);
    analogWrite(rgbBluePin, bluePinFadeValue);

    animationStartTime = currentTime;
  }
}

void blink() {
  if (currentTime - animationStartTime > animationPeriod) {
    // If random animation colors and led is turned off, generate random colors
    if (randomAnimationColors && currentRedPinValue == 0 && currentGreenPinValue == 0  && currentBluePinValue == 0) { // if turned off, and we random animation colors
      do {
        currentRedPinValue = random(0, max_red);
        currentGreenPinValue = random(0, max_green);
        currentBluePinValue = random(0, max_blue);
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
    if (currentRedPinValue != 0 || currentGreenPinValue != 0 || currentBluePinValue != 0) { // save current values only if not currently turend off
      lastRedPinValue = currentRedPinValue;
      lastGreenPinValue = currentGreenPinValue;
      lastBluePinValue = currentBluePinValue;
    }
  } else { // If set new color
    resetAnimation(); // Resets the current animation
  }

  // scale the values with map() so that the R, G, and B brightnesses are balanced.
  currentRedPinValue = map(red, 0, 255, 0, max_red);
  currentGreenPinValue = map(green, 0, 255, 0, max_green);
  currentBluePinValue = map(blue, 0, 255, 0, max_blue);

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
