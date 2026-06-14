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
// |     |     | MODE_FLAG_PbFree | MODE_FLAG_SnPb |MODE_FLAG_TEST|MODE_FLAG_PROG_HEATING|MODE_FLAG_MANUAL_HEATING|MODE_FLAG_STANDBAY|
//
//********************************************************************
int8_t Mode;

static inline void clearModeFlags(void) {
  Mode &= ~(MODE_FLAG_MANUAL_HEATING | MODE_FLAG_PROG_HEATING | MODE_FLAG_TEST | MODE_FLAG_SnPb | MODE_FLAG_PbFree);
}

void setModeStandby(void) {
  clearModeFlags();
}

void setModeManualHeating(void) {
  clearModeFlags();
  Mode |= MODE_FLAG_MANUAL_HEATING;
}

void setModeProgramSnPb(void) {
  clearModeFlags();
  Mode |= (MODE_FLAG_PROG_HEATING | MODE_FLAG_SnPb);
}

void setModeProgramPbFree(void) {
  clearModeFlags();
  Mode |= (MODE_FLAG_PROG_HEATING | MODE_FLAG_PbFree);
}

void startProgramByIndex(int index) {
  if (index == 0) {
    setModeProgramSnPb();
    Current_pos = 0;
  } else if (index == 1) {
    setModeProgramPbFree();
    Current_pos = 1;
  } else {
    return;
  }

  tProg = 0;
}

void setModeTest(void) {
  clearModeFlags();
  Mode |= MODE_FLAG_TEST;
}

//****int8_t State   status mode reg (Статус нагревательного стола ) ****************
//    7     6     5     4     3       2       1       0
// |     |     |     |     |     |STATUS_FLAG_Tracking|STATUS_FLAG_CUR_MES|STATUS_FLAG_HEATING|
//
//********************************************************************
int8_t State;
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
  Kp=0.6827, Ki=0.0001, Kd=0.0;
  // Near-setpoint defaults for inertial heater (can be overridden via WebSocket)
  KpTracking = 0.1792, KiTracking = 0.0004, KdTracking = 0.0;
  twoZonePID = 1;
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
  setModeStandby();
  State= 0;
  unitProg = 600;
  minimize=5;
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
  static long lastEncoderValue = 0;
  //*****************************************
  // Encoder rotary
  //*****************************************
  // действия на вращения енкодера в разных режимах
	if (rotaryEncoder.encoderChanged())
	{
    long encoderValue = rotaryEncoder.readEncoder();
    long step = 0;
    if (encoderValue > lastEncoderValue) {
      step = 1;
    } else if (encoderValue < lastEncoderValue) {
      step = -1;
    }
    lastEncoderValue = encoderValue;

    if(Mode == MODE_FLAG_STANDBAY){
      State &= ~STATUS_FLAG_HEATING;
      return;
    } 
    if(Mode&MODE_FLAG_MANUAL_HEATING){
      State |= STATUS_FLAG_CUR_MES;      // Установка флага вывода выбранной(ручной) температуры 
      State &= ~STATUS_FLAG_HEATING;     // Выключение нагревателя
      Curr_Temp = (double)encoderValue;// чтение энкодера и запись в выбраную температуру
      pidSetpoint = (float)(Curr_Temp + calibrateTemp);   // Установка температуры нагрева стола
    }
    if((Mode&MODE_FLAG_PROG_HEATING) && !(Mode&MODE_FLAG_SnPb) && !(Mode&MODE_FLAG_PbFree)){
      if (step > 0) {
        Current_pos++;
      } else if (step < 0) {
        Current_pos--;
      }

      if (Current_pos > 2) {
        Current_pos = 0;
      } else if (Current_pos < 0) {
        Current_pos = 2;
      }
    }
    if(Mode & MODE_FLAG_TEST){
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
    State &= ~STATUS_FLAG_HEATING;     // Выключение нагревателя
    #ifdef __DEBUG__
      Serial.print(" button=");
      Serial.println(String(button, BIN));
      //Serial.print(" Mode =");
      //Serial.println(String(Mode, BIN));
    #endif
    // Выбор режимов роботы Heating Table
    // Короткое нажатие кнопки
    if(button & FLAG_SHORT_PRESS){
      if(Mode == MODE_FLAG_STANDBAY){
        setModeManualHeating();
        tProg = 0;
        #ifdef __DEBUG__
          Serial.println("Mode == MODE_FLAG_MANUAL_HEATING");
        #endif
      }
      else if(Mode&MODE_FLAG_MANUAL_HEATING){
        clearModeFlags();
        Mode |= MODE_FLAG_PROG_HEATING;
        Current_pos = 0;
        tProg = 0;
        TableHeat.setState(OFF); // State(ON/OFF);
      }
      else if((Mode&MODE_FLAG_PROG_HEATING) && (Current_pos == 0)){
        startProgramByIndex(0);
      }
      else if((Mode&MODE_FLAG_PROG_HEATING) && (Current_pos == 1)){
        startProgramByIndex(1);
      }
      else if(Mode&MODE_FLAG_PROG_HEATING && Current_pos == 2){// Выход из програмного режима 
        setModeStandby();
        Current_pos = 2;
        tProg = 0;
        TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == StandBay");
        #endif
      }
      #ifdef __DEBUG__
        else if(Mode&MODE_FLAG_TEST){
        setModeStandby();
        Current_pos = 2;
        tProg = 0;
        TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == MODE_FLAG_STANDBAY");
        #endif
        }
      #endif      
    }
    // Длинное нажатие кнопки
    if(button&FLAG_LONG_PRESS){
        #ifdef __DEBUG__
          Serial.println("A detected is LONG press in controler_loop");
        #endif
        Mode &= ~MODE_FLAG_SnPb;
        Mode &= ~MODE_FLAG_PbFree;
    }
  }
  //**** Сброс бита для вывода устанавливаемой температуры через 3 сек *********
  if((State&STATUS_FLAG_CUR_MES) && (millis()-lastTimeWait > GUI_TIME_DELAY)){
    State &= ~STATUS_FLAG_CUR_MES; // переключения на вывод измеренной температуры
    lastTimeWait = millis();
  }
  if(Mode&MODE_FLAG_TEST || Mode&MODE_FLAG_MANUAL_HEATING || Mode&MODE_FLAG_SnPb || Mode&MODE_FLAG_PbFree){// Активный режим нагрева
    State |= STATUS_FLAG_HEATING;  // Включение флага нагревателя
    TableHeat.setState(ON); // Включение нагревателя State(ON/OFF);
  }
  else{
    TableHeat.setState(OFF); // Выключение нагревателя State(ON/OFF);
    TableHeat.setPower(0);
    State &= ~STATUS_FLAG_HEATING;  // Выключение флага нагревателя
    State &= ~STATUS_FLAG_Tracking;
  }
  if((tProg>=unitProg) && (Mode&MODE_FLAG_TEST || Mode&MODE_FLAG_SnPb || Mode&MODE_FLAG_PbFree)){ //Выключение режима Test через tProg сек
    TableHeat.setState(OFF); // State(ON/OFF);
    Current_pos = 2;
    tProg = 0;
    setModeStandby();
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
  if(Mode&MODE_FLAG_MANUAL_HEATING){
    valComputePID = pidOutput;
  } 
  else if ((Mode & MODE_FLAG_PROG_HEATING) && (Mode & MODE_FLAG_SnPb)) {
    if (tProg <= SNPB_T_PREHEAT_END) {
      Curr_Temp = SNPB_TEMP_PREHEAT;
    } else if (tProg <= SNPB_T_REFLOW_END) {
      Curr_Temp = SNPB_TEMP_REFLOW;
    } else if (tProg <= SNPB_T_PEAK_END) {
      Curr_Temp = SNPB_TEMP_REFLOW;
    } else if (tProg <= SNPB_T_COOL_END) {
      Curr_Temp = SNPB_TEMP_COOL;
    } else {
      tProg = 0;
      Mode &= ~MODE_FLAG_SnPb;
      State &= ~STATUS_FLAG_HEATING;
      Current_pos = 2;
    }
   pidSetpoint = (float)(Curr_Temp + calibrateTemp);
  }
  else if ((Mode & MODE_FLAG_PROG_HEATING) && (Mode & MODE_FLAG_PbFree)) {
  if (tProg <= PBFREE_T_PREHEAT_END) {
    Curr_Temp = PBFREE_TEMP_PREHEAT;
  } else if (tProg <= PBFREE_T_REFLOW_END) {
    Curr_Temp = PBFREE_TEMP_REFLOW;
  } else if (tProg <= PBFREE_T_PEAK_END) {
    Curr_Temp = PBFREE_TEMP_REFLOW;
  } else if (tProg <= PBFREE_T_COOL_END) {
    Curr_Temp = PBFREE_TEMP_COOL;
  } else {
    tProg = 0;
    Mode &= ~MODE_FLAG_PbFree;
    State &= ~STATUS_FLAG_HEATING;
    Current_pos = 2;
  }
  pidSetpoint = (float)(Curr_Temp + calibrateTemp);
}
  else if(Mode&MODE_FLAG_TEST){
    valComputePID = pidOutput;
  } 
  if(State&STATUS_FLAG_CUR_MES) {//вывод на OLED температуры измереной или устанволеной
    Temperature = Curr_Temp;
  }
  else{
    Temperature = Measured_Temp;
  }     
  if(State&STATUS_FLAG_HEATING){ //
    static unsigned long lastPowerLogMs = 0;
    // Вычисляем ошибку до выбора зоны регулирования.
    deltaTemp = (int16_t)(Curr_Temp - Measured_Temp);
    // Переходим в tracking только вблизи уставки при подходе снизу.
    // При перелете (deltaTemp <= 0) не держим минимальную мощность,
    // иначе нагрев не может стабилизироваться.
    if ((deltaTemp > 0) && (deltaTemp <= switchTemp)) {
      if (twoZonePID == 1) {
        TableHeatPID.SetTunings((float)KpTracking, (float)KiTracking, (float)KdTracking);
      } else {
        TableHeatPID.SetTunings((float)Kp, (float)Ki, (float)Kd);
      }
      TableHeatPID.SetOutputLimits((float)minimize, 100); // tracking mode: min..100%
      State |= STATUS_FLAG_Tracking;
    }
    else {
      // В разгонной зоне и при перелете разрешаем нулевую мощность.
      if ((deltaTemp <= 0) && twoZonePID == 1) {
        TableHeatPID.SetTunings((float)KpTracking, (float)KiTracking, (float)KdTracking);
      } else {
        TableHeatPID.SetTunings((float)Kp, (float)Ki, (float)Kd);
      }
      TableHeatPID.SetOutputLimits(0, 100); // full range: 0..100%
      State &= ~STATUS_FLAG_Tracking;
    }
    // Пересчет PID после выбора текущей зоны.
    TableHeatPID.Compute();
    valComputePID = pidOutput;
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

    if (deltaTemp <= 0) {
      State &= ~STATUS_FLAG_Tracking;
    }
  }
  // counter tPROG step 1 sec
  if((Mode&MODE_FLAG_TEST || Mode&MODE_FLAG_SnPb || Mode&MODE_FLAG_PbFree) && ((millis()-lastTimeProg) > 1000)) {
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
    if(Mode == MODE_FLAG_STANDBAY){
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
    if(Mode&MODE_FLAG_MANUAL_HEATING){
      //display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      // Выбор стрелок или нагревания 
      if(State&STATUS_FLAG_CUR_MES){  
        display.drawBitmap(5, 20, epd_bitmap_up, 20, 36, WHITE); // заменить 36 на 40
        display.drawBitmap(110, 20, epd_bitmap_down, 20, 36, WHITE);        
      }
      else if(State&STATUS_FLAG_HEATING && (Curr_Temp > Measured_Temp )){
        display.drawBitmap(5, 20, epd_bitmap_heating_table, 22, 21, WHITE);// подобрать 22,21
      }
      DisplayTemp(30, 40, temperature); 
      DisplayPwr(110, 10, &valComputePID);
      display.display();
    }
    if(Mode&MODE_FLAG_PROG_HEATING){
      display.setFont(&FreeSerif12pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);    
      // Вывод меню на экран
      if(!(Mode&MODE_FLAG_SnPb || Mode&MODE_FLAG_PbFree)){
        for(selection_num=0; selection_num < 2; selection_num ++){
          if( prog[selection_num]->num_selections == Current_pos ){
            display.setCursor(prog[selection_num]->XPOS-14, prog[selection_num]->YPOS);
            display.print(">");
          }
          display.setCursor(prog[selection_num]->XPOS, prog[selection_num]->YPOS);
          display.print(prog[selection_num]->Str);
        }
      }
      else if(Mode&MODE_FLAG_SnPb){
        DisplayProg(tProg, temperature, &valComputePID);
      }
      else if(Mode&MODE_FLAG_PbFree){
        DisplayProg(tProg, temperature, &valComputePID);
      }
      display.display();
    }
    if(Mode&MODE_FLAG_TEST){
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
