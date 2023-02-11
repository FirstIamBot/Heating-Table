#include <Arduino.h>
// Load Wi-Fi library
#include "wifi.h"
#include "display.h"

#include <Wire.h>
#include "max6675.h"
#include <AiEsp32RotaryEncoder.h>
#include <RBDdimmer.h>
#include <PIDController.h>
#include "image.h"
#include "main.h"

//#define ARDUINO_ARCH_ESP32
#define __DEBUG__

//********************* User variables ******************************
//float Temperature, Curr_Temp, Measured_Temp;
int64_t Encpos;
int8_t Button;

//***********  mode reg (Режим роботы нагревательного стола ) **************************
//    7     6      5       4       3          2            1           0
// |     |     | PROG1 | PROG0 |SETTING|PROG_HEATING|MANUAL_HEATING|STANDBAY|
//
//********************************************************************
int8_t Mode;

//***********  status mode reg (Статус нагревательного стола ) ****************
//    7     6     5     4     3     2      1      0
// |     |     |     |     |     |     |CUR_MES|HEATING|
//
//********************************************************************
int8_t State;//

int8_t Current_pos = 0;
int8_t Select_pos = 0;
int8_t index_pos = 0;

double Temperature, Measured_Temp, Curr_Temp, valComputePID;
double coeffTempTable = 0; // температурный коэфициент нагревательного стола
unsigned long time1;
unsigned long time2;   

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

void DisplayMenu(void);
void ControlerMenu(void);
//********************************************************************
bool temp_loop_pntr(double *temperature);
void model_loop(void);

bool loop_GUI(double *temperature);

uint8_t rotary_EncoderButton(void);
void controler_loop(void);
//********************************************************************
//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
                    ROTARY_ENCODER_A_PIN, \
                    ROTARY_ENCODER_B_PIN, \
                    ROTARY_ENCODER_BUTTON_PIN, \
                    ROTARY_ENCODER_VCC_PIN, \
                    ROTARY_ENCODER_STEPS);
MAX6675 thermocouple(MAX6675_CLK, MAX6675_CS, MAX6675_DO);
dimmerLamp TableHeat(OUTPUT_PIN, ZEROCROSS); //initialase port for dimmer for ESP8266, ESP32, Arduino due boards
PIDController TableHeatPID; // Create an instance of the PID controller class, called "pid"
//***********************************************************************
void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}
//***********************************************************************
void setup() {
  Serial.begin(115200);
  initDisplay();
  DislayLogo();
  //*****************************    max6675    *************************
  Serial.println("MAX6675 init");
  // wait for MAX chip to stabilize
  delay(200);
  temp_loop_pntr(&Curr_Temp);// измерение температуры запись начального значения температуры
  //****************************    rotaryEncoder    *********************
  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  //rotaryEncoder.setup([]{rotaryEncoder.readEncoder_ISR();}); // установка прерываний для Энкодера
  //optionally we can set boundaries and if values should cycle or not
  bool circleValues = false;
  rotaryEncoder.setBoundaries(0, 400, circleValues);// Установка границы от текущей темрературы до максимальной
  rotaryEncoder.setAcceleration(150); //or set the value - larger number = more accelearation; 0 or 1 means disabled acceleration
  //rotaryEncoder.disableAcceleration(); //acceleration is now enabled by default - disable if you dont need it
  rotaryEncoder.setEncoderValue(25); //int(thermocouple.readCelsius()) init start value rotary encoder
  Serial.println("RotaryEncoder init");
  // ***************************    PID regulator    **************************
  TableHeatPID.begin();           // initialize the PID instance
  TableHeatPID.setpoint(Curr_Temp);// The "goal" the PID controller tries to "reach"
  TableHeatPID.tune(Kp, Kd, Ki);  // Tune the PID, arguments: kP, kI, kD
  TableHeatPID.limit(0, 100);    // Limit the PID output between 0 and 255, this is important to get rid of integral windup!
  Serial.println("PID regulator init");
  // ***************************    RBD dimmer    **************************
  TableHeat.begin(NORMAL_MODE, OFF); //dimmer initialisation: name.begin(MODE, STATE) TOGGLE_MODE
  //TableHeat.setPower(0);
  //delay(10);
  //TableHeat.setState(ON); //name.setState(ON/OFF);
  //TableHeat.toggleSettings(0, 100);
  Serial.println("RBD dimmer init");
  //***************************************
  Mode = 0 ;
  State= 0;
  initWIFI();
}
//***********************************************************************
void loop() {
  controler_loop();// управление режимами
  model_loop();
  // вывод на OLED
  loop_GUI(&Temperature); 
  // вывод на WiFI
  server.handleClient();        // manage HTTP requests
}
//########################################################################################
void controler_loop(void){

  uint8_t button; 
  static unsigned long lastTimeWait = 0;
  static unsigned long lastTimeProg;
  //*****************************************
  // Encoder rotary
  //*****************************************
  // действия на вращения енкодера в разных режимах
	if (rotaryEncoder.encoderChanged())
	{
    if(Mode&STANDBAY){
      State &= ~HEATING;
      return;
    }
    if(Mode&MANUAL_HEATING){
      State |= CUR_MES;      // Установка флага вывода выбранной(ручной) температуры 
      State &= ~HEATING;     // Выключение нагревателя
      Curr_Temp = rotaryEncoder.readEncoder();// чтение энкодера и запись в выбраную температуру
      TableHeatPID.setpoint(Curr_Temp);
    }
    if(Mode&PROG_HEATING){
      Current_pos++;
      if(Current_pos<=0){
        Current_pos=2;
      }
      else if(Current_pos>2){
        Current_pos=0;
      }
    }
    if(Mode & SETTING){
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
    State &= ~HEATING;     // Выключение нагревателя
    #ifdef __DEBUG__
      Serial.print(" button=");
      Serial.println(String(button, BIN));
      //Serial.print(" Mode=");
      //Serial.println(String(Mode, BIN));
    #endif
    // Выбор режимов роботы Heating Table
    // Короткое нажатие кнопки
    if(button & SHORT_PRESS){
      if(Mode == STANDBAY){
        Mode &= ~STANDBAY;
        Mode |= MANUAL_HEATING;
        #ifdef __DEBUG__
          Serial.println("Mode == MANUAL_HEATING");
        #endif
      }
      else if(Mode & MANUAL_HEATING){
        Mode &= ~MANUAL_HEATING;
        Mode |= PROG_HEATING;
        //TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == PROG_HEATING");
        #endif
      }
      else if((Mode & PROG_HEATING) && (Current_pos == 0)){
        Mode |= PROG0;
        Mode &= ~PROG1;
      }
      else if((Mode & PROG_HEATING) && (Current_pos == 1)){
        Mode |= PROG1;
        Mode &= ~PROG0;
      }
      // Выход из програмного режима 
      else if(Mode & PROG_HEATING && Current_pos == 2){
//*********************************************************
        Mode &= ~PROG_HEATING;
        Mode |= SETTING;
        Mode &= ~PROG1;
        Mode &= ~PROG0;
        Current_pos = 0;
//*********************************************************
        TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == SETTING");
        #endif
      }
      else if(Mode & SETTING){
        Mode &= ~SETTING;
        Mode |= STANDBAY;
        Current_pos = 0;
        TableHeat.setState(OFF); // State(ON/OFF);
        #ifdef __DEBUG__
          Serial.println("Mode == STANDBAY");
        #endif
      }
    }
    // Длинное нажатие кнопки
    if(button & LONG_PRESS){
        #ifdef __DEBUG__
          Serial.println("A detected is LONG press in controler_loop");
        #endif
        Mode &= ~PROG0;
        Mode &= ~PROG1;
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
  if((State&CUR_MES) && (millis()-lastTimeWait > GUI_TIME_DELAY)){
    State &= ~CUR_MES; // переключения на вывод измеренной температуры
    lastTimeWait = millis();
  } 
  if((Curr_Temp > (Measured_Temp + coeffTempTable)) && (Mode&MANUAL_HEATING || Mode&PROG_HEATING)){// Вкючение нагревателя
    State |= HEATING;  // Включение флага нагревателя
    TableHeat.setState(ON); // Включение нагревателя State(ON/OFF);
  }
  else{
    State &= ~HEATING;  // Включение флага нагревателя
    TableHeat.setState(OFF); // Выключение нагревателя State(ON/OFF);
  }
  //**** Установка Curr_temp в зависимости от времерни для режимов PROG0 и PROG1 *********
  if((Mode&PROG_HEATING) && Mode&PROG0){
    if(Prog0[0][0] > millis()-lastTimeProg){
      time1 = Prog0[0][0];
      time2 = millis()-lastTimeProg;      
      Curr_Temp = Prog0[0][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog0[0][0] < millis()-lastTimeProg < Prog0[1][0]){
      Curr_Temp = Prog0[1][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog0[1][0] < millis()-lastTimeProg < Prog0[2][0]){
      Curr_Temp = Prog0[2][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog0[2][0] < millis()-lastTimeProg < Prog0[3][0]){
      Curr_Temp = Prog0[3][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    lastTimeProg = millis();
  }
  if(((Mode&PROG_HEATING)) && (Mode&PROG1)){
    if(Prog1[0][0] > millis()-lastTimeProg ){
      Curr_Temp = Prog1[0][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog1[0][0] < millis()-lastTimeProg < Prog1[1][0]){
      Curr_Temp = Prog1[1][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog1[1][0] < millis()-lastTimeProg < Prog1[2][0]){
      Curr_Temp = Prog1[2][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog1[2][0] < millis()-lastTimeProg < Prog1[3][0]){
      Curr_Temp = Prog1[3][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    lastTimeProg = millis();
  }
}

//########################################################################################
bool temp_loop_pntr(double *temperature){
  static float lastTemp;
	static unsigned long lastTempUpdate = 0;
  
	// Переодическое измерение температуры
	if (millis() - lastTempUpdate > TEMP_READ_DELAY)
	{
    *temperature = double(thermocouple.readCelsius());
    lastTempUpdate = millis();
		return true;
	}
  return false;
}

void model_loop(void){

  temp_loop_pntr(&Measured_Temp);// измерение температуры
  if(State&CUR_MES) {//вывод на OLED температуры измереной или устанволеной
    Temperature = Curr_Temp;
  }
  else{
    Temperature = Measured_Temp;
  }
  //***********  Управление температурой PID контролером и Dimmer *****************
  if(Mode & MANUAL_HEATING){
    //if((Curr_Temp - Measured_Temp)<=20){
      //TableHeatPID.tune(1,1,1);
    //  TableHeatPID.minimize(100); // default = 10
    //}
    valComputePID = TableHeatPID.compute(Measured_Temp);   
  } 
  else if(Mode&PROG_HEATING && Mode&PROG0){
    valComputePID = TableHeatPID.compute(Measured_Temp);
  }
  else if(Mode&PROG_HEATING && Mode&PROG1){
    valComputePID = TableHeatPID.compute(Measured_Temp);
  }  
  else if(State&HEATING){ //
    #ifdef __DEBUG__
      Serial.print("Measured_Temp = ");
      Serial.println(String(Measured_Temp, DEC));
      Serial.print("Curr_Temp = ");
      Serial.println(String(Curr_Temp, DEC));
      Serial.print("valComputePID = ");
      Serial.println(String(int(valComputePID), DEC));
    #endif
    //delay(10);
    //TableHeat.setState(ON); //name.setState(ON/OFF);
    TableHeat.setPower(int(valComputePID));//50
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
    if(Mode == STANDBAY){
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
    }
    if(Mode & MANUAL_HEATING){
      //display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      // Выбор стрелок или нагревания 
      if(State&CUR_MES){  
        display.drawBitmap(5, 20, epd_bitmap_up, 20, 36, WHITE); // заменить 36 на 40
        display.drawBitmap(110, 20, epd_bitmap_down, 20, 36, WHITE);        
      }
      else if(State&HEATING && (Curr_Temp > Measured_Temp )){
        display.drawBitmap(5, 20, epd_bitmap_heating_table, 22, 21, WHITE);// подобрать 22,21
      }
      DisplayTemp(30, 40, temperature); 
      DisplayPwr(110, 10, &valComputePID);
    }
    if(Mode & PROG_HEATING){
      display.setFont(&FreeSerif12pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);    
      // Вывод меню на экран
      if(!(Mode&PROG0 || Mode&PROG1)){
        for(selection_num=0; selection_num < 2; selection_num ++){
          if( prog[selection_num]->num_selections == Current_pos ){
            display.setCursor(prog[selection_num]->XPOS-14, prog[selection_num]->YPOS);
            display.print(">");
          }
          display.setCursor(prog[selection_num]->XPOS, prog[selection_num]->YPOS);
          display.print(prog[selection_num]->Str);
        }
      }
      else if(Mode&PROG0){
        DisplayProg(20, temperature, &valComputePID);
      }
      else if(Mode&PROG1){
        DisplayProg(25, temperature, &valComputePID);
      }
      display.display();
    }
    if(Mode & SETTING){
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(20, 20);
      display.print("Setting");
      display.display();
      //DisplayTemp(10, 10, temperature); 
    }
    lastGUIUpdate = millis();
		return true;
	}
  return false;
 }

//########################################################################################
uint8_t rotary_EncoderButton(void){

    unsigned long  pressDuration;

    pressDuration = rotaryEncoder.isEncoderButtonType();

   //if( pressDuration < SHORT_PRESS_TIME ){
     // return NOT_PRESS;
   //}
   if( (SHORT_PRESS_TIME < pressDuration) && (LONG_PRESS_TIME > pressDuration ) ){
      #ifdef __DEBUG__
        Serial.println("A SHORT press is detected");
      #endif
      return SHORT_PRESS;
   }
   if( LONG_PRESS_TIME < pressDuration){
      #ifdef __DEBUG__
        Serial.println("A LONG_PRESS is detected");
      #endif
      return LONG_PRESS;      
    }
   return NOT_PRESS;
}

//#################### Menu  function View, Controler #############################
 
void DisplayMenu(double *temperature){

	static unsigned long lastGUIUpdate = 0;
  static char outstr[6];
  int selection_num;
    
  if (millis() - lastGUIUpdate > GUI_UPDATE_DELAY)
  {
    //display.clearDisplay();
    if(Mode == STANDBAY){
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
    }
    else if(Mode&MANUAL_HEATING){
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      if(State&CUR_MES){  // Выбор стрелок или нагревания 
        display.drawBitmap(5, 20, epd_bitmap_up, 20, 36, WHITE); 
        display.drawBitmap(110, 20, epd_bitmap_down, 20, 36, WHITE);        
      }
      else if(State&HEATING && (Measured_Temp - Curr_Temp < 3 )){ //вывод на OLED температуры измереной или устанволеной
        display.drawBitmap(5, 20, epd_bitmap_heating_table, 22, 21, WHITE);// подобрать 22,21
      }
      DisplayTemp(30, 40, temperature); 
    }
    // Вывод меню на экран в режиме PROG_HEATING
    if(Mode & PROG_HEATING){
        display.setFont(&FreeSerif12pt7b);
        display.setTextSize(1);
        display.setTextColor(WHITE);    
        // Вывод меню на экран
        if(!(Mode&PROG0 || Mode&PROG1)){
          for(selection_num=0; selection_num < 2; selection_num ++){
            if( prog[selection_num]->num_selections == Current_pos ){
              display.setCursor(prog[selection_num]->XPOS-14, prog[selection_num]->YPOS);
              display.print(">");
            }
            display.setCursor(prog[selection_num]->XPOS, prog[selection_num]->YPOS);
            display.print(prog[selection_num]->Str);
          }
        }
        else if(Mode&PROG0){
            display.clearDisplay();
            display.setCursor(30, 30);
            display.print("SnPb");
        }
        else if(Mode&PROG1){
            display.clearDisplay();
            display.setCursor(30, 30);
            display.print("Pb-free");
        }
    }
    display.display();
  }
}

void ControlerMenu(void){

    static unsigned long lastTimeWait = 0;
    uint8_t button;
    
    button = rotary_EncoderButton();

    if(button){
      if(button & SHORT_PRESS && Mode == STANDBAY){
        Mode &= ~STANDBAY;
        Mode |= MANUAL_HEATING;
      }
      else if(button&SHORT_PRESS && Mode&MANUAL_HEATING){
        Mode &= ~MANUAL_HEATING;
        Mode |= PROG_HEATING;
        //TableHeat.setState(OFF); // State(ON/OFF);
      }
      else if(button&SHORT_PRESS && Mode&PROG_HEATING ){
        Mode &= ~SETTING;
        Mode |= STANDBAY;
        Current_pos = 0;
        TableHeat.setState(OFF); // State(ON/OFF);
      }
      else if(button&SHORT_PRESS && Mode&PROG_HEATING && Current_pos == 0 ){
        Mode |= PROG0;
      }
      else if(button&SHORT_PRESS && Mode&PROG_HEATING && Current_pos ==1 ){
        Mode |= PROG1;
      }
      else if(button&LONG_PRESS && Mode&PROG_HEATING && Current_pos == 2 ){
        Mode &= ~PROG0;
        Mode &= ~PROG1;
      }
    }
    if(rotaryEncoder.encoderChanged()){
      if(Mode & PROG_HEATING && (Mode & PROG0 || Mode & PROG1)){
        if(0 <= Current_pos < 2){
          Current_pos++;
        }
        else if(Current_pos>=2){
          Current_pos=0;
        }
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
    if((State&CUR_MES) && (millis()-lastTimeWait > GUI_TIME_DELAY)){
      State &= ~CUR_MES; // переключения на вывод измеренной температуры
      State |= HEATING; // Включение нагревателя
      TableHeat.setState(ON); // State(ON/OFF);
      lastTimeWait = millis();
    }
    if(State&HEATING){ //Запись значения мощности(процентах) в RBDdimmer
    //delay(10);
    //TableHeat.setState(ON); //name.setState(ON/OFF);
    TableHeat.setPower(int(valComputePID));//50
    delay(100);
  } 

}

void ModelMenu(menu *Menu, int8_t *mode, int8_t *state, int8_t *cur_pos){

  static long lastTimeProg;

  temp_loop_pntr(&Measured_Temp);// измерение температуры
  if(State&CUR_MES) {//выбор температуры измереной или устанволеной для вывода на OLED 
    Temperature = Curr_Temp;
  }
  else{
    Temperature = Measured_Temp;
  }
  //***********  Управление температурой PID контролером и Dimmer *****************
  if(Mode&MANUAL_HEATING){
    valComputePID = TableHeatPID.compute(Measured_Temp);   
  } 
  //**** Установка Curr_temp в зависимости от времерни для режимов PROG0 и PROG1 *********
  if(((Mode&PROG_HEATING)) && (Mode&PROG0)){
    if(Prog0[0][0] > millis()-lastTimeProg ){
      Curr_Temp = Prog0[0][1];
    }
    else if(Prog0[0][0] < millis()-lastTimeProg < Prog0[1][0]){
      Curr_Temp = Prog0[1][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog0[1][0] < millis()-lastTimeProg < Prog0[2][0]){
      Curr_Temp = Prog0[2][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog0[2][0] < millis()-lastTimeProg < Prog0[3][0]){
      Curr_Temp = Prog0[3][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    lastTimeProg = millis();
    valComputePID = TableHeatPID.compute(Measured_Temp);  
  }
  if(((Mode&PROG_HEATING)) && (Mode&PROG1)){
    if(Prog1[0][0] > millis()-lastTimeProg ){
      Curr_Temp = Prog1[0][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog1[0][0] < millis()-lastTimeProg < Prog1[1][0]){
      Curr_Temp = Prog1[1][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog1[1][0] < millis()-lastTimeProg < Prog1[2][0]){
      Curr_Temp = Prog1[2][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    else if(Prog1[2][0] < millis()-lastTimeProg < Prog1[3][0]){
      Curr_Temp = Prog1[3][1];
      TableHeatPID.setpoint(Curr_Temp);
    }
    lastTimeProg = millis();
    valComputePID = TableHeatPID.compute(Measured_Temp);  
  }
}