#ifndef _MAIN_H_
#define _MAIN_H_

#include <Adafruit_GFX.h>
#include <Fonts/Picopixel.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Adafruit_SSD1306.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>

#include <WiFi.h>
// update from OTA
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

//*************************  MODE and State working HEAT_TABLE ******************************
// MODE FLAG's
#define FLAG_STANDBAY 0          // режим ожидания
#define FLAG_MANUAL_HEATING 1    // режим ручной установка температуры
#define FLAG_PROG_HEATING   2    // режим установки температуры по программе
#define FLAG_TEST     4          // настройка(Тест)
#define FLAG_SnPb    8          // SnPb      свинцовый припой
#define FLAG_PbFree   16          // Pb-free   безсвинцовый припой

// State FLAG's
#define FLAG_HEATING 1  // Флаг включеного нагревателя стола(для вывода на OLED, запись и вычесление PID)
#define FLAG_CUR_MES 2  // Флаг вывода температуры: 0 - измереная, 1 - установленая(ручная)
#define FLAG_Tracking 4  // Флаг влюченого режима отслеживания заданной температуры
//*********************************************
#define time 0
#define temper 1
#define pointA 0
#define pointB 1
#define pointC 2
#define pointD 3
//*********************************************
// State FLAG's Button Encoder
#define FLAG_NOT_PRESS   0   // кнопка не нажата
#define FLAG_SHORT_PRESS 1   // короткое нажатие кнопки
#define FLAG_LONG_PRESS  2   // длинное нажатие кнопки
// Time key pres mSec
#define SHORT_PRESS_TIME 300    // время короткого нажатия 0,5 сек
#define LONG_PRESS_TIME  3000   // время длинного нажатия 3 сек
//************************************** I2c Bus  ***********************************
#define SCL 4
#define SDA 5
//************************************** LCD  ***************************************
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define GUI_UPDATE_DELAY 500    // Задержка обновления 0,5 сек 
#define GUI_TIME_DELAY 3000     // Отображение на OLED 3 сек
//*******************************      SPI Bus for MAX6675     **********************
#define TEMP_READ_DELAY 1000
int MAX6675_DO  = 14; // голубой
int MAX6675_CS  = 12; // фиолетовый
int MAX6675_CLK = 13; // серый
//********************************     Rotary Encoder    ******************************
#define ROTARY_ENCODER_A_PIN      1  // CLK(А)     синий   25 
#define ROTARY_ENCODER_B_PIN      3  // DT(В)      зелёный 26
#define ROTARY_ENCODER_BUTTON_PIN 15  // SW(Button) фиолетовый
#define ROTARY_ENCODER_VCC_PIN   -1   //
//depending on your encoder - try 1,2 or 4 to get expected behaviour
//#define ROTARY_ENCODER_STEPS 1
//#define ROTARY_ENCODER_STEPS 2
#define ROTARY_ENCODER_STEPS 4
// ********************************** RBD dimmer ************************************
#define OUTPUT_PIN 25 
#define ZEROCROSS  26 
// ************************ PID controller  settings and gains **********************
//double Kp=15, Ki=50, Kd=300;
double Kp, Kd, Ki;
double KpTracking, KiTracking, KdTracking;
int twoZonePID;
int switchTemp;// 15
int unitProg;
int minimize;
int calibrateTemp = 1;

// SnPb profile: time boundaries, s
const int SNPB_T_PREHEAT_END  = 200;  // было 140
const int SNPB_T_REFLOW_END   = 400;  // было 240 — даём 200 сек на разгон
const int SNPB_T_PEAK_END     = 520;  // было 440
const int SNPB_T_COOL_END     = 680;  // было 600 — подбирается под реальный стол

// SnPb profile: target temperatures, C
const int SNPB_TEMP_PREHEAT = 140;
const int SNPB_TEMP_REFLOW = 235;
const int SNPB_TEMP_COOL = 150;

// Pb-free profile: time boundaries, s
const int PBFREE_T_PREHEAT_END = 240;
const int PBFREE_T_REFLOW_END = 340;
const int PBFREE_T_PEAK_END = 380;
const int PBFREE_T_COOL_END = 600;

// Pb-free profile: target temperatures, C
const int PBFREE_TEMP_PREHEAT = 140;
const int PBFREE_TEMP_REFLOW = 260;
const int PBFREE_TEMP_COOL = 150;
// ************************************************************************************* 
IPAddress IP;

// QuickPID API variables
extern float pidInput, pidOutput, pidSetpoint;

void setModeStandby(void);
void setModeManualHeating(void);
void setModeProgramSnPb(void);
void setModeProgramPbFree(void);
void startProgramByIndex(int index);
void setModeTest(void);

#endif // end 