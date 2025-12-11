#include <Arduino_APDS9960.h>

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

#define NUM_SENSORS 5
#define SMOOTHING_WINDOW 5
#define CALIBRATION_SAMPLES 3

// Pins
const int sensorPins[NUM_SENSORS] = {A0, A1, A2, A3, A4};
SoftwareSerial mySoftwareSerial(10, 11); // RX, TX for DFPlayer
DFRobotDFPlayerMini myDFPlayer;

// Finger calibration structure
typedef struct {
  int straightValue;
  int bentValue;
  int fullyBentValue;
  int currentValue;
  int rawValue;
  int readings[SMOOTHING_WINDOW];
  int readIndex;
} FingerCalibration;

FingerCalibration fingers[NUM_SENSORS];
bool calibrationDone = false;

// ASL command structure with custom file numbers
struct ASLCommand {
  const char* name;
  const char* pattern;
  uint16_t fileNumber;
};

// Customize these mappings as needed (file numbers can be any 1-9999)
ASLCommand aslCommands[] = {
  // Basic commands
  {"Hello", "BSBSS", 2},       // Will play 0101.mp3
  {"Goodbye", "SSSSS", 1},     // Will play 0102.mp3
  {"Thank you", "BSSBS", 6},   // Will play 0103.mp3
  {"Yes", "FFFFB",7},         // Will play 0201.mp3
  {"No", "FFBFF", 4},          // Will play 0202.mp3
  {"Help", "FSFSS", 3},        // Will play 0203.mp3
  
  // Alphabet (A-Z)
  {"A", "SFFFF", 9}, {"B", "BSSSS", 10}, {"C", "SBBBB", 11},
  {"D", "BSBBB", 12}, {"E", "FBBBB", 13}, {"F", "BBSSS", 14},
  {"G", "SSBBB", 15}, {"H", "BSSFF", 16}, {"I", "FFFFS", 17},
  {"J", "BFFFS", 18}, {"K", "SSSFF", 19}, {"L", "SSFFF", 20},
  
  {"M", "FFFFF", 21}, {"N", "BBBFF", 22}, {"O", "BBBBB", 23},
  {"P", "SSBFF", 24}, {"Q", "SSBBF", 25}, {"R", "BSBFF", 26},
  {"S", "BFFFF", 27}, {"T", "SBBFF", 28}, {"U", "BSSBB", 29},
  {"V", "FSSFF", 30}, {"W", "FSSSF", 31}, {"X", "FBFFF", 32},
  {"Y", "SFFFS", 33}, {"Z", "FSFFF", 34},
  
  // Additional phrases
  {"I love you", "SSFFS", 401},      // Will play 0401.mp3
  {"What's your name", "FSFFS", 8}, // Will play 0402.mp3
  {"Skibidi", "SBSBS", 5}          // Will play 0501.mp3
};
const int aslCommandCount = sizeof(aslCommands)/sizeof(aslCommands[0]);

void setup() {
  Serial.begin(9600);
  mySoftwareSerial.begin(9600);
  
  Serial.println(F("Initializing DFPlayer..."));
  delay(1000);
  
  if (!myDFPlayer.begin(mySoftwareSerial)) {
    Serial.println(F("Connection error:"));
    Serial.println(F("1. Check DFPlayer wiring (RX->10, TX->11)"));
    Serial.println(F("2. Insert formatted SD card (FAT32)"));
    while(true);
  }
  
  Serial.println(F("DFPlayer ready"));
  myDFPlayer.volume(20);  // Set volume (0-30)
  myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);

  // Initialize finger sensors
  for (int i = 0; i < NUM_SENSORS; i++) {
    fingers[i].straightValue = 0;
    fingers[i].bentValue = 0;
    fingers[i].fullyBentValue = 0;
    fingers[i].currentValue = 0;
    fingers[i].rawValue = 0;
    fingers[i].readIndex = 0;
    for (int j = 0; j < SMOOTHING_WINDOW; j++) {
      fingers[i].readings[j] = 0;
    }
  }

  Serial.println(F("\nFlex Sensor Glove System"));
  Serial.println(F("Press any key to begin calibration"));
}

void loop() {
  if (!calibrationDone) {
    if (Serial.available()) {
      while (Serial.available()) Serial.read();
      calibrateSensors();
      calibrationDone = true;
      Serial.println(F("Calibration complete! System ready."));
    }
    return;
  }

  readAndProcessSensors();
  String currentState = getCurrentHandState();
  Serial.print(F("Hand State: "));
  Serial.println(currentState);
  
  // Check for matching ASL commands
  for (int i = 0; i < aslCommandCount; i++) {
    if (currentState.equals(aslCommands[i].pattern)) {
      Serial.print(F("Playing: "));
      Serial.print(aslCommands[i].name);
      Serial.print(F(" (File "));
      Serial.print(aslCommands[i].fileNumber);
      Serial.println(F(".mp3)"));
      
      myDFPlayer.play(aslCommands[i].fileNumber);
      delay(2000); // Wait for sound to play
      break;
    }
  }
  delay(100);
}

void calibrateSensors() {
  Serial.println(F("\n=== CALIBRATION STARTED ==="));
  Serial.println(F("Will take 3 measurements for each position"));
  
  // Calibrate STRAIGHT position
  Serial.println(F("\nHold ALL fingers STRAIGHT and press any key"));
  waitForSerialInput();
  for (int i = 0; i < NUM_SENSORS; i++) {
    fingers[i].straightValue = getCalibrationValue(sensorPins[i], "Straight");
  }

  // Calibrate BENT position
  Serial.println(F("\nHold ALL fingers BENT and press any key"));
  waitForSerialInput();
  for (int i = 0; i < NUM_SENSORS; i++) {
    fingers[i].bentValue = getCalibrationValue(sensorPins[i], "Bent");
  }

  // Calibrate FULLY BENT position
  Serial.println(F("\nHold ALL fingers FULLY BENT and press any key"));
  waitForSerialInput();
  for (int i = 0; i < NUM_SENSORS; i++) {
    fingers[i].fullyBentValue = getCalibrationValue(sensorPins[i], "Fully Bent");
  }

  Serial.println(F("\n=== CALIBRATION COMPLETE ==="));
  printCalibrationValues();
}

int getCalibrationValue(int pin, const char* position) {
  int samples[CALIBRATION_SAMPLES];
  int sum = 0;
  
  Serial.print(position);
  Serial.println(F(" position - Taking 3 samples..."));
  
  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    samples[i] = analogRead(pin);
    sum += samples[i];
    Serial.print(F("Sample "));
    Serial.print(i+1);
    Serial.print(F(": "));
    Serial.println(samples[i]);
    delay(200);
  }
  
  int mean = sum / CALIBRATION_SAMPLES;
  Serial.print(F("Mean: "));
  Serial.println(mean);
  
  return mean;
}

void waitForSerialInput() {
  while (!Serial.available());
  while (Serial.available()) Serial.read();
  delay(500);
}

void readAndProcessSensors() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    fingers[i].rawValue = analogRead(sensorPins[i]);
    
    fingers[i].readings[fingers[i].readIndex] = fingers[i].rawValue;
    fingers[i].readIndex = (fingers[i].readIndex + 1) % SMOOTHING_WINDOW;
    
    int total = 0;
    for (int j = 0; j < SMOOTHING_WINDOW; j++) {
      total += fingers[i].readings[j];
    }
    fingers[i].currentValue = total / SMOOTHING_WINDOW;
  }
}

char getFingerState(int fingerIndex) {
  int value = fingers[fingerIndex].currentValue;
  int straight = fingers[fingerIndex].straightValue;
  int bent = fingers[fingerIndex].bentValue;
  int fullyBent = fingers[fingerIndex].fullyBentValue;
  
  // Using mean ±5 range for decision
  if (abs(value - straight) <= 5) return 'S';
  else if (abs(value - bent) <= 5) return 'B';
  else if (abs(value - fullyBent) <= 5) return 'F';
  
  // If not in clear range, estimate position
  if (value < bent) return (value < (straight+bent)/2) ? 'S' : 'B';
  else return (value < (bent+fullyBent)/2) ? 'B' : 'F';
}

String getCurrentHandState() {
  String state;
  for (int i = 0; i < NUM_SENSORS; i++) {
    state += getFingerState(i);
  }
  return state;
}

void printCalibrationValues() {
  Serial.println(F("\nCalibration Values:"));
  Serial.println(F("Finger\tStraight\tBent\tFully Bent"));
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.print(i);
    Serial.print('\t');
    Serial.print(fingers[i].straightValue);
    Serial.print(F("\t\t"));
    Serial.print(fingers[i].bentValue);
    Serial.print('\t');
    Serial.println(fingers[i].fullyBentValue);
    
    if (fingers[i].straightValue >= fingers[i].bentValue || 
        fingers[i].bentValue >= fingers[i].fullyBentValue) {
      Serial.println(F("WARNING: Invalid calibration!"));
    }
  }
}

void printCombinedState() {
  Serial.print(F("Hand: "));
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.print(getFingerState(i));
  }
  Serial.println();
}