#ifndef _MAIN_H_
#define _MAIN_H_

#include <Adafruit_GFX.h>
#include <Fonts/Picopixel.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <WebServer.h>        // include ESP32 library
#include "webpage/index.h"
#include "webpage/dbg.h"

// update from OTA



//*************************  MODE and State working HEAT_TABLE ******************************
// MODE FLAG's
#define STANDBAY 0          // режим ожидания
#define MANUAL_HEATING 1    // режим ручной установка температуры
#define PROG_HEATING   2    // режим установки температуры по программе
#define SETTING  4          // настройка
#define PROG0    8          // SnPb      свинцовый припой
#define PROG1   16          // Pb-free   безсвинцовый припой

// State FLAG's
#define HEATING 1  // Флаг включеного нагревателя стола(для вывода на OLED, запись и вычесление PID)
#define CUR_MES 2  // Флаг вывода температуры: 0 - измереная, 1 - установленая(ручная)
#define Entrer  4

//#define HEATING_ON  1
//#define HEATING_OFF 0
// State FLAG's Button Encoder
#define NOT_PRESS   0   // кнопка не нажата
#define SHORT_PRESS 1   // короткое нажатие кнопки
#define LONG_PRESS  2   // длинное нажатие кнопки
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
double Kp=10, Ki=3, Kd=3;
//double Kp=6, Ki=2, Kd=2;
// ************************************************************************************* 
IPAddress IP;


#endif // end 