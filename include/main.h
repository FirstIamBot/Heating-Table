#ifndef _MAIN_H_
#define _MAIN_H_
//*************************  MODE and State working HEAT_TABLE ******************************
// MODE FLAG's
#define STANDBAY 0          // режим ожидания
#define MANUAL_HEATING 1    // режим ручной установка температуры
#define PROG_HEATING   2    // режим установки температуры по программе
#define SETTING  4          // настройка

#define CUR_MES 16  // Флаг вывода температуры: 0 - измереная, 1 - установленая(ручная)
// State FLAG's
#define HEATING 1   // включение нагрева стола

//#define HEATING_ON  1
//#define HEATING_OFF 0
// State FLAG's Button Encoder
#define NOT_PRESS   0   // кнопка не нажата
#define SHORT_PRESS 1   // короткое нажатие кнопки
#define LONG_PRESS  2   // длинное нажатие кнопки
// Time key pres mSec
#define SHORT_PRESS_TIME 500    // время короткого нажатия 0,5 сек
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
#define ROTARY_ENCODER_A_PIN      3  // CLK(А)     синий   25 
#define ROTARY_ENCODER_B_PIN      1  // DT(В)      зелёный 26
#define ROTARY_ENCODER_BUTTON_PIN 15  // SW(Button) фиолетовый
#define ROTARY_ENCODER_VCC_PIN   -1   //
//depending on your encoder - try 1,2 or 4 to get expected behaviour
//#define ROTARY_ENCODER_STEPS 1
//#define ROTARY_ENCODER_STEPS 2
#define ROTARY_ENCODER_STEPS 4

// ********************************** RBD dimmer ************************************
#define OUTPUT_PIN 4 //12 
#define ZEROCROSS  2 //7 // 5 for boards with CHANGEBLE input pins

#define TEMPHEATMAXVALUE 100
// ************************ PID controller  settings and gains **********************
double Kp=2, Ki=5, Kd=1;
// ************************************************************************************* 

#endif // end 