#include <Arduino.h>
// Load Wi-Fi library
#include "wifi.h"
#include "display.h"

#include <Wire.h>
#include "max6675.h"
#include <AiEsp32RotaryEncoder.h>
#include <RBDdimmer.h>
#include <QuickPID.h>
#include "image.h"
#include "main.h"

//#define ARDUINO_ARCH_ESP32
//#define __DEBUG__

//********************* User variables ******************************
int8_t Button;

//***int8_t Mode  mode reg (Режим роботы нагревательного стола ) **************************
//    7     6      5       4       3       2            1           0
// |     |     | FLAG_PROG1 | FLAG_PROG0 |FLAG_TEST|FLAG_PROG_HEATING|FLAG_MANUAL_HEATING|FLAG_STANDBAY|
//
//********************************************************************
int8_t Mode;

//****int8_t State   status mode reg (Статус нагревательного стола ) ****************
//    7     6     5     4     3       2       1       0
// |     |     |     |     |     |FLAG_Tracking|FLAG_CUR_MES|FLAG_HEATING|
//
//********************************************************************
int8_t State;//

int8_t Current_pos = 2;

double Temperature, Measured_Temp, valComputePID=0, Curr_Temp=10;
float pidInput = 0, pidOutput = 0, pidSetpoint = 0;
double steepness;
double coeffTempTable = 0; // температурный коэфициент нагревательного стола
int tProg;
int getPower;
int16_t deltaTemp; 

// array temperature on time for programing heating
int16_t Prog0[4][2]={{50, 125},{120, 125},{210, 235},{240, 0}};// SnPb
int16_t Prog1[4][2]={{50, 175},{180, 175},{210, 260},{240, 0}};// Pb-free

typedef struct 
{
	uint8_t id;
	uint8_t num_selections;
	String  Str;
  uint8_t XPOS;
  uint8_t YPOS;
	uint8_t (*function)(int);
	uint8_t *fn_arg;
} menu; 

menu prog[2][7]={
  {0, 0, "SnPb", 25, 24, 0, (uint8_t*)Prog0},
  {1, 1, "Pb-free", 25, 48, 0, (uint8_t*)Prog1},
};

//********************************************************************
bool temp_loop_pntr(double *temperature);
void model_loop(void);
bool loop_GUI(double *temperature);
uint8_t rotary_EncoderButton(void);
void controler_loop(void);

static void logWifi(const String &message) {
  Serial.println(message);
  notifyLogClients(message);
}
//******************** Define object ************************
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
                    ROTARY_ENCODER_A_PIN, \
                    ROTARY_ENCODER_B_PIN, \
                    ROTARY_ENCODER_BUTTON_PIN, \
                    ROTARY_ENCODER_VCC_PIN, \
                    ROTARY_ENCODER_STEPS);
MAX6675 thermocouple(MAX6675_CLK, MAX6675_CS, MAX6675_DO);
dimmerLamp TableHeat(OUTPUT_PIN, ZEROCROSS); //initialase port for dimmer for ESP8266, ESP32, Arduino due boards
QuickPID TableHeatPID(&pidInput, &pidOutput, &pidSetpoint); // QuickPID instance

//********************** Initialize ISR for rodaryEncoder ***************
void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}

//********************** Initialize SPIFFS ******************************
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}


//***********************************************************************
void setup() {
  Serial.begin(115200);
  initDisplay();
  DislayLogo();
  initSPIFFS();
  initWIFI();
  initWebServer();
  initWebSocket();
  //***************************AotaryEncoder    *********************
  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  //rotaryEncoder.setup([]{rotaryEncoder.readEncoder_ISR();}); // установка прерываний для Энкодера
  //optionally we can set boundaries and if values should cycle or not
  bool circleValues = false;
  rotaryEncoder.setBoundaries(0, 280, circleValues);// Установка границы от текущей темрературы до максимальной
  rotaryEncoder.setAcceleration(100); //or set the value - larger number = more accelearation; 0 or 1 means disabled acceleration
  //rotaryEncoder.disableAcceleration(); //acceleration is now enabled by default - disable if you dont need it
  rotaryEncoder.setEncoderValue(25); //int(thermocouple.readCelsius()) init start value rotary encoder
  logWifi("RotaryEncoder init");
  // ***************************    PID regulator    **************************
  pidSetpoint = (float)Curr_Temp;
  Kp=0.299, Ki=0.005, Kd=0.0;
  // Near-setpoint defaults for inertial heater (can be overridden via WebSocket)
  KpTracking = Kp * 0.45;
  KiTracking = Ki * 0.20;
  KdTracking = 0.0;
  twoZonePID = 1;
  //Kp=11.14817586323742732, Ki=0.0093430506411197874, Kd=0.013430506411197874;
  //Kp=1, Ki=0.307, Kd=27.475;
  switchTemp=3;

  TableHeatPID.SetTunings((float)Kp, (float)Ki, (float)Kd); // Tune the PID, arguments: kP, kI, kD
  TableHeatPID.SetOutputLimits(0, 100); // Limit output 0-100% for NORMAL_MODE dimmer
  TableHeatPID.SetMode(QuickPID::Control::automatic);
  logWifi("PID regulator init");
  // ***************************    RBD dimmer    **************************
  TableHeat.begin(NORMAL_MODE, OFF); //dimmer initialisation: NORMAL_MODE  TOGGLE_MODE
  TableHeat.setPower(0);
  logWifi("RBD dimmer init");
  //***************************************
  Mode = 0;
  State= 0;
  unitProg=600;
  minimize=10;
}
//***********************************************************************
void loop() {
  controler_loop();       // управление режимами
  model_loop();
  loop_GUI(&Temperature); // вывод на OLED
  ws.cleanupClients();
}
//***********************************************************************
void controler_loop(void){

  uint8_t button; 
  static unsigned long lastTimeWait;//=0
  static unsigned long lastTimeProg;//=0
  static unsigned long lastSetTempWait;
  //*****************************************
  // Encoder rotary
  //*****************************************
  // действия на вращения енкодера в разных режимах
	if (rotaryEncoder.encoderChanged())
	{
    if(Mode&FLAG_STANDBAY){
      State &= ~FLAG_HEATING;
      return;
    } 
    if(Mode&FLAG_MANUAL_HEATING){
      State |= FLAG_CUR_MES;      // Установка флага вывода выбранной(ручной) температуры 
      State &= ~FLAG_HEATING;     // Выключение нагревателя
      Curr_Temp = (double)rotaryEncoder.readEncoder();// чтение энкодера и запись в выбраную температуру
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);   // Установка температуры нагрева стола
    }
    if(Mode&FLAG_PROG_HEATING){
      Current_pos++; // Выбор режима роботы нагревателя SnPb или Pb-free 
      if(Current_pos<=0){
        Current_pos=2;
      }
      else if(Current_pos>2){
        Current_pos=0;
      }
    }
    if(Mode & FLAG_TEST){
      Current_pos = rotaryEncoder.encoderChanged();
      if (Current_pos>0) Serial.println("11111");
      if (Current_pos<0) Serial.println("22222"); 
    }
	}
  //*****************************************
  //  Button click
  //*****************************************
  // действия на нажатие кнопки енкодера в разных режимах
  button = rotary_EncoderButton();
  if(button){
    State &= ~FLAG_HEATING;     // Выключение нагревателя
    #ifdef __DEBUG__
      Serial.print(" button=");
      Serial.println(String(button, BIN));
      //Serial.print(" Mode =");
      //Serial.println(String(Mode, BIN));
    #endif
    // Выбор режимов роботы Heating Table
    // Короткое нажатие кнопки
    if(button & FLAG_SHORT_PRESS){
      if(Mode == FLAG_STANDBAY){
        Mode &= ~FLAG_STANDBAY;
        Mode |= FLAG_MANUAL_HEATING;
        tProg=0;
        #ifdef __DEBUG__
          Serial.println("Mode == FLAG_MANUAL_HEATING");
        #endif
      }
      else if(Mode&FLAG_MANUAL_HEATING){
        Mode &= ~FLAG_MANUAL_HEATING;
        Mode |= FLAG_PROG_HEATING;
        tProg=0;
        TableHeat.setState(OFF); // State(ON/OFF);
      }
      else if((Mode&FLAG_PROG_HEATING) && (Current_pos == 0)){
        Mode |= FLAG_PROG0;
        Mode &= ~FLAG_PROG1;
        tProg=0;
      }
      else if((Mode&FLAG_PROG_HEATING) && (Current_pos == 1)){
        Mode |= FLAG_PROG1;
        Mode &= ~FLAG_PROG0;
        tProg=0;
      }
      // Выход из програмного режима 
      else if(Mode&FLAG_PROG_HEATING && Current_pos == 2){
        Mode &= ~FLAG_PROG_HEATING;
        Mode &= ~FLAG_PROG1;
        Mode &= ~FLAG_PROG0;        
        Mode |= FLAG_STANDBAY;
        Current_pos = 2;
        tProg=0;
        TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == StandBay");
        #endif
      }
      #ifdef __DEBUG__
        else if(Mode&FLAG_TEST){
        Mode &= ~FLAG_TEST;
        Mode |= FLAG_STANDBAY;
        Current_pos = 2;
        tProg = 0;
        TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == FLAG_STANDBAY");
        #endif
        }
      #endif      
    }
    // Длинное нажатие кнопки
    if(button&FLAG_LONG_PRESS){
        #ifdef __DEBUG__
          Serial.println("A detected is LONG press in controler_loop");
        #endif
        Mode &= ~FLAG_PROG0;
        Mode &= ~FLAG_PROG1;
    }
  }
  // myflags |= option4; // включаем option4  
  // myflags &= ~option4; // выключаем option4
  // myflags ^= option4; // включаем или выключаем(ИНВЕРТИРУЕМ) option4 
  // if (myflags & option4) ... // если option4 установлено - что-нибудь делаем
  //  if ((mask & value) != 0U)
  //  {
  //      value &= ~myflags; clear
  //  }
  //**** Сброс бита для вывода устанавливаемой температуры через 3 сек *********
  if((State&FLAG_CUR_MES) && (millis()-lastTimeWait > GUI_TIME_DELAY)){
    State &= ~FLAG_CUR_MES; // переключения на вывод измеренной температуры
    lastTimeWait = millis();
  }
  if(Mode&FLAG_TEST || Mode&FLAG_MANUAL_HEATING || Mode&FLAG_PROG0 || Mode&FLAG_PROG1){// Активный режим нагрева
    State |= FLAG_HEATING;  // Включение флага нагревателя
    TableHeat.setState(ON); // Включение нагревателя State(ON/OFF);
  }
  else{
    TableHeat.setState(OFF); // Выключение нагревателя State(ON/OFF);
    TableHeat.setPower(0);
    State &= ~FLAG_HEATING;  // Выключение флага нагревателя
    State &= ~FLAG_Tracking;
  }
  if((tProg>=unitProg) && (Mode&FLAG_TEST || Mode&FLAG_PROG0 || Mode&FLAG_PROG1)){ //Выключение режима Test через tProg сек
    TableHeat.setState(OFF); // State(ON/OFF);
    Current_pos = 2;
    tProg = 0;
    Mode &= ~FLAG_TEST;
    Mode &= ~FLAG_PROG_HEATING;
    Mode &= ~FLAG_PROG0;
    Mode &= ~FLAG_PROG1;
    Mode |= FLAG_STANDBAY;
    notifyStopClients();
  }
  else{
    pidSetpoint = (float)(Curr_Temp + calibrateTemp);// Set setpoint = Curr_Temp from PC + callibrate Temperature
  }
}

//########################################################################################
bool temp_loop_pntr(double *temperature){
  static float lastTemp;
	static unsigned long lastTempUpdate = 0;
  
	// Переодическое измерение температуры
	if (millis() - lastTempUpdate >  TEMP_READ_DELAY)
	{
    *temperature = double(thermocouple.readCelsius());
    lastTempUpdate = millis();
		return true;
	}
  return false;
}
// Дабовлен 18.03.23 Curr_Temp + calibrateTemp для компенсации ошибки при нагреве
void model_loop(void){

  static unsigned long lastTimeProg;

  temp_loop_pntr(&Measured_Temp);// измерение температуры
  pidInput = (float)Measured_Temp;
  //***********  Управление температурой PID контролером и Dimmer *****************
  if(Mode&FLAG_MANUAL_HEATING){
    TableHeatPID.Compute();
    valComputePID = pidOutput;
  } 
  else if(Mode&FLAG_PROG_HEATING && Mode&FLAG_PROG0){
    if(tProg < 240){
      Curr_Temp = 140;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if((240 < tProg) && (tProg < 340)){
      Curr_Temp = 235;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if((340 < tProg) && (tProg < 345)){
      Curr_Temp = 235;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if((345 < tProg) && (tProg < 600)){
      Curr_Temp = 150;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if(tProg > 600){
      tProg = 0;
      Mode &= ~FLAG_PROG0;     // Выключение режима FLAG_PROG0
      State &= ~FLAG_HEATING;  // Выключение нагревателя
      Current_pos = 2;
    }
    TableHeatPID.Compute();
    valComputePID = pidOutput;
  }
  else if(Mode&FLAG_PROG_HEATING && Mode&FLAG_PROG1){
    if(tProg < 240){
      Curr_Temp = 140;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if((240 < tProg) && (tProg < 340)){
      Curr_Temp = 260;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if((340 < tProg) && (tProg < 345)){
      Curr_Temp = 260;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if((345 < tProg) && (tProg < 600)){
      Curr_Temp = 150;
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);
    }
    else if(tProg > 600){
      tProg = 0;
      Mode &= ~FLAG_PROG1;     // Выключение режима FLAG_PROG0
      State &= ~FLAG_HEATING;  // Выключение нагревателя
      Current_pos = 2;
    }
    TableHeatPID.Compute();
    valComputePID = pidOutput;
  }
  else if(Mode&FLAG_TEST){
    TableHeatPID.Compute();
    valComputePID = pidOutput;
  } 
  if(State&FLAG_CUR_MES) {//вывод на OLED температуры измереной или устанволеной
    Temperature = Curr_Temp;
  }
  else{
    Temperature = Measured_Temp;
  }     
  if(State&FLAG_HEATING){ //
    static unsigned long lastPowerLogMs = 0;
    TableHeat.setPower((int)valComputePID);
    getPower = TableHeat.getPower();
    if (millis() - lastPowerLogMs >= 1000) {
      logWifi(
        "heater power=" + String(getPower) +
        " measured=" + String(Measured_Temp, 1) +
        " set=" + String(Curr_Temp, 1)
      );
      lastPowerLogMs = millis();
    }
    //getPower = (int)valComputePID;
    deltaTemp = (int16_t)(Curr_Temp - Measured_Temp);
    /**/
    if(deltaTemp <= switchTemp){
      if (twoZonePID) {
        TableHeatPID.SetTunings((float)KpTracking, (float)KiTracking, (float)KdTracking);
      } else {
        TableHeatPID.SetTunings((float)Kp, (float)Ki, (float)Kd);
      }
      TableHeatPID.SetOutputLimits((float)minimize, 100); // tracking mode: min..100%
      State |= FLAG_Tracking;
    }
    else{
      TableHeatPID.SetTunings((float)Kp, (float)Ki, (float)Kd);
      TableHeatPID.SetOutputLimits(0, 100); // full range: 0..100%
      State &= ~FLAG_Tracking;
    }    
  }
  // counter tPROG step 1 sec
  if((Mode&FLAG_TEST || Mode&FLAG_PROG0 || Mode&FLAG_PROG1) && ((millis()-lastTimeProg) > 1000)) {
    tProg += 1;//(millis()-lastTimeProg)/1000;
    steepness = Measured_Temp/tProg; //крутизна температуры
    notifyClients();      
    lastTimeProg = millis();
  }
}

bool loop_GUI(double *temperature){

	static unsigned long lastGUIUpdate = 0;
  static char outstr[6];
  int selection_num;

	// Плавное отображение(вывод) изменения температуры
	if (millis() - lastGUIUpdate > GUI_UPDATE_DELAY)
	{
    display.clearDisplay();
    if(Mode == FLAG_STANDBAY){
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(20, 45);
      display.print("Standbay");
      display.setFont(&Picopixel);
      display.setCursor(35, 55);
      display.print(IP.toString());
      display.setFont(&FreeSerif9pt7b);

      DisplayTemp(25, 25, temperature);  
      display.display();
    }
    if(Mode&FLAG_MANUAL_HEATING){
      //display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      // Выбор стрелок или нагревания 
      if(State&FLAG_CUR_MES){  
        display.drawBitmap(5, 20, epd_bitmap_up, 20, 36, WHITE); // заменить 36 на 40
        display.drawBitmap(110, 20, epd_bitmap_down, 20, 36, WHITE);        
      }
      else if(State&FLAG_HEATING && (Curr_Temp > Measured_Temp )){
        display.drawBitmap(5, 20, epd_bitmap_heating_table, 22, 21, WHITE);// подобрать 22,21
      }
      DisplayTemp(30, 40, temperature); 
      DisplayPwr(110, 10, &valComputePID);
      display.display();
    }
    if(Mode&FLAG_PROG_HEATING){
      display.setFont(&FreeSerif12pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);    
      // Вывод меню на экран
      if(!(Mode&FLAG_PROG0 || Mode&FLAG_PROG1)){
        for(selection_num=0; selection_num < 2; selection_num ++){
          if( prog[selection_num]->num_selections == Current_pos ){
            display.setCursor(prog[selection_num]->XPOS-14, prog[selection_num]->YPOS);
            display.print(">");
          }
          display.setCursor(prog[selection_num]->XPOS, prog[selection_num]->YPOS);
          display.print(prog[selection_num]->Str);
        }
      }
      else if(Mode&FLAG_PROG0){
        DisplayProg(tProg, temperature, &valComputePID);
      }
      else if(Mode&FLAG_PROG1){
        DisplayProg(tProg, temperature, &valComputePID);
      }
      display.display();
    }
    if(Mode&FLAG_TEST){
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(10, 15);
      display.print("Test");
      
      display.setCursor(1, 35);
      display.print(String(Kp,1));
      display.setCursor(40, 35);
      display.print(String(Ki,1));
      display.setCursor(75, 35);
      display.print(String(Kd,1));

      //DisplayTemp(1, 55, &Curr_Temp); 
      DisplayTemp(70, 60, temperature); 
      display.setFont(&Picopixel);
      display.setCursor(70, 10);
      display.print(String(tProg));
      DisplayPwr(110, 10, &valComputePID);
      display.display();
    }
    lastGUIUpdate = millis();
		return true;
	}
  return false;
 }

//########################################################################################
uint8_t rotary_EncoderButton(void){
    static bool buttonWasDown = false;
    static unsigned long buttonPressedAt = 0;
    unsigned long pressDuration = 0;

    bool buttonIsDown = rotaryEncoder.isEncoderButtonDown();
    if (buttonIsDown && !buttonWasDown) {
      buttonPressedAt = millis();
      buttonWasDown = true;
    }

    if (!buttonIsDown && buttonWasDown) {
      pressDuration = millis() - buttonPressedAt;
      buttonWasDown = false;
    }

   if( (SHORT_PRESS_TIME < pressDuration) && (LONG_PRESS_TIME > pressDuration ) ){
      #ifdef __DEBUG__
        Serial.println("A SHORT press is detected");
      #endif
      return FLAG_SHORT_PRESS;
   }
   if( LONG_PRESS_TIME < pressDuration){
      #ifdef __DEBUG__
        Serial.println("A FLAG_LONG_PRESS is detected");
      #endif
      return FLAG_LONG_PRESS;      
    }
   return FLAG_NOT_PRESS;
}
