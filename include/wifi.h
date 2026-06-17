// Load Wi-Fi library

#include "main.h"

#define STA 
//#define AP 
#define HTTP_PORT 80

#ifdef STA    
const char* ssid     = "my_point";
const char* password = "mobku166740";
#endif
#ifdef AP  
const char* ssid     = "HeatTable";
const char* password = "123456789";
#endif
// Replace with your network credentials for softAP mode

// Set web server port number to 80
AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");
// Variable to store the HTTP request 
String jsonString;
String str, status_str, wsstr;
JSONVar data_jsn, readings, wsreadings;

//******************************* Extern variable *******************
extern double Measured_Temp, Curr_Temp, valComputePID, OutputVal;
extern int8_t Mode;
extern int8_t State;
extern int16_t deltaTemp;
extern int tProg;
extern double coeffTempTable;
extern int getPower;
extern double steepness;
extern int8_t Current_pos;
extern double Kp, Kd, Ki, KpTracking, KiTracking, KdTracking;
extern int twoZonePID;
extern int minimize;
extern int switchTemp;
//*******************************************************************
void initWIFI(void);
//****************************** WebServer Handle *******************
void initWebServer();
String getStatusReadings();
String getTempReadings();
String getGdbvarReadings();
//******************************** WebSocket Handle ******************

//******************* функции для обработки данных отправляемых сервером **********
void notifyStartClients() {

    setModeTest();
    Current_pos = 2;
    tProg=0;

    wsreadings="";
    wsreadings["varStatus"] = "Start";
    wsreadings["unitProg"] = unitProg;
    jsonString = JSON.stringify(wsreadings);
    ws.textAll(jsonString);
}

void notifyStartClientsProg(){
    setModeProgramSnPb();
    tProg = 0;
    
    wsreadings="";
    wsreadings["varStatus"] = "Start";
    wsreadings["unitProg"] = unitProg;
    jsonString = JSON.stringify(wsreadings);
    ws.textAll(jsonString);
}

void notifyStopClients(){
    setModeStandby();
    Current_pos = 2;
    tProg = 0;    
    wsreadings="";
    wsreadings["varStatus"] = "Stop";
    jsonString = JSON.stringify(wsreadings);
    ws.textAll(jsonString);
}

void notifyClients() {
    wsreadings="";
    wsreadings["vCurrT"] = Curr_Temp;
    wsreadings["vMesT"] = Measured_Temp;
    wsreadings["vtProg"] = tProg;      
    wsreadings["vComputePID"] = valComputePID;
    wsreadings["vgetPower"] = getPower;  
    jsonString = JSON.stringify(wsreadings);
    ws.textAll(jsonString);
}

void notifyLogClients(const String &message) {
    if (ws.count() == 0) {
        return;
    }
    JSONVar logReadings;
    logReadings["type"] = "log";
    logReadings["message"] = message;
    logReadings["uptimeMs"] = (double)millis();
    String logJson = JSON.stringify(logReadings);
    ws.textAll(logJson);
}

//********************* WiFI ****************************
void initWIFI(void){
    // Connect to Wi-Fi network with SSID and password
    #ifdef STA    
        WiFi.mode(WIFI_STA); //  WIFI_AP
        WiFi.begin(ssid, password); 
        // Wait for connection
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(ssid);
        Serial.print("IP address: ");
        IP = WiFi.localIP();
        Serial.println(IP);
    #endif
    #ifdef AP  
        WiFi.softAP(ssid, password); 
        Serial.print("IP address: ");
        IP = WiFi.softAPIP();
        Serial.println(IP);
    #endif
}
//***************** WebServer ****************************
void initWebServer(){
  // Route for index and dgb web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/index.html", "text/html",false);
    });
    server.on("/gdburl", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/dbg.html", "text/html",false);
    });
    // Route for request data format JSON
    // Request for the latest temperature, STATUS, MODE and ohter data readings
    server.on("/tempurl", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = getTempReadings();
        request->send(200, "application/json", json);
        json = String();
    });
    server.on("/statusurl", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = getStatusReadings();
        request->send(200, "application/json", json);
        json = String();
    });
    server.on("/gdbvarurl", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = getGdbvarReadings();
        request->send(200, "application/json", json);
        json = String();
    });
    server.serveStatic("/", SPIFFS, "/");

    AsyncElegantOTA.begin(&server);    // Start ElegantOTA
    server.begin();
    Serial.println("HTTP server started");
    notifyLogClients("HTTP server started");
}
// Get Sensor Readings and return JSON object
String getTempReadings(){
    readings["varCurrT"] = String(Curr_Temp);
    readings["varMesT"] =  String(Measured_Temp);
    readings["varComputePID"] =  String(valComputePID);
    String jsonString = JSON.stringify(readings);
    return jsonString;
}

String getStatusReadings(){
    str=" ";
    if(Mode == 0){
        str += " MODE_FLAG_STANDBAY ";
    }
    if(Mode&MODE_FLAG_MANUAL_HEATING){
            str += " MANUAL STATUS_FLAG_HEATING";
    }
    if(Mode&MODE_FLAG_PROG_HEATING){
            str += " PROG STATUS_FLAG_HEATING";
    }
    if(Mode&MODE_FLAG_SnPb){
            str += " MODE_FLAG_SnPb";
    }
    if(Mode&MODE_FLAG_PbFree){
            str += " MODE_FLAG_PbFree";
    }
    if(Mode&MODE_FLAG_TEST){
            str += " MODE_FLAG_TEST";
    }
    readings["varMode"] = str;
    status_str=" ";
    if(State&STATUS_FLAG_HEATING){
            status_str += " STATUS_FLAG_HEATING ";
    }
    if(State&STATUS_FLAG_CUR_MES){
            status_str += " STATUS_FLAG_CUR_MES ";
    }   
    if(State&STATUS_FLAG_Tracking){
            status_str += " STATUS_FLAG_Tracking ";
    }       
    readings["varStatus"] = status_str;
    jsonString = JSON.stringify(readings);
    return jsonString;
}

String getGdbvarReadings(){
    readings["vardeltaTemp"] = String(deltaTemp);
    readings["vartProg"] =  String(tProg);
    readings["vargetPower"] =  String(getPower);
    readings["varcoeffTempTable"] =  String(coeffTempTable);
    readings["varsteepness"] =  String(steepness);
    readings["pidInput"] = String(pidInput);
    readings["pidOutput"] = String(pidOutput);
    readings["pidSetpoint"] = String(pidSetpoint);
    String jsonString = JSON.stringify(readings);
    return jsonString;
}

//******************* функция для обработки сообщений, полученных от клента **********
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String payload;
        payload.reserve(len + 1);
        for (size_t i = 0; i < len; i++) {
            payload += (char)data[i];
        }

    data_jsn = JSON.parse(payload);
    if (JSON.typeof(data_jsn) == "undefined") {
        Serial.println("Parsing input failed!");
                Serial.println(payload);
        return;
    }

    int wsMode = data_jsn["Start"];

    if(wsMode == 0){
        notifyStopClients();
    }
    if(wsMode == 1){
        Kp = data_jsn["Kp"];
        Kd = data_jsn["Kd"];
        Ki = data_jsn["Ki"];

        // Optional two-zone PID parameters (if not provided, keep defaults)
        if (JSON.typeof(data_jsn["twoZone"]) != "undefined") {
            twoZonePID = (int)data_jsn["twoZone"];
        }
        if (JSON.typeof(data_jsn["KpTracking"]) != "undefined") {
            KpTracking = (double)data_jsn["KpTracking"];
        }
        if (JSON.typeof(data_jsn["KiTracking"]) != "undefined") {
            KiTracking = (double)data_jsn["KiTracking"];
        }
        if (JSON.typeof(data_jsn["KdTracking"]) != "undefined") {
            KdTracking = (double)data_jsn["KdTracking"];
        }

        unitProg = data_jsn["unitProg"];
        Curr_Temp = data_jsn["Curr_Temp"];
        minimize = data_jsn["minimize"];
        switchTemp = data_jsn["switchTemp"];
        notifyStartClients();
    }
    if(wsMode == 2){
        if (JSON.typeof(data_jsn["unitProg"]) != "undefined") {
            unitProg = (int)data_jsn["unitProg"];
        }
        startProgramByIndex(0);
        wsreadings="";
        wsreadings["varStatus"] = "Start";
        wsreadings["unitProg"] = unitProg;
        jsonString = JSON.stringify(wsreadings);
        ws.textAll(jsonString);
    }
    if(wsMode == 3){
        startProgramByIndex(1);
        wsreadings="";
        wsreadings["varStatus"] = "Start";
        wsreadings["unitProg"] = unitProg;
        jsonString = JSON.stringify(wsreadings);
        ws.textAll(jsonString);
    }
    if(wsMode == 4){
        setModeManualHeating();
        Current_pos = 2;
        tProg = 0;
        wsreadings="";
        wsreadings["varStatus"] = "Start";
        wsreadings["unitProg"] = unitProg;
        jsonString = JSON.stringify(wsreadings);
        ws.textAll(jsonString);
    }
  }
}

//******************* Чтение данных от клиент ws *************************************
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
        Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        notifyLogClients("WebSocket client #" + String(client->id()) + " connected from " + client->remoteIP().toString());;
        break;
    case WS_EVT_DISCONNECT:
        Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        notifyLogClients("WebSocket client #" + String(client->id()) + " disconnected");
        break;
    case WS_EVT_ERROR:
        Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
        break;
    case WS_EVT_PONG:
        Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
        break;
    case WS_EVT_DATA:
        Serial.printf("data from %s :\n", client->remoteIP().toString().c_str());
        handleWebSocketMessage(arg, data, len);
        break;
  }
}
//************************* WebSocket ***********************************
void initWebSocket(void) {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  Serial.println("WebSocket started");
    notifyLogClients("WebSocket started");
}








