#include <AccelStepper.h>
#include <Arduino.h>

// Полупериодный режим (Half-Step) для плавности
// Пины: IN1, IN3, IN2, IN4 — именно в таком порядке для ULN2003
#define HALFSTEP 8

#define motorPin1  19  // IN1
#define motorPin2  18  // IN2
#define motorPin3  5   // IN3
#define motorPin4  17  // IN4

// Инициализация: режим, пины
AccelStepper stepper(HALFSTEP, motorPin1, motorPin3, motorPin2, motorPin4);

void setup() {
  Serial.begin(115200);
  
  stepper.setMaxSpeed(1000);      // Макс. скорость (шагов/сек)
  stepper.setAcceleration(500);   // Ускорение
  stepper.setSpeed(500);          // Рабочая скорость
  
  Serial.println("Шаговый двигатель готов");
}

void loop() {
  // Поворот на 1 оборот (28BYJ-48 в полушаге = 4096 шагов)
  stepper.moveTo(stepper.currentPosition() + 4096);
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  delay(500);
  
  // Поворот обратно
  stepper.moveTo(stepper.currentPosition() - 4096);
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
  delay(500);
}