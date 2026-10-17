// https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html

// https://github.com/kerikun11/ESP32Time hier aut. synch 24h interne rtc 

// todo connect zusammenführen
// todo mirrornr und ip zusammenführen
// todo warum fällt v1 scheinbar öfter aus? offline


#ifdef ESP8266
  #include "ESP8266WiFi.h"
#else 
  #include "WiFi.h"
#endif
  #include "ArduinoOTA.h"
#include <MQTT.h>     //joel Gaehwiler
//#include "Wire.h"
#include <esp_task_wdt.h>

#define MIRROR_NR  1

#define WDT_TIMEOUT 10    // define a  seconds WDT (Watch Dog Timer)

IPAddress local_IP(192, 168, 10, 42);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

esp_err_t ESP32_ERROR; //wdt

int ledPin = 2; // 1 bei v2 ist nicht nutzbar.schade.  2 bei v1
int ledState = LOW;
unsigned long previousMillis = 0;  // will store last time LED was updated
const long interval = 1000;  // interval at which to blink (milliseconds)
unsigned long previousMillisConnectMsg = 0;  // will store last time LED was updated
const long intervalConnectMsg = 1000 * 60 * 10;  // interval at which to send a connected-message

//char Router[] = "rheinsberg.mesh";
//char Passwort[] = "1234567899";
const  char* ssid = "rheinsberg.mesh";
const  char* password = "1234567899";


char OTAHOSTNAME[] = "ESP32-mirror";
char OTAPASSWORD[] = "olfi";
const char* hostname = OTAHOSTNAME;   

const char* mqtt_path_connection;
const char* mqtt_path_m1;
const char* mqtt_path_m2;
const char* mqtt_user;
const char* mqtt_server = "192.168.10.51";
const int mport = 1883;

uint32_t i = 0;

// Motor A connections
int enA = 14;  // ENA pin
int in1 = 26;  // IN1 pin
int in2 = 27;  // IN2 pin

// Motor B connections
int enB = 32;  // ENB pin
int in3 = 25;  // IN3 pin
int in4 = 33;  // IN4 pin

// PWM properties
const int freq = 1000;        // PWM frequency
const int pwmChannelA = 0;    // PWM channel for motor A
const int pwmChannelB = 1;    // PWM channel for motor B
const int resolution = 8;     // 8-bit resolution (0-255)

volatile int motor1=0;
volatile int motor2=0;
volatile unsigned long receiveMillis = millis();
const long intervalReceive = 2000; // ms interval at which stop motors
int mm1=-2;  // Merker änderungserkennung
int mm2=-1;


WiFiClient client;
MQTTClient mclient;

long lastwifibegin = 0 - 30000;




void messageReceived(String &topic, String &payload) {
  Serial.println("incoming " + topic + ": " + payload);
  int i = payload.toInt();
  //min max grenzen
  if(i > 255){
    i = 255;
  }
  if(i < -255){
    i = -255;
  }
  if (topic == mqtt_path_m1){
    motor1 = i;
  }
  if (topic == mqtt_path_m2){
    motor2 = i;
  }
  receiveMillis = millis();
 }

/*

mirror
  1 offline
    1   0
    2   0
  2 30.11.25 11:41:56
    1   254
    2   -255

*/

void initMQTT(){
  //MIRROR_NR
  char bufferc[20];
  sprintf(bufferc, "mirror/%d%", MIRROR_NR);  
  mqtt_path_connection = strdup(bufferc);
  Serial.print("topic: ");
  Serial.println(mqtt_path_connection);
  sprintf(bufferc, "mirror/%d%/1", MIRROR_NR);  
  mqtt_path_m1 = strdup(bufferc);  
  sprintf(bufferc, "mirror/%d%/2", MIRROR_NR);  
  mqtt_path_m2 = strdup(bufferc);


  

  mclient.begin(mqtt_server, mport, client);  
  mclient.onMessage(messageReceived);
  mclient.setWill(mqtt_path_connection, "disconnected", true, 0);
  if (!mclient.connected()) {
    //connect();
  }  
  //mclient.subscribe(mqtt_path_m1);
  //mclient.subscribe(mqtt_path_m2);
  //mclient.publish(mqtt_path_m1, "0");
  //mclient.publish(mqtt_path_m2, "0");
}


void watchdog(){
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,                 // Convertin ms
    .idle_core_mask = 1, //(1 << portNUM_PROCESSORS) - 1,  // Bitmask of all cores, https://github.com/espressif/esp-idf/blob/v5.2.2/examples/system/task_watchdog/main/task_watchdog_example_main.c
    .trigger_panic = true                             // Enable panic to restart ESP32
  };
  ESP32_ERROR = esp_task_wdt_init(&wdt_config);  // enable panic so ESP32 restarts
  Serial.println("Last Reset : " + String(esp_err_to_name(ESP32_ERROR)));
  esp_task_wdt_add(NULL);                           // add current thread to WDT watch
  Serial.print("BootReason: ");
  Serial.println(esp_reset_reason());
  Serial.println("Watchdog is activ.");
}


void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
 
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  //local_IP
  //gateway
  //subnet
  //INADDR_NONE
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  //WiFi.config(local_IP, gateway, subnet, gateway); // required to set hostname properly
  WiFi.setHostname(OTAHOSTNAME);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) 
  {
    Serial.println("Keine Verbindung, neuer Versuch...");
    delay(2000);
    ESP.restart();
  }
  Serial.println("Verbindung erfolgreich hergestellt!");
  Serial.print("IP Adresse: ");
  Serial.println(WiFi.localIP());
  //publishConnectionTime();
  pinMode(ledPin, OUTPUT);


#if CONFIG_IDF_TARGET_ESP32
  //char message[64];
  //snprintf(message, 64, "%lu Packets.", i++);
  //Wire.slaveWrite((uint8_t *)message, strlen(message));
#endif

 // Set all the motor control pins to outputs
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);
  
  // Configure PWM for motor speed control using new API
  ledcAttachChannel(enA, freq, resolution, pwmChannelA);
  ledcAttachChannel(enB, freq, resolution, pwmChannelB);
  
  // Turn off motors - Initial state
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
  
  // Set initial PWM duty cycle to 0 (motors off)
  ledcWrite(enA, 0);
  ledcWrite(enB, 0);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
    mm1 = 0;
    mm2 = 0;
    Serial.println("------------------bremsen---------------------"); 
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    digitalWrite(in3, LOW);
    digitalWrite(in4, LOW);
    ledcWrite(enA, 255);    
    ledcWrite(enB, 255);   

    esp_task_wdt_reset();
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");    
    ESP.restart();
  });

  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total){
    esp_task_wdt_reset();
  });

  ArduinoOTA.begin();
  ArduinoOTA.setHostname(OTAHOSTNAME);
  ArduinoOTA.setPassword(OTAPASSWORD);


  initMQTT();
  watchdog();
}




void loop() {
  mclient.loop();
  if (!mclient.connected()) {
    connect();
  }  
  ArduinoOTA.handle();
  unsigned long currentMillis = millis();
  // connected-Message regelmässig
  if (currentMillis - previousMillisConnectMsg >= intervalConnectMsg) {    
    publishConnectionTime();
    previousMillisConnectMsg = currentMillis;
  }
  //blinken
  if (currentMillis - previousMillis >= interval) {    
    esp_task_wdt_reset();
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    // if the LED is off turn it on and vice-versa:
    ledState = not(ledState);
    // set the LED with the ledState of the variable:
    digitalWrite(ledPin,  ledState);                
    //Serial.print("motor1: ");    Serial.println(motor1);    Serial.print("motor2: ");    Serial.println(motor2);
  }
  //Notabschaltung, wenn keine Impulse kommen. 3 Sek. beide Motoren.
  if (currentMillis - receiveMillis >= intervalReceive) {        
    if((motor1 != 0)){
      motor1 = 0;
      Serial.println("No Signal, Motor1 stopped.");
    }      
    if((motor2 != 0)){
      motor2 = 0;
      Serial.println("No Signal, Motor2 stopped.");
    }          
  }
  motorControl();  
  delay(10);
}


void motorControl(){
// richtung und power je Motor werden per i2c gesetzt
// -255,-255    12,0
// minus heisst  rückwärts, abs ist power in der direkten Auflösung
  if((mm1==motor1) & (mm2==motor2)){
    //Serial.print("identisch"); 
    return;
  }
  // Änderung erkannt.
  mm1 = motor1;
  mm2 = motor2;

if(mm1==0 && mm2==0){ // Wirkung testen
    Serial.println("------------------bremsen---------------------"); 
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    digitalWrite(in3, LOW);
    digitalWrite(in4, LOW);

    ledcWrite(enA, 255);    
    ledcWrite(enB, 255);    
} else {
    boolean richtung = motor1 >=0;
    if(richtung){
      digitalWrite(in1, HIGH);
      digitalWrite(in2, LOW);
      ledcWrite(enA, motor1);
    }else{
      digitalWrite(in1, LOW);
      digitalWrite(in2, HIGH);
      ledcWrite(enA, motor1 * -1);
    }
    Serial.print("set motor1: "); 
    Serial.println(motor1); 

    richtung = motor2 >=0;
    if(richtung){
      digitalWrite(in3, HIGH);
      digitalWrite(in4, LOW);
      ledcWrite(enB, motor2);
    }else{
      digitalWrite(in3, LOW);
      digitalWrite(in4, HIGH);
      ledcWrite(enB, motor2 * -1);
    }
    Serial.print("set motor2: "); 
    Serial.println(motor2); 
  }

}

// This function lets you control spinning direction of motors
void directionControl() {
  // Set motors to maximum speed
  ledcWrite(enA, 255);
  ledcWrite(enB, 255);
  
  // Turn on motor A & B (forward direction)
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
  delay(2000);
  
  // Now change motor directions (reverse direction)
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
  delay(2000);
  
  // Turn off motors
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
}

// This function lets you control speed of the motors
void speedControl() {
  // Turn on motors (reverse direction)
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
  
  // Accelerate from zero to maximum speed
  for (int i = 0; i < 256; i++) {
    ledcWrite(enA, i);
    ledcWrite(enB, i);
    delay(20);
  }
  
  // Decelerate from maximum speed to zero
  for (int i = 255; i >= 0; --i) {
    ledcWrite(enA, i);
    ledcWrite(enB, i);
    delay(20);
  }
  
  // Now turn off motors
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
}





void connect() {
  Serial.print("checking WiFi\n");  
  while (WiFi.status() != WL_CONNECTED) {    
    if (millis() - lastwifibegin > 15000) {  // alle 15 sec
      WiFi.persistent(false); 
      WiFi.disconnect();
      delay(100);
      WiFi.mode(WIFI_OFF);
      delay(100);
      WiFi.mode(WIFI_STA);
      lastwifibegin = millis();
      Serial.print("\nretry WiFi begin...");
      Serial.println(millis()-lastwifibegin);
 //local_IP
  //gateway
  //subnet
  //INADDR_NONE

      //WiFi.config(local_IP, gateway, subnet, gateway);
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
      WiFi.setHostname(hostname);
      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {  
        Serial.print(".");
        delay(1000); //warten auf Verbindung  
      }
      delay(1000);
      Serial.println("\n-------------------------");      
      Serial.print("hostname: ");
      Serial.println(hostname);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("ESP Mac Address: ");
      Serial.println(WiFi.macAddress());
      Serial.print("Subnet Mask: ");
      Serial.println(WiFi.subnetMask());
      Serial.print("Gateway IP: ");
      Serial.println(WiFi.gatewayIP());
      Serial.print("DNS: ");
      Serial.println(WiFi.dnsIP());  
      Serial.println("-------------------------");      
      Serial.print("\nWiFi connected!\n");      
      esp_task_wdt_reset();
      for (int i=0; i<7; i++){
        digitalWrite(ledPin, 1); // Status   
        delay(100);
        digitalWrite(ledPin, 0); // Status   
        delay(100);
      }
      break;      
    }
  }
  
  //printMacAddress();
  if (WiFi.status() == WL_CONNECTED){ 
    bool erfolg = false;
    for (int i=0; i<30; i++){
      Serial.println("connecting to MQTT-Server...");
      digitalWrite(ledPin, 1); // Status   
      if(mclient.connect(mqtt_user, "", "")){
        Serial.println("MQTT connected.\n");
        //mclient.publish(mqtt_path_connection, "reconnect");
        mclient.subscribe(mqtt_path_m1);
        mclient.subscribe(mqtt_path_m2);
        mclient.publish(mqtt_path_m1, "0");
        mclient.publish(mqtt_path_m2, "0");
        publishConnectionTime();
        erfolg = true;
        break;     
      }
      Serial.print("not connecting to MQTT-Server");
      digitalWrite(ledPin, 0); // Status   
      delay(1000);        
    }
    if (!erfolg){
      ESP.restart();
    }
    Serial.println();
  }
}


const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

void publishConnectionTime(){
  #define MY_TZ  "CET-1CEST,M3.5.0,M10.5.0/3"
  configTime(0, 0, ntpServer);
  setenv("TZ", MY_TZ, 1);            // Set environment variable with your time zone
  tzset();
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
   mclient.publish(mqtt_path_connection, "Error NTP",true,2);
    return;
  }
  char IP[] = "000.000.000.000";
  WiFi.localIP().toString().toCharArray(IP, 16);
  char buffer[50];
  char tag[][3] = {"So","Mo", "Di", "Mi", "Do", "Fr","Sa"};
  //sprintf(buffer, "connected %s, %02d.%02d.%02d %02d:%02d:%02d, IP:%s",tag[timeinfo.tm_wday],timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year+1900-2000,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,IP);    
  sprintf(buffer, "connected %s, %02d:%02d, IP:%s",tag[timeinfo.tm_wday],timeinfo.tm_hour,timeinfo.tm_min,IP);
  //Serial.println(buffer);
  mclient.publish(mqtt_path_connection, buffer,true,2);
}

