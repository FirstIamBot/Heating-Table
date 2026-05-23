

#include "main.h"

void initDisplay();
void DisplayTemp(int16_t x, int16_t y, double *temp);
void DisplayPwr(int16_t x, int16_t y, double *pwr);
unsigned char  DisplayTime(unsigned long *tm1, int *tm2);
void DisplayProg(int16_t tm ,double *tmp, double *vlCmpuPID);
void DislayLogo(void);


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void initDisplay(){
  // Start I2C Communication SDA = 5 and SCL = 4 on Wemos Lolin32 ESP32 with built-in SSD1306 OLED
  //*****************************    SSD1306 OLED    ********************
  Wire.begin(SDA, SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  Serial.println("SSD1306 init");
}
/*
  Вывод на OLED значение темперературы
*/

void DisplayTemp(int16_t x, int16_t y, double *temp){
  static char outstr[6];
  static unsigned long lastLCDUpdate;

	if (millis() - lastLCDUpdate > 500)
	{
    display.setFont(&FreeSerif12pt7b);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(x, y);
    display.print(dtostrf(*temp ,4, 2, outstr));
    //display.print(" C");
    //display.display();
    lastLCDUpdate = millis();
  }
  //display.display();
}

/*
  Вывод на OLED значение мощности
*/
void DisplayPwr(int16_t x, int16_t y, double *pwr){
  static unsigned long lastLCDUpdate;

	if (millis() - lastLCDUpdate > 500)
	{
    display.setFont(&Picopixel);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(x, y);
    display.print(String(int(*pwr)));
    display.print("%");
    lastLCDUpdate = millis();
  }
  //display.display();
}
/*
  Вывод на OLED  значение Time
*/
unsigned char  DisplayTime(unsigned long *tm1, int *tm2){
  static unsigned long lastLCDUpdate;
  unsigned char tm;
  char buf[3];
	if (millis() - lastLCDUpdate > 500)
	{
    display.setFont(&Picopixel);
    display.setTextSize(1);
    display.setTextColor(WHITE);

    sprintf(buf, "%l,u", tm1);

    display.print(buf);
    display.print("C");
    lastLCDUpdate = millis();
  }
  return tm;
}

void DisplayProg(int16_t tm ,double *tmp, double *vlCmpuPID){
    display.setFont(&FreeSerif12pt7b);
    display.setTextSize(1);
    display.setTextColor(WHITE); 
    display.setCursor(5, 20);
    display.print("t=");  
    display.setCursor(40, 20);
    display.print(tm); 
    display.setCursor(5, 45);
    display.print("T=");  
    DisplayTemp(40, 45, tmp);
    DisplayPwr(110, 10, vlCmpuPID);
}
//
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
