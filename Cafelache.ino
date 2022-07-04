#include <HX711.h>
#include <TM1637Display.h>
#include <ESP8266WiFi.h>
#include <HCSR04.h>
#include <EasyButton.h>

#define SCALE_DOUT_PIN 4
#define SCALE_CLK_PIN 5
#define DIST_SENSOR_TRIG_PIN 0
#define DIST_SENSOR_ECHO_PIN 16
#define DISPLAY_CLK 12
#define DISPLAY_DIO 14
#define GRINDER_START_PIN 13
#define BTN_PIN 3

#define LONG_PRESS_DURATION 1500  // long press is used for starting the grinder
#define WEIGHT_DIFF 0.2 // Stop as targetWeight - WEIGHT_DIFF grams 
#define SINGLE_CALIBRATION_WEIGHT 477  // weight of tamping station + portafilter + single basket + funnel
#define DOUBLE_CALIBRATION_WEIGHT 480  // weight of tamping station + portafilter + double basket + funnel
#define DEFAULT_CALIBRATION_WEIGHT DOUBLE_CALIBRATION_WEIGHT
#define DEFAULT_CALIBRATION_FACTOR -818
#define GRIND_SPEED 1.25 // grams/second
#define STEPS_UNTIL_FINISH 3
#define DEFAULT_SHOT_COUNT 2
#define MIN_RUNTIME_FACTOR 0.8
#define MAX_RUNTIME_FACTOR 1.6

#define _CAFELACHE_DEBUG 1; // comment out to disable debug

HX711 scale;    // parameter "gain" is ommited; the default value 128 is used by the library
TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);
UltraSonicDistanceSensor distanceSensor(DIST_SENSOR_TRIG_PIN, DIST_SENSOR_ECHO_PIN);
EasyButton button(BTN_PIN);
unsigned long volatile lastDisplayTime = 0, firstReadingTime;
long zeroFactor;
float volatile val;
unsigned long startTime;
float oneShotWeight = 8, twoShotsWeight = 14, targetWeight = twoShotsWeight - WEIGHT_DIFF;
bool volatile isStopped = true;
bool volatile needsCalibration = true, calibrationStarted = false;
uint8_t volatile shotCount = DEFAULT_SHOT_COUNT;
int volatile stepsUntilFinish = STEPS_UNTIL_FINISH;

void onButtonLongPressed() {
  #ifdef _CAFELACHE_DEBUG
  Serial.print("Start grinding. Shots: ");
  Serial.println(shotCount);
  Serial.print("Target weight: ");
  Serial.println(targetWeight);
  #endif
  scale.tare();
  startGrinder();
}

void calibrateScale(float calibrationWeight = DEFAULT_CALIBRATION_WEIGHT)
{
  display.clear();
  long average = scale.read_average(10);
  //scale.set_offset(zeroFactor);
  float calibrationFactor = ((average - zeroFactor) / calibrationWeight);
  // protection
  if (calibrationFactor < DEFAULT_CALIBRATION_FACTOR - 200 || calibrationFactor > DEFAULT_CALIBRATION_FACTOR + 200) {
    Serial.println("Calibration failed");
    display.showNumberDec(0);
    delay(500);
    display.clear();
    delay(500);
    display.showNumberDec(0);
    calibrationStarted = false;
    return;
  }
  
  scale.set_scale(calibrationFactor);
  scale.tare();
  //scale.set_offset(average);

  needsCalibration = false;
  calibrationStarted = false;
  display.showNumberDec(shotCount);
  #ifdef _CAFELACHE_DEBUG
  Serial.print("New calibration factor: ");
  Serial.println(calibrationFactor);
  #endif
}

void onButtonPressed() {
  #ifdef _CAFELACHE_DEBUG
  Serial.println("Button pressed");
  #endif

  if (!isStopped) {
    stopGrinder();
    return;
  }
  
  if (needsCalibration) {
    calibrationStarted = true;
    return;
  }
  
  shotCount = shotCount == 1 ? 2 : 1;
  needsCalibration = true;
  calibrationStarted = true;
  targetWeight = ((shotCount == 1 ? oneShotWeight : twoShotsWeight) - WEIGHT_DIFF);
  display.showNumberDec(shotCount);
  #ifdef _CAFELACHE_DEBUG
  Serial.print("Switched to shot count ");
  Serial.println(shotCount);
  #endif
}

void setup() {
  pinMode(GRINDER_START_PIN, OUTPUT); 
  digitalWrite(GRINDER_START_PIN, HIGH);
  pinMode(BTN_PIN, INPUT_PULLUP);
  calibrationStarted = false;

  if (button.supportsInterrupt()) {
    button.enableInterrupt(btnIntrCallback);
  }

  Serial.begin(9600, SERIAL_8N1, SERIAL_TX_ONLY); 

  button.begin();

  // Attach callbacks
  button.onPressedFor(LONG_PRESS_DURATION, onButtonLongPressed);
  button.onPressed(onButtonPressed);

  WiFi.mode(WIFI_OFF);
  
  scale.begin(SCALE_DOUT_PIN, SCALE_CLK_PIN);
  display.clear();
  display.setBrightness(1);
  uint8_t data[] = {0x0, 0x0, 0x0, 0x0};
  data[2]= display.encodeDigit(12);
  display.setSegments(data);
  
  Serial.println("Cafelache - your espresso buddy");

  scale.set_scale(DEFAULT_CALIBRATION_FACTOR); // this value is obtained by calibrating the scale with known weights
  scale.tare();         // reset the scale to 0
  zeroFactor = scale.read_average(20); // Get a baseline reading
  //Serial.println(zeroFactor);
}

void startGrinder()
{
  digitalWrite(GRINDER_START_PIN, LOW); // start grinder
  #ifdef _CAFELACHE_DEBUG
  Serial.print("Starting grinder");
  #endif
  isStopped = false;
  startTime = millis();
  stepsUntilFinish = STEPS_UNTIL_FINISH;
}

void stopGrinder()
{
  digitalWrite(GRINDER_START_PIN, HIGH); // stop grinder
  isStopped = true;

  #ifdef _CAFELACHE_DEBUG
  Serial.print("Stopped grinder at ");
  Serial.println(val);
  #endif

  // wait 2s for all remaining coffee to fall
  delay(2000);

  // calculate a 5 readings average from a more accurate reading
  val = 0;
  for (uint8_t i = 1; i <= 5; i++) {
    val += scale.get_units(2);
    delay(80);
  }

  val /= 5.;
  // round to one decimal
  val = (float)((int)(val * 10.) / 10.);

  #ifdef _CAFELACHE_DEBUG
  Serial.print("Stable val: ");
  Serial.println(val);
  #endif
  
  display.showNumberDecEx((int)(val * 100), 0b11100000, false, 4, 0 );
  delay(5000);
  display.showNumberDec(shotCount);
}

ICACHE_RAM_ATTR void btnIntrCallback()
{
  button.read();
}

void loop() {

  // needed for long press. short press is handled by an intrerrupt
  button.update();

  // handle calibration event
  if (calibrationStarted) {
    calibrateScale(shotCount == 1 ? SINGLE_CALIBRATION_WEIGHT : DOUBLE_CALIBRATION_WEIGHT);
    return;
  }
  
  if (isStopped || needsCalibration) {
    #ifdef _CAFELACHE_DEBUG
    Serial.println(scale.get_units(5));
    #endif

    float distance = distanceSensor.measureDistanceCm();
    if (distance < 0) {
      distance = 0;
    }

    // 50ml per coffee
    // 20 .. 2500ml
    // x .. ? (x = 22 - distance)
    // => ? = x * 2500 / 20 
    // no_of_coffees = ? / 50

    /*float remainingDistance = 22 - distance;
    float waterInMilliliters = remainingDistance * 2500.0 / 20.0;
    unsigned int availableCoffees = round(waterInMilliliters / 50.0);
    if (availableCoffees > 2500 / 50) {
      availableCoffees = 0;
    }*/
    const int tankCapacity = 2500;
    const int waterInMlPerShot = 80;
    const uint8_t tankHeightInCm = 20;
    const uint8_t gapFromSensorInCm = 2;

    float remainingDistance = tankHeightInCm + gapFromSensorInCm - distance;
    float waterInMilliliters = remainingDistance * tankCapacity / (float)tankHeightInCm;
    unsigned int availableCoffees = round(waterInMilliliters / (float)waterInMlPerShot);
    if (availableCoffees > round(tankCapacity / (float)waterInMlPerShot)) {
      availableCoffees = 0;
    }

    display.showNumberDec(availableCoffees, false, 2, 0);
    
    #ifdef _CAFELACHE_DEBUG
    Serial.print("Distance: ");
    Serial.println(distance);
    #endif

    
    delay(100);
    return;
  }

  val = scale.get_units(2);

  // weight test. grinder must run at least MIN_RUNTIME_FACTOR (40%) of the time required with the typical speed
  if (val >= targetWeight) {
    #ifdef _CAFELACHE_DEBUG
    Serial.printf("Reached target weight: %.2f. Remaining steps: %d\n", val, stepsUntilFinish);
    #endif
    
    stepsUntilFinish--;
    if (stepsUntilFinish <= 0 &&
      millis() - startTime > (MIN_RUNTIME_FACTOR * targetWeight / GRIND_SPEED) * 1000) {
        
      Serial.println("Stopping grinder...");
      stopGrinder();
      return;
    }
  }

  // this should not happen
  if (val < -2) {
    Serial.println("Invalid reading protection");
    stopGrinder();
    return;
  }

  // timeout protection. grinder must not run more than MAX_RUNTIME_FACTOR (40%) than the typical speed
  if (millis() - startTime > ((MAX_RUNTIME_FACTOR * targetWeight) / GRIND_SPEED) * 1000) {
    Serial.println("Timeout protection");
    stopGrinder();
    return;
  }

  if (millis() - lastDisplayTime > 600) {
    #ifdef _CAFELACHE_DEBUG
    Serial.print("Reading: ");
    Serial.println(val);
    #endif
    display.showNumberDec(val);
    lastDisplayTime = millis();
  }
   
}
