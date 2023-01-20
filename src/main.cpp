#include <Arduino.h>
// Load Wi-Fi library
#include "wifi.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
//#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Adafruit_SSD1306.h>
#include "max6675.h"
#include <AiEsp32RotaryEncoder.h>
#include <RBDdimmer.h>
#include <PID_v1.h>
//#include < AiEsp32RotaryEncoderNumberSelector.h>
#include "image.h"
#include "main.h"

#define ARDUINO_ARCH_ESP32
#define __DEBUG__

//********************* User variables ******************************
//float Temperature, Curr_Temp, Measured_Temp;
int64_t Encpos;
int8_t Button;
//***********  mode reg (Режим роботы нагревательного стола ) **************************
//    7     6      5       4       3       2              1          0
// |     |     |CUR_MES|       |SETTING|PROG_HEATING|MANUAL_HEATING|STANDBAY|
//
//********************************************************************
int8_t Mode;

//***********  status mode reg (Статус нагревательного стола ) ****************
//    7     6     5     4     3     2      1      0
// |     |     |     |     |     |     |      |HEATING|
//
//********************************************************************
int8_t State;//

int8_t Current_pos;
int8_t Select_pos;

double Temperature, Measured_Temp, Curr_Temp, SetPoint, OutputVal;
//********************************************************************
void DislayLogo(void);
uint8_t rotary_EncoderButton(void);

void DisplayTemp(int16_t x, int16_t y, double *temp);
bool temp_loop_pntr(double *temperature);
bool loop_GUI(double *temperature, uint32_t epcos);
void controler_loop(void);
//********************************************************************
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
                    ROTARY_ENCODER_A_PIN, \
                    ROTARY_ENCODER_B_PIN, \
                    ROTARY_ENCODER_BUTTON_PIN, \
                    ROTARY_ENCODER_VCC_PIN, \
                    ROTARY_ENCODER_STEPS);
MAX6675 thermocouple(MAX6675_CLK, MAX6675_CS, MAX6675_DO);
dimmerLamp TableHeat(OUTPUT_PIN, ZEROCROSS); //initialase port for dimmer for ESP8266, ESP32, Arduino due boards

PID TableHeatPID(&Measured_Temp, &OutputVal, &SetPoint, Kp, Ki, Kd, AUTOMATIC);
//***********************************************************************
void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}
//***********************************************************************
void setup() {
  Serial.begin(115200);
  // Start I2C Communication SDA = 5 and SCL = 4 on Wemos Lolin32 ESP32 with built-in SSD1306 OLED
  //*****************************    SSD1306 OLED    ********************
  Wire.begin(SDA, SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  Serial.println("SSD1306 init");
  DislayLogo();
  //*****************************    max6675    *************************
  Serial.println("MAX6675 init");
  // wait for MAX chip to stabilize
  delay(200);
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
  //set point 100% or 400 Celsium
  SetPoint = 400;
  TableHeatPID.SetOutputLimits(0, 100);
  //set PID update interval to 1000ms  
  TableHeatPID.SetSampleTime(1000);
  TableHeatPID.SetMode(AUTOMATIC);
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
  static char outstr[6];
  bool bComputePID;

  temp_loop_pntr(&Measured_Temp);// измерение температуры

  controler_loop();// управление режимами
  //вывод на OLED температуры измереной или устанволеной
  if(Mode&CUR_MES){ 
    //Serial.print(" Curr_Temp=");
    //Serial.println(String(Curr_Temp, DEC));
    Temperature = Curr_Temp;
  }
  else{
    Temperature = Measured_Temp;
  }
  //***********  Управление температурой PID контролером и Dimmer *****************
  if(State&HEATING){ //
    bComputePID = TableHeatPID.Compute();
    
    Serial.print(" OutputVal=");
    Serial.println(dtostrf(OutputVal, 6, 0, outstr));
    Serial.print(" x=");
    Serial.println(String(bComputePID, BIN));

    TableHeat.setPower(int(OutputVal));
  }    
  // вывод на OLED
  loop_GUI(&Temperature, Encpos); 
  // вывод на WiFI
  loopWIFI();
  
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
      Mode |= CUR_MES;       // Установка флага вывода выбранной(ручной) температуры 
      State &= ~HEATING;     // Выключение нагревателя
      Curr_Temp = rotaryEncoder.readEncoder();// чтение энкодера в выбраную температуру
    }
    if(Mode & PROG_HEATING){
      Current_pos = rotaryEncoder.readEncoder();
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
    //Serial.print(" button=");
    //Serial.println(String(button, BIN));
    //Serial.print(" Mode=");
    //Serial.println(String(Mode, BIN));
    // Выбор режимов роботы Heating Table
    // Короткое нажатие кнопки
    if(button & SHORT_PRESS){
      if(Mode == STANDBAY){
        Mode &= ~STANDBAY;
        Mode |= MANUAL_HEATING;
        Serial.println("Mode == MANUAL_HEATING");
      }
      else if(Mode & MANUAL_HEATING){
        Mode &= ~MANUAL_HEATING;
        Mode |= PROG_HEATING;
        Serial.println("Mode == PROG_HEATING");
      }
      else if(Mode & PROG_HEATING){
        Mode &= ~PROG_HEATING;
        Mode |= SETTING;
        Serial.println("Mode == SETTING");
      }
      else if(Mode & SETTING){
        Mode &= ~SETTING;
        Mode |= STANDBAY;
        Serial.println("Mode == STANDBAY");
      }
    }
    // Длинное нажатие кнопки
    if(button & LONG_PRESS){
        Serial.println("A detected is LONG press in controler_loop");
    }
  }
  // myflags |= option4; // включаем option4  
  // myflags &= ~option4; // выключаем option4
  // myflags ^= option4; // включаем или выключаем option4 
  // if (myflags & option4) ... // если option4 установлено - что-нибудь делаем
  //  if ((mask & value) != 0U)
  //  {
  //      value &= ~myflags; clear
  //  }
  //**** Сброс бита для вывода устанавливаемой температуры через 3 сек *********
  if((Mode&CUR_MES) && (millis()-lastTimeWait > GUI_TIME_DELAY)){
    Mode &= ~CUR_MES; // переключения на вывод измеренной температуры
    State |= HEATING; // Включение нагревателя
    lastTimeWait = millis();
  }
}

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
 
void DisplayTemp(int16_t x, int16_t y, double *temp){
  static char outstr[6];
  static unsigned long lastLCDUpdate;

	if (millis() - lastLCDUpdate > 500)
	{
    //display.clearDisplay();
    display.setFont(&FreeSerif12pt7b);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(x, y);
    display.print(dtostrf(*temp ,4, 2, outstr));
    //display.print(" C");
    display.display();
    lastLCDUpdate = millis();
  }
}

void DislayLogo(void){
        display.clearDisplay();
        display.setFont(&FreeSerif12pt7b);
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(20, 20);
        display.print("Heating");
        display.setCursor(30, 40);
        display.print("Table");
        display.display();
        delay(3000);
}

bool loop_GUI(double *temperature, uint32_t epcos){

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
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      // Выбор стрелок или нагревания 
      if(Mode&CUR_MES){  
        display.drawBitmap(5, 20, epd_bitmap_up, 20, 36, WHITE); // заменить 36 на 40
        display.drawBitmap(110, 20, epd_bitmap_down, 20, 36, WHITE);        
      }
      else if(State&HEATING){
        display.drawBitmap(5, 20, epd_bitmap_heating_table, 22, 21, WHITE);// подобрать 22,21
      }
      DisplayTemp(30, 40, temperature); 
    }
    if(Mode & PROG_HEATING){
      display.setFont(&FreeSerif9pt7b);
      display.setTextSize(1);
      display.setTextColor(WHITE);
      display.setCursor(25, 15);
      display.print("Prog_heating");

      DisplayTemp(25, 55, temperature); 
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

uint8_t rotary_EncoderButton(void){

    unsigned long  pressDuration;

    pressDuration = rotaryEncoder.isEncoderButtonType();

   //if( pressDuration < SHORT_PRESS_TIME ){
     // return NOT_PRESS;
   //}
   if( (SHORT_PRESS_TIME < pressDuration) && (LONG_PRESS_TIME > pressDuration ) ){
      //Serial.println("A SHORT press is detected");
      return SHORT_PRESS;
    }
    if( LONG_PRESS_TIME < pressDuration){
      //Serial.println("A detected is LONG press ");
      return LONG_PRESS;      
    }
    return NOT_PRESS;
}
