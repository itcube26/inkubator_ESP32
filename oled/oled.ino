#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Явно указываем пины SDA (21) и SCL (22) для ESP32
#define I2C_SDA 21
#define I2C_SCL 22

// Создаем объект TwoWire для явного управления
TwoWire I2C_OLED = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, -1);

void setup() {
  Serial.begin(115200);
  
  // Инициализируем I2C на конкретных пинах
  I2C_OLED.begin(I2C_SDA, I2C_SCL, 400000); // 400 кГц для быстрой работы
  
  // Попробуем инициализировать дисплей (если не выйдет, сканер выше должен был показать другой адрес)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("Ошибка: не удалось инициализировать SSD1306 по адресу 0x3C");
    Serial.println("Запустите I2C-сканер, чтобы проверить адрес и подключение.");
    for(;;);
  }
  
  Serial.println("Дисплей успешно инициализирован!");
  delay(1000);
  
  display.cp437(true);
  display.clearDisplay();
  display.setTextSize(1);             
  display.setTextColor(SSD1306_WHITE);        
  display.setCursor(0, 20);             
  display.println("Hello ESP32 & OLED!");
  display.display();
  delay(2000); 
  
  // Пример отрисовки
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Filled Round Rect");
  display.fillRoundRect(0, 15, 60, 40, 8, SSD1306_WHITE);
  display.display();
  delay(2000);
  
  display.clearDisplay();
}

void loop() {
  // Ваш код для loop
}
