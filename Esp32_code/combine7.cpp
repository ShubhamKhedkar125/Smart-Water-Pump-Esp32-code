/*
 ═══════════════════════════════════════════════════════
   SKYTECH IoT — Smart Water Pump Control System
   FINAL STABLE CODE (WEB + OFFLINE PERFECT) ✅ FIXED
 ═══════════════════════════════════════════════════════
*/

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi & Firebase
const char* WIFI_SSID = getenv("WIFI_SSID");
const char* WIFI_PASSWORD = getenv("WIFI_PASSWORD");

const char* FIREBASE_HOST = getenv("FIREBASE_HOST");
const char* FIREBASE_API_KEY = getenv("FIREBASE_API_KEY");

const char* USER_EMAIL = getenv("USER_EMAIL");
const char* USER_PASSWORD = getenv("USER_PASSWORD");

// Pins
#define TRIG_PIN    5
#define ECHO_PIN    18
#define RELAY_PIN   23
#define SWITCH_PIN  19
#define VOLTAGE_PIN 34
#define CURRENT_PIN 35
#define RED_LED      4
#define YELLOW_LED  16
#define GREEN_LED   17

// Thresholds
float MIN_VOLTAGE  = 6.0;
float MAX_VOLTAGE  = 7.5;
float MIN_WATER    = 8.0;
float MAX_CURRENT  = 1.5;
float DRY_RUN_THRESHOLD = 0.4;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Firebase
FirebaseData fbdo, fbCmd;
FirebaseAuth fbAuth;
FirebaseConfig fbConfig;

// State
float distance = 0, waterLevel = 0, voltage = 0;
float current = 0, filteredCurrent = 0;

bool motorState = false;
bool lastMotorCmd = false;

// Switch toggle
bool lastSwitchState = HIGH;

// Faults
bool faultVoltage = false, faultOverload = false;
bool faultDryRun = false, faultLowWater = false;

// Startup delay
unsigned long motorStartTime = 0;
bool startupDelayActive = false;
#define STARTUP_DELAY 10000

// Timing
unsigned long lastSendTime=0,lastCmdTime=0,lastThreshTime=0,lastLcdTime=0;

const unsigned long SEND_INTERVAL=2000;
const unsigned long CMD_INTERVAL=500;
const unsigned long THRESH_INTERVAL=5000;
const unsigned long LCD_INTERVAL=2000;

// ═════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN,OUTPUT);
  pinMode(ECHO_PIN,INPUT);
  pinMode(RELAY_PIN,OUTPUT);
  pinMode(SWITCH_PIN,INPUT_PULLUP);
  pinMode(RED_LED,OUTPUT);
  pinMode(YELLOW_LED,OUTPUT);
  pinMode(GREEN_LED,OUTPUT);

  setRelay(false);

  Wire.begin(21,22);
  lcd.init();
  lcd.backlight();
  lcdShow("SkyTech System","Starting...");

  connectWiFi();
  connectFirebase();
}

// ═════════════════════════════
void loop() {
  unsigned long now = millis();

  readWaterLevel();
  readVoltage();
  readCurrent();

  if (Firebase.ready()) {
    if (now-lastCmdTime>CMD_INTERVAL){lastCmdTime=now;checkMotorCommand();}
    if (now-lastThreshTime>THRESH_INTERVAL){lastThreshTime=now;checkThresholds();}
    if (now-lastSendTime>SEND_INTERVAL){lastSendTime=now;sendToFirebase();}
  }

  controlLogic();
  updateLEDs();

  if (now-lastLcdTime>LCD_INTERVAL){lastLcdTime=now;updateLCD();}

  printSerial();
}

// ═════════════════════════════
void controlLogic() {

  // 🔥 FORCE STOP from Firebase (HIGHEST PRIORITY)
  if (!lastMotorCmd && motorState) {
    motorState = false;
    setRelay(false);
    Serial.println("⛔ Motor OFF by USER (Firebase)");
    return;
  }

  // Switch toggle
  bool currentSwitchState = digitalRead(SWITCH_PIN);

  if (lastSwitchState == HIGH && currentSwitchState == LOW) {
    lastMotorCmd = !lastMotorCmd;
    Firebase.RTDB.setBool(&fbdo,"/command/motorOn",lastMotorCmd);
  }
  lastSwitchState = currentSwitchState;

  // Voltage
  if (voltage > 1.0)
    faultVoltage = !(voltage>=MIN_VOLTAGE && voltage<=MAX_VOLTAGE);
  else
    faultVoltage = false;

  // Water
  faultLowWater = (waterLevel <= MIN_WATER);

  bool canRun = !faultVoltage && !faultLowWater && lastMotorCmd;

  // START
  if (canRun && !motorState) {
    motorState=true;
    motorStartTime=millis();
    startupDelayActive=true;
    setRelay(true);

    Serial.println("✅ Motor STARTED");
  }

  // STOP
  if (!canRun && motorState) {
    motorState=false;
    setRelay(false);
    Serial.println("⛔ Motor STOPPED");
    updateFirebaseMotorOff("Condition Fail");
  }

  // Startup delay
  if (startupDelayActive) {
    if (millis()-motorStartTime>=STARTUP_DELAY) {
      startupDelayActive=false;
      Serial.println("Motor stabilized");
    } else {
      return;
    }
  }

  // Overload
  if (motorState && current > MAX_CURRENT) {
    faultOverload=true;
    motorState=false;
    setRelay(false);
    lastMotorCmd=false;
    updateFirebaseMotorOff("Overload");
    return;
  }

  // Dry run
  if (motorState && !startupDelayActive && current < DRY_RUN_THRESHOLD) {
    faultDryRun=true;
    motorState=false;
    setRelay(false);
    lastMotorCmd=false;
    updateFirebaseMotorOff("Dry Run");
    return;
  }

  if (motorState){
    faultOverload=false;
    faultDryRun=false;
  }
}

// ═════════════════════════════
void checkMotorCommand(){

  if(Firebase.RTDB.getBool(&fbCmd,"/command/motorOn")){
    
    bool newCmd = fbCmd.boolData();

    if (newCmd != lastMotorCmd) {
      lastMotorCmd = newCmd;

      Serial.println("🌐 Firebase Cmd: " + String(newCmd));

      // 🔥 Immediate OFF response
      if (!newCmd && motorState) {
        motorState = false;
        setRelay(false);
        Serial.println("⛔ Motor OFF from Firebase");
      }
    }
  }
}

// ═════════════════════════════
void readWaterLevel(){
  digitalWrite(TRIG_PIN,LOW);delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH);delayMicroseconds(10);
  digitalWrite(TRIG_PIN,LOW);
  long dur=pulseIn(ECHO_PIN,HIGH,30000);
  distance=dur*0.034/2.0;
  waterLevel=((20-distance)/(20-2))*100;
  waterLevel=constrain(waterLevel,0,100);
}

void readVoltage(){
  int raw=analogRead(VOLTAGE_PIN);
  voltage=raw*(3.3/4095.0)*5.5;
}

void readCurrent(){
  long sum=0;
  for(int i=0;i<200;i++){
    sum+=analogRead(CURRENT_PIN);
    delayMicroseconds(150);
  }
  float avg=sum/200.0;
  float sensorVolt=avg*(3.3/4095.0);

  float rawCurrent=(sensorVolt-2.46)/0.185;

  filteredCurrent=0.2*rawCurrent+0.8*filteredCurrent;
  current=filteredCurrent;

  if(abs(current)<0.08) current=0;
  if(current<0) current=0;
}

// ═════════════════════════════
void setRelay(bool on){
  digitalWrite(RELAY_PIN,on?LOW:HIGH);
}

void updateLEDs(){
  bool fault=faultVoltage||faultOverload||faultDryRun||faultLowWater;

  digitalWrite(RED_LED,fault);
  digitalWrite(GREEN_LED,motorState && !fault);
  digitalWrite(YELLOW_LED,!motorState && !fault);
}

void sendToFirebase(){
  FirebaseJson json;
  json.set("water",waterLevel);
  json.set("volt",voltage);
  json.set("curr",current);
  json.set("motorOn",motorState);

  Firebase.RTDB.setJSON(&fbdo,"/sensors",&json);
}

void checkThresholds(){
  FirebaseData td;
  if(Firebase.RTDB.getFloat(&td,"/thresholds/minWater")) MIN_WATER=td.floatData();
}

void updateFirebaseMotorOff(String reason){
  Firebase.RTDB.setBool(&fbdo,"/command/motorOn",false);
  Firebase.RTDB.setString(&fbdo,"/status/safetyReason",reason);
}

// ═════════════════════════════
void lcdShow(String l1,String l2){
  lcd.clear();
  lcd.setCursor(0,0);lcd.print(l1);
  lcd.setCursor(0,1);lcd.print(l2);
}

void updateLCD(){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("W:");lcd.print((int)waterLevel);lcd.print("% ");
  lcd.print(motorState?"ON":"OFF");

  lcd.setCursor(0,1);
  lcd.print("V:");lcd.print(voltage,1);
  lcd.print(" I:");lcd.print(current,2);
}

// ═════════════════════════════
void printSerial(){
  Serial.print("W:");Serial.print(waterLevel);


  Serial.print(" V:");Serial.print(voltage);
  Serial.print(" I:");Serial.print(current);
  Serial.print(" M:");Serial.println(motorState);
}

// ═════════════════════════════
void connectWiFi(){
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  while(WiFi.status()!=WL_CONNECTED){delay(500);}
}

void connectFirebase(){
  fbConfig.host=FIREBASE_HOST;
  fbConfig.api_key=FIREBASE_API_KEY;
  fbAuth.user.email=USER_EMAIL;
  fbAuth.user.password=USER_PASSWORD;
  Firebase.begin(&fbConfig,&fbAuth);
  Firebase.reconnectWiFi(true);
}