#include "Arduino.h"
#include "Arduino.h"
#include <ESPAsyncWebSrv.h> 
#include <ESPAsyncTCP.h> 
#include <SPI.h> 
#include <FS.h> 
#include <LiquidCrystal_I2C.h>



#define TRIG_PIN D3  
#define ECHO_PIN D8  
#define BUZZER_PIN D0
#define PUMP1 D5
#define PUMP2 D6
#define PUMP3 D7
#define GLASS_DISTANCE_THRESHOLD 10
#define SETUP_TIME 3000


// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

// set LCD address, number of columns and rows
// if you don't know your display address, run an I2C scanner sketch
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows); 


long P1START = -1;
long P1STOP = -1;
long P2START = -1;
long P2STOP = -1;
long P3START = -1;
long P3STOP = -1;
long ORDERFINISH = -1;
long ORDERDURATION = -1;

boolean startOrder = false;
boolean newOrder = false;
long lastDistCheck = 0;
long lastDrawProgress = 0;

byte zero[] = {B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000
};
byte one[] = {
  B10000,
  B10000,
  B10000,
  B10000,
  B10000,
  B10000,
  B10000,
  B10000
};
byte two[] = {
  B11000,
  B11000,
  B11000,
  B11000,
  B11000,
  B11000,
  B11000,
  B11000
};
byte three[] = {
  B11100,
  B11100,
  B11100,
  B11100,
  B11100,
  B11100,
  B11100,
  B11100
};
byte four[] = {
  B11110,
  B11110,
  B11110,
  B11110,
  B11110,
  B11110,
  B11110,
  B11110
};
byte five[] = {
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111
};


// Create AsyncWebServer instance on port "80"
AsyncWebServer server(80);
// Create WebSocket instance on URL "/ws"
AsyncWebSocket ws("/ws");

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  lcd.createChar(0, zero);
  lcd.createChar(1, one);
  lcd.createChar(2, two);
  lcd.createChar(3, three);
  lcd.createChar(4, four);
  lcd.createChar(5, five);
  
  pinMode(TRIG_PIN, OUTPUT);  
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);    

  pinMode(PUMP1, OUTPUT);  
  digitalWrite(PUMP1, HIGH);  
  pinMode(PUMP2, OUTPUT);    
  digitalWrite(PUMP2, HIGH);
  pinMode(PUMP3, OUTPUT);   
  digitalWrite(PUMP3, HIGH);
  
  SPIFFS.begin();
  startAP();
  startServer();

  // initialize LCD
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();
  
  resetScreen();
  delay(1000);  
  beep();
  
}

void resetScreen(){
  Serial.println("reset screen");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" - CookTail - ");
  lcd.setCursor(0, 1);
  lcd.print(" -  HELLO   - ");  
}

void loop() {
  if (newOrder){
    Serial.println("new order");
    if (checkGlass()==false){
      //cancel order
      startOrder = false;           
      P1START = -1;
      P1STOP = -1;
      P2START = -1;
      P2STOP = -1;
      P3START = -1;
      P3STOP = -1;
      newOrder = false;
      Serial.println("no glass !!!");
      return;
    }
    startOrder = true;
    newOrder = false;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Preparing...");   
    Serial.println("Preparing..."); 
  }
  
  if(startOrder){
    startMotors();
    stopMotors();  
    if(millis()>=ORDERFINISH){
      // order is completed
      startOrder = false;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Cheers :)");
      Serial.println("Cheers :)"); 
      delay(2000);
      resetScreen();
    }
    
  }  

  if(millis()-lastDistCheck > 5000){
    int dist = calculateGlassDistance();
    Serial.print("glass distance:"); Serial.println(dist);
    lastDistCheck = millis(); 
  }

  if(startOrder && (millis()-lastDrawProgress > 500)){
    drawProgress();
    lastDrawProgress = millis();
  }
  delay(5);

}



void startAP() {
  WiFi.mode(WIFI_AP);
  Serial.print(F("[ INFO ] Configuring access point... "));
  bool success = WiFi.softAP("CookTail", NULL);
  Serial.println(success ? "Ready" : "Failed!");
  // Access Point IP
  IPAddress myIP = WiFi.softAPIP();
  Serial.print(F("[ INFO ] AP IP address: "));
  Serial.println(myIP);
  Serial.println("[ INFO ] AP SSID: CookTail");

}

void startServer() {
  // Start WebSocket Plug-in and handle incoming message on "onWsEvent" function
  server.addHandler(&ws);
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve all files in root folder
  server.serveStatic("/", SPIFFS, "/");

  // Route for root / web page  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html");
  });
  
  // Handle what happens when requested web file couldn't be found
  server.onNotFound([](AsyncWebServerRequest * request) {
    Serial.println("onNotFound:");
    AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "Not found");
    request->send(response);
  });

  server.on("/order", HTTP_GET, [] (AsyncWebServerRequest *request) {
    Serial.println("on(/order:");
    String value;
    // GET command value on <ESP_IP>/order?P1S=xx&P1D=xx&P2S=xx&P2D=xx&P3S=xx&P3D=xx
    if (request->hasParam("P1S")) {
      long currentTime = millis();
      value = request->getParam("P1S")->value();
      Serial.println("P1S:" + value);
      P1START = currentTime + value.toInt()*1000 + SETUP_TIME;
      value = request->getParam("P1D")->value();
      P1STOP = P1START + value.toInt()*1000;
      ORDERFINISH = P1STOP;

      value = request->getParam("P2S")->value();
      Serial.println("P2S:" + value);
      P2START = currentTime + value.toInt()*1000 + SETUP_TIME;
      value = request->getParam("P2D")->value();
      P2STOP = P2START + value.toInt()*1000;
      if (P2STOP>ORDERFINISH)
        ORDERFINISH = P2STOP;

      value = request->getParam("P3S")->value();
      Serial.println("P1S:" + value);
      P3START = currentTime + value.toInt()*1000 + SETUP_TIME;
      value = request->getParam("P3D")->value();
      P3STOP = P3START + value.toInt()*1000;
      if (P3STOP>ORDERFINISH)
        ORDERFINISH = P3STOP;

      ORDERDURATION = ORDERFINISH - currentTime;
      Serial.print("ORDERDURATION:");Serial.println(ORDERDURATION);

      startOrder = false;
      newOrder = true;
    }
    request->send(200, "text/plain", "OK");
  });

  // Start Web Server
  server.begin();
}



boolean checkGlass(){
  Serial.println("check glass");
  int dist = calculateGlassDistance();
  Serial.println(dist);
  if (dist<=GLASS_DISTANCE_THRESHOLD){
    Serial.println("check glass ok");
    return true;    
  }
  Serial.println("no glass");
  // LCD ERROR: Please put glass
  lcd.clear();    
  lcd.setCursor(0,0);
  lcd.print("NO GLASS !");  
  lcd.setCursor(0,1);
  lcd.print("Pls order again!");
  beep();
  delay(100);
  beep();
  delay(3000);
  resetScreen(); 
  
  return false;
   
}


int calculateGlassDistance() {    
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3000);    
        
  // Clears the trigPin
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);    
    
  // Sets the trigPin on HIGH state for 10 micro seconds. So, sends a 10ms ultrasound wave
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);   

  // Reads the echoPin, returns the sound wave travel time in microseconds
  int duration = pulseIn(ECHO_PIN, HIGH);    
  
  // Calculating the distance in cm: 
  // (34000cm per second / 1000000) * time / 2
  int distance= duration*0.034/2;
  
  return distance;
    
}

void startMotors(){
  long curtime = millis();
  if( P1START > 0 && curtime >= P1START){
    digitalWrite(PUMP1, LOW);
    P1START=-1;
  }
  if( P2START > 0 && curtime >= P2START){
    digitalWrite(PUMP2, LOW);
    P2START=-1;
  }
  if( P3START > 0 && curtime >= P3START){
    digitalWrite(PUMP3, LOW);
    P3START=-1;
  }  
}

void stopMotors(){
  long curtime = millis();
  if( P1STOP > 0 && curtime >= P1STOP){
    digitalWrite(PUMP1, HIGH);
    P1STOP = -1;
  }
  if( P2STOP > 0 && curtime >= P2STOP){
    digitalWrite(PUMP2, HIGH);
    P2STOP = -1;
  }
  if( P3STOP > 0 && curtime >= P3STOP){
    digitalWrite(PUMP3, HIGH);
    P3STOP = -1;
  }  
}


void beep(){
  Serial.println("Beep..");
  
  digitalWrite(BUZZER_PIN,HIGH); // for active buzzers just supplying voltage to the buzzer is enough. frequency is fixed. 
  long starttime = millis();
  while(millis()-starttime<200)  {delay(1);yield();} // wait for 200 ms
  digitalWrite(BUZZER_PIN,LOW) ;// for active buzzer: cut the energy of the buzzer.   
}

// Handles WebSocket Events
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  Serial.println("onWsEvent");
  if (type == WS_EVT_ERROR) {
    Serial.printf("[ WARN ] WebSocket[%s][%u] error(%u): %s\r\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  }
  else if (type == WS_EVT_DATA) {
    Serial.println("WS_EVT_DATA");

    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";

    if (info->final && info->index == 0 && info->len == len) {
      //the whole message is in a single frame and we got all of it's data
      for (size_t i = 0; i < info->len; i++) {
        msg += (char) data[i];
      }

      Serial.println("msg:"+msg);
      
    }
  }
}


void drawProgress(){
    long left = ORDERFINISH - millis();
    int secondsLeft = (int)(left / 1000);    
    String str = "";
    if(secondsLeft<100)
      str = " ";
    if(secondsLeft<10)
      str = "  ";  
    str = str + String(secondsLeft);
    lcd.setCursor(13,1);
    lcd.print(str);
    Serial.print("left:");Serial.println(left);

    int percent = 100 - (left*100)/ORDERDURATION;
    percent = percent / 2; 
    Serial.print("percent:");Serial.println(percent);

    int mycursor = 0;    

    int number = percent/5;
    int remainder = percent%5;
    Serial.print("remainder:");Serial.println(remainder);
    if(number > 0)
    {
      for(int j = 0; j < number; j++)
      {
        lcd.setCursor(j,1);
        lcd.write(5);
      }
    }
    lcd.setCursor(number,1);
    lcd.write(remainder); 

}
