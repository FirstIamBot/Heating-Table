// Load Wi-Fi library
#include <WiFi.h>
#include <WebServer.h>        // include ESP32 library
#include "webpage/index.h"
#include "webpage/dbg.h"
#include "main.h"

// Replace with your network credentials
const char* ssid     = "HeatTAbleESP32";
const char* password = "123456789";

// Set web server port number to 80
//WiFiServer server(80);
WebServer server(80);
// Variable to store the HTTP request 
String header;
String str, json;

extern double Measured_Temp, Curr_Temp, valComputePID;

extern int8_t Mode;
extern int8_t State;
//********   route function   *************************************
void base(void);
void dbg(void);
void status(void);
void temp(void);
void pid(void);
//*********************************************
void initWIFI(void){
        // Connect to Wi-Fi network with SSID and password
    Serial.print("Setting AP (Access Point)…");
    // Remove the password parameter, if you want the AP (Access Point) to be open
    //while(!WiFi.softAP(ssid, password)){
    while(!WiFi.softAP(ssid, password)){
        Serial.print("WiFi allocation failed");
        for(;;);
    }
    server.on("/", base);        // load default webpage
    server.on("/tempurl", temp); // map URLs to functions:
    server.on("/gdburl", dbg);   // map URLs to functions:
    server.on("/statusurl", status);   // map URLs to functions:
    server.on("/pidurl", pid);   // map URLs to functions:    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    server.begin();
}


String JsonConvert(int val1, String val2)
{
    json = "{\"var1\": \"" + String(val1) + "\",";
    json += " \"var2\": \"" + val2 + "\"}";
    return json;
}

// function to load default webpage
void base(void){ 
    // and send HTML code to client
    server.send(200, "text.html", page_index);
}

void dbg(void){

    json = "{\"varComputePID\": \"" + String(valComputePID) + "\",";
    server.send(200, "text.html", page_dbg);
}

void temp(void){

    json = "{\"varMesT\": \"" + String(Measured_Temp)  + "\",";
    json += "\"varCurrT\": \"" + String(Curr_Temp)  + "\"}";
    server.send(200, "text/json", json);
}

void status(void){

    if(Mode == 0){
       str = "STANDBAY";
    }
    else if(Mode & MANUAL_HEATING){
        str = "MANUAL HEATING";
    }
    else if(Mode & PROG_HEATING){
        str = "PROG HEATING";
    }
    else if(Mode & SETTING){
        str = "SETTING";
    }
    json = "{\"varMode\": \"" + str  + "\",";
    if(State & HEATING){
        str = "HEATING";
    }
    else if(!State & HEATING){
        str = "";
    }
    json += "\"varStatus\": \"" + str + "\"}";

    server.send (200, "text/json", json);// send JSON text to client
}

void pid(void){
    json = "{\"varComputePID\": \"" + String(valComputePID) + "\"}";
    server.send (200, "text/json", json);// send JSON text to client
}