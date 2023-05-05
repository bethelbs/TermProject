#include <Wire.h>
#include "MAX30100.h"
#include <MAX30100_PulseOximeter.h>
#include <M5Core2.h>
#include <WiFi.h> //Wifi library
#include "esp_wpa2.h" //wpa2 library for connections to Enterprise networks
#include <EMailSender.h>
//#include <M5Stack.h>
byte mac[6];

#define EAP_ANONYMOUS_IDENTITY "NET ID" //Enter your NYU Net ID
#define EAP_IDENTITY "NET ID" //Enter your NYU Net ID
#define EAP_PASSWORD "NET ID PASSWORD" //enter the password for NYU Net ID account here in the quotations


#define IR_LED_CURRENT  MAX30100_LED_CURR_7_6MA
//#define RED_LED_CURRENT MAX30100_LED_CURR_7_6MA
#define REPORTING_PERIOD_MS     1000
#define M5_LED 10

// SSID NAME
const char* ssid = "nyu"; // Wifi SSID -- Should be set to "nyu"

// Create a PulseOximeter object
PulseOximeter pox;
 

const int ecgPin = 0;
int upperThreshold = 100;
int lowerThreshold = 0;
int ecgOffset = 400;
float beatsPerMinute = 0.0;
bool alreadyPeaked = false;
unsigned long firstPeakTime = 0;
unsigned long secondPeakTime = 0;
unsigned long rrInterval = 0;
int numRRDetected = 0;
bool hrvStarted = false;
bool hrvUpdate = false;
bool hrvComplete = false;
unsigned long hrvStartTime = 0;
unsigned long rrIntervalPrevious = 0;
float rrDiff = 0.0;
float rrDiffSquaredTotal = 0.0;
float diffCount = 0.0;
float rmssd = -1.0;
// Time at which the last beat occurred
uint32_t tsLastReport = 0;
bool bttn_pressed;
// Callback routine is executed when a pulse is detected
void onBeatDetected() {
    Serial.println("Beat!");
}

void setup() {
M5.begin();
    Serial.begin(115200);
delay(10);
M5.Lcd.print(F("Connecting to network: "));
 M5.Lcd.println(ssid);
WiFi.disconnect(true);  //disconnect from WiFi to set new WiFi connection
WiFi.mode(WIFI_STA); //init wifi mode
WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_ANONYMOUS_IDENTITY, EAP_IDENTITY, EAP_PASSWORD); //Without Certificate
 
while (WiFi.status() != WL_CONNECTED) {
delay(500);
M5.Lcd.print(F("."));
 M5.Lcd.println("");
M5.Lcd.println(F("WiFi is connected!"));
M5.Lcd.println(F("IP address set: "));
M5.Lcd.println(WiFi.localIP()); //print LAN IP }

    Serial.print("Initializing pulse oximeter..");

    // Initialize sensor
    if (!pox.begin()) {
        Serial.println("FAILED");
        for(;;);
    } 
    else {
        Serial.println("SUCCESS");
    }

	// Configure sensor to use 7.6mA for LED drive
	pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);

    // Register a callback routine
    pox.setOnBeatDetectedCallback(onBeatDetected);
}}

void display(){

//display stress detected
//set cursor, size and colour for stress detection message
M5.lcd.setCursor(10,90);
M5.lcd.setTextColor(RED);
M5.lcd.setTextSize(4);
M5.Lcd.println("Stress detected");
delay(2000); //hold the message for 2 seconds
}
 void loop() {

     // Read from the sensor
    pox.update();

    // Grab the updated heart rate and SpO2 levels
   if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    Serial.print("Heart rate:");
        M5.lcd.print("HEART RATE:");
        Serial.print(pox.getHeartRate());
        M5.lcd.print(pox.getHeartRate());
        Serial.print("bpm / SpO2:");
        M5.lcd.print("bpm.        /SpO2:");
        Serial.print(pox.getSpO2());
        M5.lcd.print(pox.getSpO2());
        Serial.println("%");
        M5.lcd.print("%");
        delay(1000);


        tsLastReport = millis();
    }
    M5.lcd.clear();
 
  int ecgReading = pox.getHeartRate(); 
// Measure the ECG reading minus an offset to bring it into the same
// range as the heart rate (i.e. around 60 to 100 bpm
    if (ecgReading > upperThreshold && alreadyPeaked == false) { 
  // Check if the ECG reading is above the upper threshold and that
  // we aren't already in an existing peak
      if (firstPeakTime == 0) {
        // If this is the very first peak, set the first peak time
        firstPeakTime = millis();
        digitalWrite(M5_LED, HIGH);
      }
      else {
        // Otherwise set the second peak time and calculate the 
        // R-to-R interval. Once calculated we shift the second
        // peak to become our first peak and start the process
        // again
        secondPeakTime = millis();
        // Store the previous R-to-R interval for use in HRV calculations
        rrIntervalPrevious = rrInterval;
        // Calculate new R-to-R interval and set HRV update flag
        rrInterval = secondPeakTime - firstPeakTime;
        firstPeakTime = secondPeakTime;
        hrvUpdate = true;
        numRRDetected = numRRDetected + 1;
        digitalWrite(M5_LED, HIGH);
      }
      alreadyPeaked = true;
  }

  if (ecgReading < lowerThreshold) {
  // Check if the ECG reading has fallen below the lower threshold
  // and if we are ready to detect another peak
    alreadyPeaked = false;
    digitalWrite(M5_LED, LOW);
  } 

  // Once two consecutive R-to-R intervals have been detected, 
  // start the 5 minute HRV window
  if (!hrvStarted && numRRDetected >= 2) {
    hrvStarted = true;
    hrvStartTime = millis();
  }
  
  // If a new R-to-R interval has been detected, update the HRV measure
  if (hrvUpdate && hrvStarted) {
    // Add the square of successive differences between 
    // R-to-R intervals to the running total
    rrDiff = float(rrInterval) - float(rrIntervalPrevious);
    rrDiffSquaredTotal = rrDiffSquaredTotal + sq(rrDiff);
    // Count the number of successive differences for averaging
    diffCount = diffCount + 1.0;
    // Reset the hrvUpdate flag
    hrvUpdate = false;
  }

  // Once five minute window has elapsed, calculate the RMSSD
  if (millis() - hrvStartTime >= 3000 && !hrvComplete) {
    rmssd = sqrt(rrDiffSquaredTotal/diffCount);
    hrvComplete = true;
  } 

  // Print the final values to be read by the serial plotter
  // RMSSD will print -1 until the 3-second window has elapsed
  // Important notes on using this code:
  // 1. RMSSD can be significantly affected by a bad sensor
  // readings in short-term recordings so ensure to check for 
  // incorrectly detected RR intervals if you are getting 
  // very large RMSSD values.
  // 2. Due to the function of the internal Arduino clock, the 
  // real-world elapsed time for HRV computation may exceed 3 seconds. 
  // This should not make a practical difference for RMSSD calculation, 
  // but it is important to note if critical precision is required.
  



Serial.print("The calculated HRV is: ");   
Serial.println(rmssd);  


//condition for stress detection :

if (rmssd < 50){
M5.Lcd.println("Long press any button if you are aware of the situation producing low HRV if you do not want an email to be sent.");


while (bttn_pressed ==true) 

{ 
  //stops loop while button is pressed

//set cursor, size, and color for long press message 
M5.lcd.setCursor(10,160);
M5.lcd.setTextColor(RED);
M5.Lcd.setTextSize(2);
delay(3000);
M5.Lcd.println("Email Terminated.");
M5.Lcd.println("Please consider the following activities:");
M5.Lcd.println("POUR YOURSELF A GLASS OF WATER");
M5.Lcd.println("TAKE A WALK AROUND THE BLOCK");
M5.Lcd.println("LISTEN TO YOUR FAVOURITE SONG");
M5.Lcd.println("MEDITATE FOR A MINUTE");
M5.Lcd.println("TAKE DEEP BREATHES");
M5.Lcd.println("TAKE A SHORT NAP");


M5.Lcd.fillScreen( BLACK);
break;

}

//update the status of the button
bttn_pressed = true;



while (bttn_pressed ==false) {//loops while button is not pressed


if(rmssd <50){
display();
delay(1000);
}

if(rmssd<50)
{
vibrate();
delay(500);
}
//send an email when rmssd is less than 45 milliseconds 


if(rmssd<50)
{
 // Create a new EMailMessage object
   EMailSender::EMailMessage message;
   message.subject = "EMERGENCY";
   message.message = "STRESS DETECTED <br> PLEASE TAKE ACTION";

   // Create a new EMailSender object
   EMailSender emailSend("jkumarichaudhary@gmail.com", "wkxsddavxbqbmdoz", "jkumarichaudhary@gmail.com", "smtp.gmail.com", 465);

   // Send the email
   EMailSender::Response resp = emailSend.send("bvshlr@gmail.com", message);
   Serial.println("Sending status: ");
   Serial.println(resp.code);
   Serial.println(resp.desc);
   Serial.println(resp.status);

   M5.Lcd.fillScreen(BLACK);
   M5.Lcd.setCursor(10, 90);
   M5.Lcd.setTextColor(RED);
   M5.Lcd.setTextSize(3);
   M5.Lcd.println("Email Sent");
   delay(5000); //delay 5s
}


break; //end loop after email is sent
}

}}
void vibrate()   
{ 
M5.Axp.SetLDOEnable(3, true);      //start vibration
delay(500);                                       //delay 0.5s
M5.Axp.SetLDOEnable(3, false);    //end vibration
}
