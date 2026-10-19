#define RELAY_PIN 16

void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Реле выключено (активный LOW!)
  
  Serial.println("Реле готово");
}

void loop() {
  // Включить реле
  Serial.println("Реле ВКЛЮЧЕНО");
  digitalWrite(RELAY_PIN, LOW);
  delay(3000);
  
  // Выключить реле
  Serial.println("Реле ВЫКЛЮЧЕНО");
  digitalWrite(RELAY_PIN, HIGH);
  delay(3000);
}