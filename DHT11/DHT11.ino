#include <DHT.h>

#define DHTPIN 13
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  dht.begin();
}

void loop() {
  delay(2000); // Важно! Не чаще раза в 2 секунды
  
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Ошибка чтения!");
    return;
  }

  Serial.print("Температура: ");
  Serial.print(temperature);
  Serial.print(" °C | Влажность: ");
  Serial.print(humidity);
  Serial.println(" %");
}