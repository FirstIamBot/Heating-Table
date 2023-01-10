#ifndef _MAIN_H_
#define _MAIN_H_
//*************************  MODE and State working HEAT_TABLE ******************************
// MODE FLAG's
#define STANDBAY 0
#define MANUAL_HEATING 1
#define PROG_HEATING   2
#define SETTING  4
#define HEATING 8
#define CUR_MES 16
// State FLAG's
#define CUR_MES_ON  1 // вкл Вывода выбранной(ручной) температуры 
#define CUR_MES_OFF 0 // выкл Вывода выбранной(ручной) температуры
#define HEATING_ON  1
#define HEATING_OFF 0
// State FLAG's Button Encoder
#define NOT_PRESS   0
#define SHORT_PRESS 1
#define LONG_PRESS  2
// Time key pres mSec
#define SHORT_PRESS_TIME 500
#define LONG_PRESS_TIME  3000
//************************************** I2c Bus  ***********************************
#define SCL 4
#define SDA 5
//************************************** LCD  ***************************************
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define GUI_UPDATE_DELAY 500
#define GUI_TIME_DELAY 3000 // вывод установленой ручной температуры 3 сек
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