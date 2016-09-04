#include <SPI.h>
#include <Servo.h>
#include <Ethernet.h>
#include <Hx711.h>
#include <avr/sleep.h>
#include <avr/wdt.h> 
#include <SoftwareSerial.h>

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))    //Setting bit
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))      //Clearing bit
#endif

#define RECONNECTTIMES 3

/////////////////////////////////////////
//          Setting Global Variable        //
/////////////////////////////////////////
Servo switchServo;
Hx711 scale(A1,A0);

//Servo and food control
float measureWeight=0;
float destWeight;
boolean sem = true;
volatile int period = 15;
volatile int minCount;
static byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
//IPAddress ip(192, 168, 14, 143);
//IPAddress subnet(255, 255, 255, 0);
//IPAddress gateway(192, 168, 14, 145);
IPAddress server(74, 125, 31, 141);
EthernetClient client;


//Bluetooth
String rx="";
String str;
int bluetoothTx = 8;
int bluetoothRx = 9;
SoftwareSerial bluetooth(bluetoothTx, bluetoothRx);
boolean bluetooth_initFlag = true;
#define feedtimes 10
int feedtime[feedtimes];
int time = 0;
int feedtime_idx = 0;
int minCountIndex = 0;

/////////////////////////////////////////
//                        Setup                       //
/////////////////////////////////////////
void setup() {
  Serial.begin(9600);

  //Bluetooth
  bluetooth.begin(9600);
  Serial.println("Please set parameters of your device.");
  Serial.println("(1.Weight of food to feed(g), 2.Current time, 3.Feeding time)");
  while(bluetooth_initFlag){
    if (bluetooth.available()) 
    { 
      char income = bluetooth.read();
      rx = rx + income;
      getData(income);
    }
  }
  //Set the interval of the first feeding time  
  while((feedtime[minCountIndex] - time) < 0){
    minCountIndex++;
  }
  sem = false;
  minCount = feedtime[minCountIndex] - time;
  sem = true;
  if(minCountIndex == feedtime_idx) {
    minCountIndex = 0;
  } else {
    minCountIndex++;
  }
  Serial.print("Minutes to wait = ");
  Serial.println(minCount);
  Serial.println("Parameters are setted.");

  //Initial servo motor
  switchServo.attach(3);  //Connected to servo via pin 3(with PWM)
  switchServo.write(85);  //Initial angle degree = 90
  delay(100);

  //Initial Ethernet
    Ethernet.begin(mac);
//  Ethernet.begin(mac, ip, gateway, subnet);
//  while() {
//    Serial.println("Ethernet initialization failed.");
//    Serial.println("Reconnecting.");
//  }
  Serial.println("Ethernet initialization successed.");
  delay(50);

  //Initial sleep mode and watch dog timer
  cbi( SMCR,SE );      // sleep enable, power down mode
  cbi( SMCR,SM0 );     // power down mode
  sbi( SMCR,SM1 );     // power down mode
  cbi( SMCR,SM2 );     // power down mode
  setup_watchdog(8);
  // 0=16ms, 1=32ms,2=64ms,3=128ms,4=250ms,5=500ms
  // 6=1 sec,7=2 sec, 8=4 sec, 9= 8sec
  
  system_sleep();
}


/////////////////////////////////////////
//                        Loop                        //
/////////////////////////////////////////
void loop() {
  if (minCount == 0) {// wait for timed out watchdog 
    int countLoopTimePre = millis();
    int countLoopTimeAft;
    int loopDuration;
    
    //Servo control
    float currentWeight;
    measureWeight = scale.getGram();
    Serial.print(measureWeight, 1);
    Serial.println(" g");
    if(measureWeight < destWeight) {                                    //Check if the food is enough
      Serial.println("The food is not enough.");                        //The food is "NOT" enough
      while(scale.getGram() < destWeight) {                  //Supply food until it's enough
        switchServo.write(85);
        switchServo.write(115);
        delay(100);
        switchServo.write(85);
        delay(200);        
        currentWeight = scale.getGram();
        Serial.print("Weight = ");
        Serial.print(currentWeight, 2);
        Serial.println(" g");
        if(currentWeight > destWeight) {                         //If the fod is enough, do not open the gate
          Serial.println("Food is enough.");
          break;
        }
      }
      delay(50);

    } 
    else {                                                                                 //The food is enough
      Serial.println("The food is enough.");
      delay(50);
    }

    //upload to cloud
    int countFailure = RECONNECTTIMES;
    while(countFailure != 0) {
      if (client.connect(server, 80)) {
        Serial.println("Connected");
        break;
      } 
      else {
        Serial.println("Connected failed.");
        countFailure ++;
        continue;
      }
    }
    client.print("GET /add?restfood=");
    client.print(measureWeight);
    client.print("&supplement=");
    client.print(currentWeight - measureWeight);
    client.println(" HTTP/1.1");
    client.println("Host: 1.ccusmartfeeder.appspot.com");
    client.println();

    Serial.println("Uploaded");


    client.stop();

    
    sem = false;
    countLoopTimeAft = millis();
    loopDuration = countLoopTimeAft - countLoopTimePre;
    loopDuration /= 1000;
    loopDuration /= 60;
    Serial.print("Loop time = ");
    Serial.println(loopDuration);
    if(minCountIndex == feedtime_idx) {
      minCount = feedtime[0] - feedtime[feedtime_idx - 1] + 1440; //1440 means 1440 minutes, i.e. 24 hours
//      Serial.print(feedtime[0]);
//      Serial.print(" - ");
//      Serial.print(feedtime[feedtime_idx - 1]);
//      Serial.print(" + 1440 = ");
//      Serial.println(minCount);
      delay(50);
      minCountIndex = 0;
    } else {
      minCount = feedtime[minCountIndex] - feedtime[minCountIndex - 1];
//      Serial.print(feedtime[minCountIndex]);
//      Serial.print(" - ");
//      Serial.print(feedtime[minCountIndex - 1]);
//      Serial.print(" = ");
//      Serial.println(minCount);
      delay(50);
      minCountIndex++;
    }
    sem = true;
    if (loopDuration >= minCount) {
      minCount = 0;
    }
    Serial.print("Minutes to wait = ");
    Serial.println(minCount);
    delay(50);
  }
  system_sleep();  // when we wake up, we’ll return to the top of the loop
}


/////////////////////////////////////////
//             Setup Watchdog                //
/////////////////////////////////////////
void setup_watchdog(int ii) {
  byte bb;
  int ww;
  if (ii > 9 ) ii=9;
  bb=ii & 7;
  if (ii > 7) bb|= (1<<5);
  bb|= (1<<WDCE);
  ww=bb;
  MCUSR &= ~(1<<WDRF); //Set watchdog reset flag = 0
  // start timed sequence
  WDTCSR |= (1<<WDCE) | (1<<WDE);  //Set watchdog change enable = 1. Set watchdog system reset enable = 1
  // set new watchdog timeout value
  WDTCSR = bb;  //Set watchdog prescaler
  WDTCSR |= _BV(WDIE);  //Set watchdog interrupt enable = 1
}

/////////////////////////////////////////
//           Setting System Sleep           //
/////////////////////////////////////////
void system_sleep() {
  cbi(ADCSRA,ADEN);                                                // switch Analog to Digitalconverter OFF
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);    // sleep mode is set here
  sleep_enable();
  sleep_mode();                                                         // System sleeps here
  sleep_disable();                                                      // System continues execution here when watchdog timed out 
  sbi(ADCSRA,ADEN);                                                // switch Analog to Digitalconverter ON
}


/////////////////////////////////////////
// Get Current Time(call by getData)//
/////////////////////////////////////////
int getTime(String s){
  int h;
  String cut = s.substring(0,2);
  char c[str.length()+1];
  cut.toCharArray(c, sizeof(c));
  h = atoi(c) * 60;
  cut = s.substring(3,5);
  cut.toCharArray(c, sizeof(c));
  int c_time = h + atoi(c);
  return c_time;
}


/////////////////////////////////////////
//               Get Parameters               //
/////////////////////////////////////////
void getData(char in){
  switch(in){
  case 'm':

    str = rx.substring(0,rx.indexOf(in));
    char c[str.length()+1];
    str.toCharArray(c, sizeof(c));
    destWeight = atof(c);
    Serial.print("Weight of food to feed = ");
    Serial.print(destWeight);
    Serial.println(" g");
    rx="";
    break;

  case 'c':
    str = rx.substring(0,rx.indexOf(in));
    time = getTime(str);
    //Serial.println(time);
    rx="";
    break;
  case 'f':
    str = rx.substring(0,rx.indexOf(in));
    feedtime[feedtime_idx++] = getTime(str);
//    Serial.println(feedtime[feedtime_idx-1]);
    rx="";
    break; 
  case 'z':
  int tmp;
  int i;
    for( i = 0; i < feedtime_idx; i++) {
      for(int j = feedtime_idx - 1; j > i; j--) {
        if(feedtime[j] < feedtime[j-1]) {
          tmp = feedtime[j-1];
          feedtime[j-1] = feedtime[j];
          feedtime[j] = tmp;
        }
      }
    }
    for(i = 0; i < feedtime_idx; i++) {
//      Serial.print(feedtime[i]);
//      Serial.print(" ");
    }
//    Serial.println();
    bluetooth_initFlag = false;
    rx="";
    break; 
  default:
    break;
  }
}
/////////////////////////////////////////
//                Watchdog ISR                 //
/////////////////////////////////////////
ISR(WDT_vect) {
  period--;                // set global flag
  if(period == 0) {
    if(sem){
      minCount--;
    }    
    period = 15;
  }
}

