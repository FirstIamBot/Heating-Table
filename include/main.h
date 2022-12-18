#ifndef _MAIN_H_
#define _MAIN_H_

//************************************** I2c Bus  ***********************************
#define SCL 4
#define SDA 5
//************************************** LCD  ***************************************
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
//*******************************      SPI Bus for MAX6675     **********************
int MAX6675_DO  = 14; // голубой
int MAX6675_CS  = 12; // фиолетовый
int MAX6675_CLK = 13; // серый
//********************************     Rotary Encoder    ******************************
#define ROTARY_ENCODER_A_PIN      15 // CLK(А)     синий
#define ROTARY_ENCODER_B_PIN      3  // DT(В)      зелёный 
#define ROTARY_ENCODER_BUTTON_PIN 1  // SW(Button) тёмно серый
#define ROTARY_ENCODER_VCC_PIN   -1   //
//depending on your encoder - try 1,2 or 4 to get expected behaviour
//#define ROTARY_ENCODER_STEPS 1
//#define ROTARY_ENCODER_STEPS 2
#define ROTARY_ENCODER_STEPS 4

#define ShortPress 0;
#define LongPress  1;
// ********************************** RBD dimmer ************************************
#define OUTPUT_PIN 4 //12 
#define ZEROCROSS  2 //7 // 5 for boards with CHANGEBLE input pins

#define TEMPHEATMAXVALUE 100
// ************************************************************************************* 

#endif // end 