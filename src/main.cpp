#include <Arduino.h>

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
//#include < AiEsp32RotaryEncoderNumberSelector.h>
#include "image.h"
#include "main.h"

//********************* User variables ******************************
float temp;
float Curr_temp;
float Measured_temp;

int64_t Encpos;
int8_t Button;

int8_t Current_pos;
int8_t Select_pos;

//********************************************************************
void DislayLogo(void);
float temp_loop(void);
void rotary_loop(void);
void rotary_onButtonClick(void);
void DisplayTempEncoder(int16_t x, int16_t y, int32_t EncPosition, float t);


//********************************************************************
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
                    ROTARY_ENCODER_A_PIN, \
                    ROTARY_ENCODER_B_PIN, \
                    ROTARY_ENCODER_BUTTON_PIN, \
                    ROTARY_ENCODER_VCC_PIN, \
                    ROTARY_ENCODER_STEPS);
MAX6675 thermocouple(MAX6675_CLK, MAX6675_CS, MAX6675_DO);
dimmerLamp tableHeat(OUTPUT_PIN, ZEROCROSS); //initialase port for dimmer for ESP8266, ESP32, Arduino due boards
//***********************************************************************
void setup() {
  Serial.begin(115200);
  // Start I2C Communication SDA = 5 and SCL = 4 on Wemos Lolin32 ESP32 with built-in SSD1306 OLED
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
  rotaryEncoder.setup([]{rotaryEncoder.readEncoder_ISR();}); // установка прерываний для Энкодера
  //optionally we can set boundaries and if values should cycle or not
  bool circleValues = false;
  rotaryEncoder.setBoundaries((int)thermocouple.readCelsius(), 400, circleValues);// Установка границы от текущей темрературы до максимальнй
  //rotaryEncoder.disableAcceleration(); //acceleration is now enabled by default - disable if you dont need it
  rotaryEncoder.setAcceleration(150); //or set the value - larger number = more accelearation; 0 or 1 means disabled acceleration
  Serial.println("rotaryEncoder init");
  // ***************************    RBD dimmer    **************************
  tableHeat.begin(NORMAL_MODE, ON); //dimmer initialisation: name.begin(MODE, STATE) 
  tableHeat.setPower(0);
  Serial.println("RBD dimmer init");
}
//***********************************************************************
void loop() {
  // put your main code here, to run repeatedly:
}


//########################################################################################
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

void rotary_loop(void)
{
	//dont print anything unless value changed
	if (rotaryEncoder.encoderChanged())
	{
    Encpos = rotaryEncoder.readEncoder();
	}
	if (rotaryEncoder.isEncoderButtonClicked())
	{
		rotary_onButtonClick();
	}
}

void rotary_onButtonClick(void)
{
	static unsigned long lastTimePressed = 0;
	//ignore multiple press in that time milliseconds
  if ((millis() - lastTimePressed) < 500 )
	{
		return;
	}
	lastTimePressed = millis();
}

float temp_loop(void){
  static float lastTemp;
	static unsigned long lastTimeTemp = 0;
  
	// Плавное отображение(вывод) изменения температуры
	if (millis() - lastTimeTemp < 150)
	{
		return lastTemp;
	}
	lastTimeTemp = millis();
  temp = thermocouple.readCelsius();
  lastTemp = temp;
  return temp;
}

void DisplayTempEncoder(int16_t x, int16_t y, int32_t EncPosition, float t){
  static char outstr[6];

  display.clearDisplay();
  //********************************************************
  display.setFont(&FreeMono9pt7b);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(x, y);
  display.print("Pos = ");
  display.print(String((int32_t)EncPosition));
  Serial.print("Pos = ");
  Serial.print(String((int32_t)EncPosition));
  //***********************************************************
  display.setFont(&FreeSerif12pt7b);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(x+30, y+30);
  display.print(dtostrf(t,4, 2, outstr));
  display.print(" C '");
  display.display();
 }