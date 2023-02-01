#include <Arduino.h>
// Load Wi-Fi library
#include "wifi.h"
#include "display.h"

#include <Wire.h>
#include "max6675.h"
#include <AiEsp32RotaryEncoder.h>
#include <RBDdimmer.h>
#include <PIDController.h>
#include <PID_v1.h>
#include "image.h"
#include "main.h"

#define ARDUINO_ARCH_ESP32
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

menu prog[2][1]={
  {0, 1, "PROG 1", 25, 24, 0, (uint8_t*)Prog0},
  {1, 2, "PROG 2", 25, 48, 0, (uint8_t*)Prog1},
};

void DisplayMenu(menu *Menu);
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
//PIDController TableHeatPID; // Create an instance of the PID controller class, called "pid"
PID TableHeatPID(&Measured_Temp, &valComputePID, &Curr_Temp, Kp, Ki, Kd, DIRECT);
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
  rotaryEncoder.setEncoderValue(int(thermocouple.readCelsius())); // init start value rotary encoder
  Serial.println("RotaryEncoder init");
  // ***************************    PID regulator    **************************
  /* 
  TableHeatPID.begin();           // initialize the PID instance
  TableHeatPID.setpoint(Curr_Temp);// The "goal" the PID controller tries to "reach"
  TableHeatPID.tune(Kp, Kd, Ki);  // Tune the PID, arguments: kP, kI, kD
  TableHeatPID.limit(0, 100);    // Limit the PID output between 0 and 255, this is important to get rid of integral windup!
   */
  //set PID and update interval to 500ms 
  TableHeatPID.SetMode(AUTOMATIC);
  TableHeatPID.SetSampleTime(500);
  TableHeatPID.SetOutputLimits(0, 100);
  Serial.println("PID regulator init");
  // ***************************    RBD dimmer    **************************
  TableHeat.begin(NORMAL_MODE, ON); //dimmer initialisation: name.begin(MODE, STATE) 
  TableHeat.setPower(0);
  Serial.println("RBD dimmer init");
  //***************************************
  Mode =0 ;
  State= 0;
  initWIFI();
}
//***********************************************************************
void loop() {
  model_loop();
  controler_loop();// управление режимами
  // вывод на OLED
  loop_GUI(&Temperature); 
  // вывод на WiFI
  //loopWIFI();
  server.handleClient();        // manage HTTP requests
}
//########################################################################################
void controler_loop(void){

  uint8_t button; 
  static unsigned long lastTimePressed = 0;
  static unsigned long lastTimeWait = 0;
  unsigned long TimePressed;
  static unsigned long lastTimeRotary = 0; // переменная для формированния задержки перед сохранением
  //*****************************************
  // Encoder rotary
  //*****************************************
  // действия на вращения енкодера в разных режимах
	if (rotaryEncoder.encoderChanged())
	{
    if(Mode & STANDBAY){
      State &= ~HEATING;
      return;
    }
    if(Mode & MANUAL_HEATING){
      State |= CUR_MES;      // Установка флага вывода выбранной(ручной) температуры 
      State &= ~HEATING;     // Выключение нагревателя
      Curr_Temp = rotaryEncoder.readEncoder();// чтение энкодера и запись в выбраную температуру
      //TableHeatPID.setpoint(Curr_Temp);
    }
    if(Mode & PROG_HEATING){
      Current_pos = rotaryEncoder.encoderChanged();
      Serial.println(String(Current_pos));
//*********************************************************

//*********************************************************
    }
    if(Mode & SETTING){
      Current_pos = rotaryEncoder.encoderChanged();
      if (Current_pos>0) Serial.print("+");
      if (Current_pos<0) Serial.print("-"); 
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
        #ifdef __DEBUG__
          Serial.println("Mode == PROG_HEATING");
        #endif
      }
      else if(Mode & PROG_HEATING){
//*********************************************************

//*********************************************************
        Mode &= ~PROG_HEATING;
        Mode |= SETTING;
        #ifdef __DEBUG__
          Serial.println("Mode == SETTING");
        #endif
      }
      else if(Mode & SETTING){
        Mode &= ~SETTING;
        Mode |= STANDBAY;
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
    lastTimeWait = millis();
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
  //вывод на OLED температуры измереной или устанволеной
  if(State&CUR_MES){ 
    Temperature = Curr_Temp;
  }
  else{
    Temperature = Measured_Temp;
  }
  //***********  Управление температурой PID контролером и Dimmer *****************
  if(Mode&MANUAL_HEATING){
    //valComputePID = TableHeatPID.compute(Measured_Temp);
    TableHeatPID.Compute();
    #ifdef __DEBUG__
      Serial.println("MODEL ****** MANUAL_HEATING *** model_loop");
    #endif    
  } 
  else if(Mode&PROG_HEATING){ //
//*********************************************************

//*********************************************************
    /*
    #ifdef __DEBUG__
      Serial.print("MODEL ****** PROG_HEATING ");
    #endif    
    */
    //valComputePID = TableHeatPID.compute(Measured_Temp);
    TableHeatPID.Compute();
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
    TableHeat.setPower(int(valComputePID));
  } 
}

bool loop_GUI(double *temperature){

	static unsigned long lastGUIUpdate = 0;
  static char outstr[6];

	// Плавное отображение(вывод) изменения температуры
	if (millis() - lastGUIUpdate > GUI_UPDATE_DELAY)
	{
    display.clearDisplay();
    if(Mode == STANDBAY){
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(25, 55);
      display.print("Standbay");
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
      else if(State&HEATING && (Measured_Temp - Curr_Temp < 3 )){
        display.drawBitmap(5, 20, epd_bitmap_heating_table, 22, 21, WHITE);// подобрать 22,21
      }
      DisplayTemp(30, 40, temperature); 
    }
    if(Mode & PROG_HEATING){
      /*
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(25, 15);
      display.print("Prog_heating");     
      */
      DisplayMenu(*prog);
      //DisplayTemp(25, 55, temperature); 
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
 
void DisplayMenu(menu *Menu){
    int selection_num=0;
    
    display.setFont(&FreeSerif12pt7b);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    // Вывод меню на экран
    for(selection_num=0; selection_num <= Menu[selection_num].num_selections; selection_num ++){
      if(Current_pos == Menu[selection_num].num_selections){    // Вывод курсора на экран
        display.setCursor(Menu[selection_num].XPOS-14, Menu[selection_num].YPOS);
        display.print(">");
      }
      display.setCursor(Menu[selection_num].XPOS, Menu[selection_num].YPOS);
      display.print(Menu[selection_num].Str);
    }
    display.display();
}

void ControlerMenu(menu *Menu, int8_t *mode, int8_t *state, int8_t *cur_pos){
    Menu[Current_pos].fn_arg;
    Serial.println(Menu[0].fn_arg[0]);

}

void ModelMenu(menu *Menu, int8_t *mode, int8_t *state, int8_t *cur_pos){
    unsigned long ProgHeatingMillis = millis();

    if((millis() - ProgHeatingMillis) < 90){

    }
    if(90<(millis() - ProgHeatingMillis) < 180){
      
    }
    if(180<(millis() - ProgHeatingMillis) < 210){
      
    }
    if(210<(millis() - ProgHeatingMillis)){
      
    }

}