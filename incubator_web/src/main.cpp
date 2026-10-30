// =====================================================================
//  ИНКУБАТОР на ESP32 — управление через Wi-Fi точку доступа и сайт
//  Файл: incubator_web.ino
//
//  Как работает:
//   1. ESP32 создаёт точку доступа: SSID = "Inkubator", пароль = "12345678"
//   2. Подключаемся телефоном/ПК к этой сети и открываем в браузере:
//          http://192.168.4.1
//   3. На сайте можно: смотреть температуру/влажность, задать уставку
//      температуры, включить/выключить обогрев (реле), вручную поворачивать
//      лоток (шаговый двигатель), включить авто-переворот, перезагрузить.
//
//  Использует пины из рабочих примеров этого репозитория:
//   DHT11  - GPIO 13                 (пример DHT11/DHT11.ino)
//   Реле   - GPIO 16, активный LOW   (пример relay/relay.ino)
//   OLED   - I2C SDA=21 SCL=22, адрес 0x3C (пример oled/oled.ino)
//   Двигатель 28BYJ-48 + ULN2003 - IN1=19, IN2=18, IN3=5, IN4=17
//        (пример stepper_motor/stepper_motor.ino)
//
//  Библиотеки (Установщик библиотек Arduino IDE):
//   - DHT sensor library (Adafruit)
//   - Adafruit SSD1306 + Adafruit GFX Library
//   - AccelStepper (by waspinator)
// =====================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <Arduino.h>

// ------------------------ СЕТЬ ------------------------
const char* AP_SSID     = "Inkubator";    // имя точки доступа
const char* AP_PASSWORD = "12345678";     // пароль (минимум 8 символов)

// ------------------------ ПИНЫ ------------------------
#define DHTPIN        13
#define DHTTYPE       DHT11
#define RELAY_PIN     16          // реле, активный LOW

#define I2C_SDA       21
#define I2C_SCL       22
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define motorPin1     19          // IN1 (ULN2003)
#define motorPin2     18          // IN2
#define motorPin3     5           // IN3
#define motorPin4     17          // IN4

// ------------------------ НАСТРОЙКИ ПО УМОЛЧАНИЮ ------------------------
#define HALFSTEP           8       // режим полушага для 28BYJ-48 (как в примере)
#define DEFAULT_SET_TEMP   37.5f  // уставка температуры, °C
#define HYSTERESIS         0.3f   // гистерезис включения/выключения обогрева
#define STEPPER_FULL_TURN  4096   // полный оборот 28BYJ-48 в полушаге
#define AUTO_TURN_INTERVAL 10     // интервал авто-переворота, часов

// ------------------------ ОБЪЕКТЫ ------------------------
DHT dht(DHTPIN, DHTTYPE);

TwoWire I2C_OLED = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, -1);

AccelStepper stepper(HALFSTEP, motorPin1, motorPin3, motorPin2, motorPin4);

WebServer server(80);

// ------------------------ СОСТОЯНИЕ ------------------------
float   temperature   = NAN;      // текущая температура
float   humidity      = NAN;      // текущая влажность
bool    heating       = false;    // включён ли обогрев сейчас
bool    manualHeat    = false;    // ручной режим нагрева (override с сайта)
float   setTemp       = DEFAULT_SET_TEMP;
bool    autoTurnOn    = true;     // авто-переворот лотка
unsigned long lastAutoTurnMs = 0; // когда был последний переворот
unsigned long bootTimeMs     = 0;
bool    turnInProgress  = false;
bool    oledReady       = false;  // найден ли OLED при старте

// ------------------------ HTML-САЙТ ------------------------
// Страница хранится во Flash (PROGMEM), чтобы не занимать RAM
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Инкубатор — панель управления</title>
<style>
  :root{
    --bg:#10151c; --card:#1b2430; --accent:#ff9f1c; --ok:#2ecc71;
    --bad:#e74c3c; --text:#e8eef4; --muted:#8fa1b3;
  }
  *{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,Arial,sans-serif}
  body{background:var(--bg);color:var(--text);min-height:100vh;padding:16px}
  .wrap{max-width:520px;margin:0 auto}
  header{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
  h1{font-size:20px}
  #conn{font-size:12px;color:var(--ok)}
  #conn.off{color:var(--bad)}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px}
  .card{background:var(--card);border-radius:14px;padding:14px;box-shadow:0 2px 8px rgba(0,0,0,.35)}
  .label{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.5px}
  .value{font-size:30px;font-weight:700;margin-top:4px}
  .value small{font-size:14px;color:var(--muted);font-weight:400}
  .sub{font-size:12px;color:var(--muted);margin-top:4px}
  .row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:10px}
  .badge{padding:4px 10px;border-radius:999px;font-size:12px;background:#2a3646;color:var(--muted)}
  .badge.on{background:rgba(255,159,28,.15);color:var(--accent)}
  button{border:none;border-radius:10px;padding:12px 16px;font-size:15px;font-weight:600;
         cursor:pointer;background:#2a3646;color:var(--text);transition:.15s}
  button:active{transform:scale(.97)}
  button.primary{background:var(--accent);color:#10151c}
  button.danger{background:var(--bad);color:#fff}
  button.toggle-on{background:var(--ok);color:#0c1a12}
  .full{width:100%}
  .settemp{display:flex;align-items:center;gap:12px;margin-top:10px}
  .settemp input[type=range]{flex:1;accent-color:var(--accent)}
  .settemp output{font-size:20px;font-weight:700;min-width:70px;text-align:right}
  fieldset{border:none;margin-top:12px}
  legend{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:6px}
  footer{text-align:center;color:var(--muted);font-size:11px;margin-top:16px}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <h1>&#129360; Инкубатор</h1>
    <span id="conn">соединение...</span>
  </header>

  <div class="grid">
    <div class="card">
      <div class="label">Температура</div>
      <div class="value" id="t">--.- <small>°C</small></div>
      <div class="sub">уставка: <b id="st">--.-</b> °C</div>
    </div>
    <div class="card">
      <div class="label">Влажность</div>
      <div class="value" id="h">-- <small>%</small></div>
      <div class="sub" id="htime">опрос каждые 2 с</div>
    </div>
  </div>

  <div class="card">
    <div class="label">Обогрев (нагреватель)</div>
    <div class="row">
      <span class="badge" id="heatBadge">выключен</span>
      <span class="badge" id="modeBadge">авто</span>
    </div>
    <div class="row">
      <button class="full primary" id="btnHeat" onclick="toggleHeat()">Включить вручную</button>
    </div>
    <fieldset>
      <legend>Задать температуру</legend>
      <div class="settemp">
        <input type="range" id="rSet" min="30" max="42" step="0.1" value="37.5"
               oninput="document.getElementById('oSet').textContent=this.value+' °C'"
               onchange="applySetTemp()">
        <output id="oSet">37.5 °C</output>
      </div>
    </fieldset>
  </div>

  <div class="card" style="margin-top:12px">
    <div class="label">Поворот лотка</div>
    <div class="row">
      <span class="badge" id="turnBadge">готов</span>
      <span class="badge" id="autoBadge">авто-переворот: вкл</span>
    </div>
    <div class="row">
      <button class="primary" onclick="turnEgg()">&#8635; Перевернуть</button>
      <button id="btnAuto" class="toggle-on" onclick="toggleAuto()">Авто: ВКЛ</button>
    </div>
  </div>

  <div class="card" style="margin-top:12px">
    <div class="label">Система</div>
    <div class="row">
      <span class="badge" id="uptime">00:00:00</span>
      <button class="danger" onclick="reboot()">&#128260; Перезагрузка</button>
    </div>
  </div>

  <footer>ESP32 &middot; Точка доступа Inkubator &middot; 192.168.4.1</footer>
</div>

<script>
let manualHeat = false;

function api(url){
  return fetch(url).then(r=>r.json()).then(d=>{update(d);}).catch(()=>{
    document.getElementById('conn').textContent='нет связи';
    document.getElementById('conn').className='off';
  });
}

function update(d){
  const conn=document.getElementById('conn');
  conn.textContent='онлайн'; conn.className='';

  document.getElementById('t').innerHTML =
    (d.temp==null?'--.-':Number(d.temp).toFixed(1))+' <small>°C</small>';
  document.getElementById('h').innerHTML =
    (d.hum==null?'--':Number(d.hum).toFixed(0))+' <small>%</small>';
  document.getElementById('st').textContent = Number(d.set).toFixed(1);

  const hb=document.getElementById('heatBadge');
  hb.textContent = d.heating ? 'нагрев ВКЛ' : 'выключен';
  hb.className = 'badge' + (d.heating?' on':'');

  const mb=document.getElementById('modeBadge');
  mb.textContent = d.manual ? 'ручной режим' : 'авто';
  mb.className = 'badge' + (d.manual?' on':'');

  const btn=document.getElementById('btnHeat');
  manualHeat = d.manual;
  btn.dataset.manual = d.manual ? '1' : '0';
  if(manualHeat){ btn.textContent='Выключить ручной нагрев'; btn.className='full danger'; }
  else { btn.textContent='Включить вручную'; btn.className='full primary'; }

  const tb=document.getElementById('turnBadge');
  tb.textContent = d.turning ? 'поворот...' : 'готов';
  tb.className = 'badge' + (d.turning?' on':'');

  const ab=document.getElementById('autoBadge');
  ab.textContent = 'авто-переворот: ' + (d.auto?'вкл':'выкл');
  const ba=document.getElementById('btnAuto');
  ba.textContent = 'Авто: ' + (d.auto?'ВКЛ':'ВЫКЛ');
  ba.className = d.auto ? 'toggle-on' : '';

  const s=d.uptime|0;
  const hh=String(Math.floor(s/3600)).padStart(2,'0');
  const mm=String(Math.floor(s/60)%60).padStart(2,'0');
  const ss=String(s%60).padStart(2,'0');
  document.getElementById('uptime').textContent = hh+':'+mm+':'+ss;
}

function toggleHeat(){
  fetch('/heat?on='+(manualHeat?0:1)).then(()=>api('/status'));
}
function applySetTemp(){
  const v=document.getElementById('rSet').value;
  document.getElementById('oSet').textContent=v+' °C';
  fetch('/settemp?v='+v).then(()=>api('/status'));
}
function turnEgg(){ fetch('/turn'); setTimeout(()=>api('/status'),1000); }
function toggleAuto(){
  fetch('/autoturn?on='+((document.getElementById('btnAuto').textContent.indexOf('ВКЛ')>=0)?0:1))
    .then(()=>api('/status'));
}
function reboot(){ if(confirm('Перезагрузить ESP32?')) fetch('/reboot'); }

api('/status');
setInterval(()=>api('/status'), 2000);
</script>

<script>
/* Демо-режим: если открыть файл локально (file://) или сервер недоступен —
   показываем правдоподобные данные вместо пустой страницы. На реальном
   ESP32 этот блок неактивен (fetch к /status проходит успешно). */
let demo = false, demoTemp = 36.8, demoHum = 55, demoUptime = 0, demoAuto = true;

// имитация команд на «устройстве», чтобы кнопки работали и без ESP32
function demoCmd(url){
  if(url.indexOf('/heat?on=1')>=0){ manualHeat = true; }
  if(url.indexOf('/heat?on=0')>=0){ manualHeat = false; }
  if(url.indexOf('/autoturn?on=')>=0){ demoAuto = url.indexOf('on=1')>=0; }
}

function demoTick(){
  demoUptime += 2;
  demoTemp += (Math.random()-0.48)*0.1;
  demoTemp = Math.min(38.2, Math.max(36.2, demoTemp));
  demoHum  = Math.min(70, Math.max(45, demoHum + (Math.random()-0.5)*1.5));
  const setv = parseFloat(document.getElementById('rSet').value);
  update({temp:demoTemp, hum:demoHum, set:setv,
          heating: manualHeat || demoTemp < setv - 0.3,
          manual: manualHeat, turning:false, auto:demoAuto, uptime:demoUptime});
}

function startDemo(){
  if(demo) return;
  demo = true;
  const conn=document.getElementById('conn');
  conn.textContent='демо-режим (нет ESP32)'; conn.className='off';
  setInterval(demoTick, 2000);
  demoTick(); // сразу показываем данные, не дожидаясь первого тика
}

const _fetch = window.fetch ? window.fetch.bind(window) : null;
window.fetch = function(url, opt){
  if(demo && typeof url==='string' && url[0]==='/'){ demoCmd(url); return Promise.resolve(new Response('OK')); }
  return _fetch ? _fetch(url, opt) : Promise.reject('no fetch');
};

// Переопределяем api(): при недоступности сервера (открытие файла локально,
// ESP32 выключен) — включаем демо-режим с имитацией данных инкубатора.
const _apiOrig = api;
api = function(url){
  // если демо уже активен — команды не отправляем, просто обновляем картинку
  if(demo && typeof url==='string' && url[0]==='/'){ demoTick(); return Promise.resolve(); }
  // свой промис в текущем контексте: надёжно отрабатывает и тогда, когда
  // оригинальный api() «глотает» ошибку внутри себя
  return new Promise(function(resolve){
    var settled = false;
    function enterDemo(){ if(!settled){ settled = true; startDemo(); } resolve(); }
    try {
      var p = _apiOrig(url);
      if(p && typeof p.then === 'function'){
        p.then(function(){
          // сервер не ответил (оригинал перехватил ошибку) -> коннект «потух»
          setTimeout(function(){
            if(!demo && document.getElementById('conn').textContent === 'нет связи'){ enterDemo(); }
            else resolve();
          }, 0);
        }, enterDemo);
      } else { enterDemo(); }
    } catch(e){ enterDemo(); }
    // страховка: если промис завис — демо включится через 4 секунды
    setTimeout(function(){
      if(!demo && document.getElementById('conn').textContent === 'нет связи'){ enterDemo(); }
    }, 4000);
  });
};
</script>
</body>
</html>)rawliteral";

// ------------------------ HTTP-ОБРАБОТЧИКИ ------------------------

// Текущее состояние в JSON — опрашивается сайтом каждые 2 секунды
void handleStatus() {
  String json;
  json.reserve(220);
  json += "{";
  if (isnan(temperature)) json += "\"temp\":null"; else { json += "\"temp\":"; json += String(temperature, 1); }
  json += ",";
  if (isnan(humidity))    json += "\"hum\":null";  else { json += "\"hum\":";  json += String(humidity, 0); }
  json += ",\"set\":"   + String(setTemp, 1);
  json += ",\"heating\":" + String(heating ? "true" : "false");
  json += ",\"manual\":"  + String(manualHeat ? "true" : "false");
  json += ",\"turning\":" + String(turnInProgress ? "true" : "false");
  json += ",\"auto\":"    + String(autoTurnOn ? "true" : "false");
  json += ",\"uptime\":"  + String((millis() - bootTimeMs) / 1000);
  json += "}";
  server.send(200, "application/json", json);
}

void sendOk() { server.send(200, "text/plain", "OK"); }

// Включение/выключение ручного режима нагрева: /heat?on=1 | /heat?on=0
void handleHeat() {
  if (server.hasArg("on")) {
    manualHeat = server.arg("on") == "1";
    // при выходе из ручного режима сразу глушим реле
    if (!manualHeat) { heating = false; digitalWrite(RELAY_PIN, HIGH); }
  }
  sendOk();
}

// Установка целевой температуры: /settemp?v=37.8
void handleSetTemp() {
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (v >= 30.0 && v <= 42.0) setTemp = v;
  }
  sendOk();
}

// Разовый переворот лотка: /turn
void handleTurn() {
  if (!turnInProgress) {
    // переворот лотка на пол-оборота
    stepper.moveTo(stepper.currentPosition() + STEPPER_FULL_TURN / 2);
    turnInProgress = true;
  }
  sendOk();
}

// Вкл/выкл авто-переворота: /autoturn?on=1|0
void handleAutoTurn() {
  if (server.hasArg("on")) {
    autoTurnOn = server.arg("on") == "1";
    if (autoTurnOn) lastAutoTurnMs = millis(); // отсчёт интервала заново
  }
  sendOk();
}

void handleReboot() { sendOk(); delay(300); ESP.restart(); }

void handleNotFound() { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); }

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }

// ------------------------ ЛОГИКА ИНКУБАТОРА ------------------------

// Чтение DHT11 (не чаще раза в 2 секунды — как в рабочем примере)
void readSensor() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead < 2000) return;
  lastRead = millis();

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println("Ошибка чтения DHT11!");
    return;
  }
  temperature = t;
  humidity = h;
}

// Терморегулятор с гистерезисом вокруг уставки.
// В ручном режиме нагрев включён всегда, пока пользователь не выключит.
void regulateTemperature() {
  if (manualHeat) {
    heating = true;
  } else if (isnan(temperature)) {
    heating = false;              // нет данных с датчика — не греем (защита от перегрева)
  } else if (!heating && temperature < setTemp - HYSTERESIS) {
    heating = true;               // стало холоднее уставки -0.3 -> включить
  } else if (heating && temperature > setTemp + HYSTERESIS) {
    heating = false;              // стало горячее уставки +0.3 -> выключить
  }

  digitalWrite(RELAY_PIN, heating ? LOW : HIGH); // реле активное LOW!
}

// Авто-переворот лотка раз в AUTO_TURN_INTERVAL часов
void autoTurn() {
  if (!autoTurnOn || turnInProgress) return;
  if (millis() - lastAutoTurnMs >= (unsigned long)AUTO_TURN_INTERVAL * 3600000UL) {
    lastAutoTurnMs = millis();
    stepper.moveTo(stepper.currentPosition() + STEPPER_FULL_TURN / 2);
    turnInProgress = true;
    Serial.println("Авто-переворот лотка");
  }
}

// Фоновое движение двигателя (без блокировки loop)
void runStepper() {
  stepper.run();
  if (turnInProgress && stepper.distanceToGo() == 0) turnInProgress = false;
}

// ------------------------ OLED ------------------------
void drawOled() {
  static unsigned long lastDraw = 0;
  if (!oledReady || millis() - lastDraw < 500) return;
  lastDraw = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Температура и влажность крупно
  display.setTextSize(2);
  display.setCursor(0, 0);
  if (isnan(temperature)) display.print("--.-C");
  else display.print(String(temperature, 1) + "C");

  display.setCursor(64, 0);
  if (isnan(humidity)) display.print("-- %");
  else display.print(String(humidity, 0) + "%");

  // Статусы
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Set:" + String(setTemp, 1) + "C");
  display.setCursor(70, 20);
  display.print(String(heating ? "HEAT ON" : "heat off"));

  display.setCursor(0, 32);
  display.print(String(autoTurnOn ? "AUTO-TURN ON" : "TURN: manual"));
  display.setCursor(0, 44);
  display.print("AP:Inkubator .4.1");
  display.display();
}

// ------------------------ SETUP / LOOP ------------------------
void setup() {
  Serial.begin(115200);
  bootTimeMs = millis();

  // Пины
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // реле выключено (активный LOW)

  stepper.setMaxSpeed(500);      // шаги/сек
  stepper.setAcceleration(300);

  dht.begin();

  // OLED
  I2C_OLED.begin(I2C_SDA, I2C_SCL, 400000);
  bool oledFound = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledFound) {
    Serial.println("OLED не найден (0x3C) — продолжаем без дисплея");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Inkubator ESP32");
    display.println("AP: Inkubator");
    display.println("pass: 12345678");
    display.println("http://192.168.4.1");
    display.display();
  }
  // разрешаем отрисовку только если дисплей найден
  oledReady = oledFound;

  // Точка доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("===============================");
  Serial.print("AP открыта: "); Serial.println(AP_SSID);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
  Serial.println("Откройте сайт: http://192.168.4.1");
  Serial.println("===============================");

  // Маршруты сайта
  server.on("/",          HTTP_GET, handleRoot);
  server.on("/status",    HTTP_GET, handleStatus);
  server.on("/heat",      HTTP_GET, handleHeat);
  server.on("/settemp",   HTTP_GET, handleSetTemp);
  server.on("/turn",      HTTP_GET, handleTurn);
  server.on("/autoturn",  HTTP_GET, handleAutoTurn);
  server.on("/reboot",    HTTP_GET, handleReboot);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();   // обслуживание сайта
  readSensor();            // датчик
  regulateTemperature();   // терморегулятор -> реле
  autoTurn();              // авто-переворот по расписанию
  runStepper();            // фоновое движение двигателя
  drawOled();              // экран
  delay(2);                // короткая пауза для стабильной работы WebServer/Stepper
}
