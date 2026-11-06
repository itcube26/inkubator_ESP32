// =====================================================================
//  ИНКУБАТОР НА ESP32 (v2.0) — Профессиональная автоматика
//  Файл: incubator_web.ino
//
//  Основные возможности v2.0:
//   1. Терморегулятор с гистерезисом + аварийная защита от перегрева (>39.5°C)
//   2. Энергонезависимая память (NVS Preferences): уставка, интервалы и
//      калибровка не сбрасываются при перезагрузке!
//   3. Устранение перегрева двигателя 28BYJ-48 + ULN2003: обмотки
//      автоматически обесточиваются после завершения поворота!
//   4. Реальный маятниковый наклон лотка (влево 45° <-> вправо 45°)
//      вместо бесконечного закручивания проводов в одну сторону.
//   5. Калибровка датчиков температуры и влажности (offset).
//   6. Календарь инкубации (дни) и пресеты птицы (Курица, Перепел, Утка, Гусь).
//   7. Автоматическая остановка переворота на последних днях перед вылуплением.
//   8. Информативный OLED-дисплей 128x64 с анимацией и статусами.
//   9. Современный адаптивный Wi-Fi веб-интерфейс (точка доступа Inkubator).
//
//  Назначение пинов:
//   DHT11 / DHT22 - GPIO 13
//   Реле          - GPIO 4 (или 16), активный LOW (регулируется)
//   OLED 128x64   - I2C SDA=21, SCL=22 (адрес 0x3C)
//   ULN2003       - IN1=19, IN2=18, IN3=5, IN4=17
//   Зуммер (опц.) - GPIO 2 (встроенный светодиод / зуммер тревоги)
// =====================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <Preferences.h>

// ------------------------ СЕТЕВЫЕ НАСТРОЙКИ ------------------------
const char* AP_SSID     = "Inkubator";      // Имя Wi-Fi сети
const char* AP_PASSWORD = "12345678";       // Пароль сети (не менее 8 символов)

// ------------------------ НАСТРОЙКА ПИНОВ ------------------------
#define DHTPIN            13                // Пин данных датчика температуры/влажности
#define DHTTYPE           DHT11             // DHT11 или DHT22 (AM2302)

// Реле нагревателя:
// ВНИМАНИЕ: на некоторых платах GPIO16 имеет аппаратный pull-down при старте.
// Рекомендуется использовать GPIO4 (или GPIO25/GPIO26).
#define RELAY_PIN         4                 // Пин управления реле
#define RELAY_ACTIVE_LOW  true              // true = LOW включает реле, false = HIGH

#define BUZZER_PIN        2                 // Пин зуммера/светодиода тревоги (опционально)

// OLED I2C
#define I2C_SDA           21
#define I2C_SCL           22
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_ADDR         0x3C

// Шаговый двигатель 28BYJ-48 + ULN2003
#define MOTOR_PIN_1       19                // IN1
#define MOTOR_PIN_2       18                // IN2
#define MOTOR_PIN_3       5                 // IN3
#define MOTOR_PIN_4       17                // IN4

// ------------------------ КОНСТАНТЫ И ПАРАМЕТРЫ ------------------------
#define HALFSTEP          8                 // 8-шаговый полушаговый режим для плавности
#define STEPS_PER_TILT    512               // Шагов для наклона лотка на ~45 градусов
#define CRITICAL_MAX_TEMP 39.5f             // Аварийный порог отключения нагрева (°C)
#define MIN_PLAUSIBLE_T   10.0f             // Минимальная правдоподобная температура (°C)
#define MAX_PLAUSIBLE_T   50.0f             // Максимальная правдоподобная температура (°C)

// ------------------------ ПРЕСЕТЫ ИНКУБАЦИИ ------------------------
struct BirdPreset {
  const char* name;
  float setTemp;
  int setHum;
  int totalDays;
  int stopTurnDay;
  float finalTemp;
  int finalHum;
};

const BirdPreset PRESETS[] = {
  { "Курица",  37.8f, 55, 21, 19, 37.3f, 70 },
  { "Перепел", 37.7f, 50, 17, 15, 37.2f, 65 },
  { "Утка",    37.5f, 60, 28, 25, 37.0f, 75 },
  { "Гусь",    37.4f, 65, 30, 27, 37.0f, 80 },
  { "Свой",    37.5f, 55, 21, 19, 37.5f, 55 }
};
const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

// ------------------------ ОБЪЕКТЫ ------------------------
DHT dht(DHTPIN, DHTTYPE);
TwoWire I2C_OLED = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, -1);
AccelStepper stepper(HALFSTEP, MOTOR_PIN_1, MOTOR_PIN_3, MOTOR_PIN_2, MOTOR_PIN_4);
WebServer server(80);
Preferences prefs;

// ------------------------ ПЕРЕМЕННЫЕ СОСТОЯНИЯ ------------------------
float   temperature     = NAN;
float   humidity        = NAN;
float   rawTemperature  = NAN;
float   rawHumidity     = NAN;
int     sensorFailCount = 0;
bool    sensorError     = false;
bool    overheatAlert   = false;

// Настройки (хранятся в NVS flash)
float   setTemp         = 37.8f;            // Целевая температура (°C)
float   hysteresis      = 0.2f;             // Гистерезис (°C)
bool    autoTurnOn      = true;             // Авто-переворот лотка
int     turnIntervalH   = 2;                // Интервал переворота (часов, обычно 2-4 ч)
int     currentDay      = 1;                // Текущий день инкубации
int     selectedPreset  = 0;                // 0=Курица, 1=Перепел, ...
float   tempOffset      = 0.0f;             // Калибровка температуры
float   humOffset       = 0.0f;             // Калибровка влажности

// Управление реле и нагревателем
volatile bool heating    = false;           // Реле активно в данный момент
volatile bool manualHeat = false;           // Ручной режим форсированного нагрева
unsigned long manualHeatStartMs = 0;        // Время включения ручного режима
const unsigned long MANUAL_TIMEOUT_MS = 15 * 60 * 1000UL; // Авто-выключение ручного через 15 мин

// Шаговый двигатель и положение лотка
enum TrayPosition { TRAY_CENTER = 0, TRAY_LEFT = -1, TRAY_RIGHT = 1 };
TrayPosition currentTrayPos = TRAY_CENTER;
TrayPosition targetTrayPos  = TRAY_CENTER;
bool    turnInProgress      = false;
unsigned long lastTurnTimeMs= 0;
unsigned long bootTimeMs    = 0;
unsigned long lastDayTickMs = 0;
bool    oledReady           = false;

// ------------------------ ФУНКЦИИ УПРАВЛЕНИЯ ПИТАНИЕМ ШАГОВИКА ------------------------
// Критически важно: обесточивание обмоток 28BYJ-48 после движения предотвращает перегрев ULN2003!
void releaseStepperOutputs() {
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
  digitalWrite(MOTOR_PIN_3, LOW);
  digitalWrite(MOTOR_PIN_4, LOW);
}

// ------------------------ РЕЛЕ ------------------------
void setRelayState(bool turnOn) {
  heating = turnOn;
  bool pinState = RELAY_ACTIVE_LOW ? !turnOn : turnOn;
  digitalWrite(RELAY_PIN, pinState ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, overheatAlert ? HIGH : LOW);
}

// ------------------------ ЗАГРУЗКА И СОХРАНЕНИЕ В NVS ------------------------
void loadPreferences() {
  prefs.begin("inkubator", true); // read-only
  setTemp         = prefs.getFloat("setTemp", 37.8f);
  hysteresis      = prefs.getFloat("hyst", 0.2f);
  autoTurnOn      = prefs.getBool("autoTurn", true);
  turnIntervalH   = prefs.getInt("turnH", 2);
  currentDay      = prefs.getInt("day", 1);
  selectedPreset  = prefs.getInt("preset", 0);
  tempOffset      = prefs.getFloat("calT", 0.0f);
  humOffset       = prefs.getFloat("calH", 0.0f);
  prefs.end();

  if (turnIntervalH < 1) turnIntervalH = 2;
  if (currentDay < 1) currentDay = 1;
  if (selectedPreset < 0 || selectedPreset >= NUM_PRESETS) selectedPreset = 0;
  Serial.println("[NVS] Настройки успешно загружены из энергонезависимой памяти.");
}

void savePreferences() {
  prefs.begin("inkubator", false); // read-write
  prefs.putFloat("setTemp", setTemp);
  prefs.putFloat("hyst", hysteresis);
  prefs.putBool("autoTurn", autoTurnOn);
  prefs.putInt("turnH", turnIntervalH);
  prefs.putInt("day", currentDay);
  prefs.putInt("preset", selectedPreset);
  prefs.putFloat("calT", tempOffset);
  prefs.putFloat("calH", humOffset);
  prefs.end();
  Serial.println("[NVS] Настройки сохранены во Flash.");
}

// ------------------------ ВСТРОЕННАЯ ВЕБ-СТРАНИЦА ------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Инкубатор ESP32 — Умная автоматика</title>
<style>
  :root{
    --bg:#0b0f17; --card:#151c28; --card-border:#222f42;
    --accent:#ff9f1c; --accent-glow:rgba(255,159,28,0.25);
    --ok:#10b981; --bad:#ef4444; --warn:#f59e0b;
    --text:#f1f5f9; --muted:#94a3b8; --blue:#38bdf8;
  }
  *{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,sans-serif}
  body{background:var(--bg);color:var(--text);min-height:100vh;padding:14px;display:flex;justify-content:center}
  .wrap{width:100%;max-width:540px}
  header{display:flex;justify-content:space-between;align-items:center;padding:10px 0 16px;border-bottom:1px solid var(--card-border);margin-bottom:14px}
  .brand{display:flex;align-items:center;gap:10px}
  .logo{font-size:26px}
  h1{font-size:19px;font-weight:700;letter-spacing:-0.3px}
  .header-badges{display:flex;align-items:center;gap:8px}
  .status-pill{font-size:11px;font-weight:600;padding:4px 10px;border-radius:999px;background:#1e293b;color:var(--muted)}
  .status-pill.online{background:rgba(16,185,129,0.15);color:var(--ok);border:1px solid rgba(16,185,129,0.3)}
  .status-pill.offline{background:rgba(239,68,68,0.15);color:var(--bad)}

  .alert-banner{background:rgba(239,68,68,0.15);border:1px solid var(--bad);color:#fca5a5;padding:10px 14px;border-radius:12px;font-size:13px;margin-bottom:14px;display:none;align-items:center;gap:10px}
  .alert-banner.show{display:flex}

  .grid-2{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:14px}
  .card{background:var(--card);border:1px solid var(--card-border);border-radius:16px;padding:14px;box-shadow:0 4px 16px rgba(0,0,0,0.35);position:relative;overflow:hidden}
  .card-label{font-size:11px;font-weight:600;text-transform:uppercase;color:var(--muted);letter-spacing:0.5px;display:flex;justify-content:space-between}
  .card-value{font-size:34px;font-weight:800;margin:6px 0 2px;letter-spacing:-0.5px}
  .card-value small{font-size:16px;font-weight:500;color:var(--muted);margin-left:2px}
  .card-sub{font-size:12px;color:var(--muted);display:flex;align-items:center;gap:4px}
  .temp-val{color:#fbbf24}
  .hum-val{color:var(--blue)}

  .section-card{background:var(--card);border:1px solid var(--card-border);border-radius:16px;padding:16px;margin-bottom:14px}
  .section-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
  .section-title{font-size:14px;font-weight:700;display:flex;align-items:center;gap:8px}

  .indicator-row{display:flex;align-items:center;gap:10px;margin-bottom:14px}
  .chip{padding:6px 12px;border-radius:10px;font-size:12px;font-weight:600;background:#1e293b;color:var(--muted);display:inline-flex;align-items:center;gap:6px}
  .chip.active-heat{background:rgba(255,159,28,0.18);color:var(--accent);border:1px solid rgba(255,159,28,0.4)}
  .chip.active-cool{background:rgba(56,189,248,0.15);color:var(--blue);border:1px solid rgba(56,189,248,0.3)}

  .btn-row{display:flex;gap:8px;margin-top:10px}
  button{flex:1;border:none;border-radius:12px;padding:11px 14px;font-size:13px;font-weight:600;cursor:pointer;background:#1e293b;color:var(--text);display:flex;align-items:center;justify-content:center;gap:6px;transition:0.15s}
  button:hover{background:#283548}
  button:active{transform:scale(0.98)}
  button.primary{background:var(--accent);color:#0f172a}
  button.primary:hover{background:#f59518}
  button.danger{background:#dc2626;color:#fff}
  button.success{background:#10b981;color:#0b1e16}
  button.outline{background:transparent;border:1px solid var(--card-border);color:var(--text)}

  .slider-box{margin-top:12px}
  .slider-header{display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-bottom:6px}
  input[type=range]{width:100%;height:8px;border-radius:4px;background:#1e293b;outline:none;accent-color:var(--accent);cursor:pointer}

  /* Визуализатор лотка */
  .tray-visualizer{height:60px;background:#0d131d;border-radius:12px;border:1px dashed var(--card-border);display:flex;align-items:center;justify-content:center;position:relative;margin:10px 0}
  .tray-beam{width:160px;height:8px;background:#38bdf8;border-radius:4px;transition:transform 0.6s cubic-bezier(0.34, 1.56, 0.64, 1);position:relative}
  .tray-beam::before, .tray-beam::after{content:'🥚';position:absolute;top:-18px;font-size:14px}
  .tray-beam::before{left:16px}
  .tray-beam::after{right:16px}
  .tray-pivot{width:12px;height:12px;background:#f8fafc;border-radius:50%;position:absolute;z-index:2}

  /* Пресеты */
  .preset-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:10px}
  .preset-btn{padding:8px 4px;font-size:11px;border-radius:10px;text-align:center;border:1px solid var(--card-border);background:#101724;cursor:pointer}
  .preset-btn.active{border-color:var(--accent);background:var(--accent-glow);color:var(--accent);font-weight:700}

  footer{text-align:center;color:var(--muted);font-size:11px;padding:16px 0;line-height:1.6}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="brand">
      <span class="logo">🥚</span>
      <div>
        <h1>Инкубатор ESP32</h1>
        <div style="font-size:11px;color:var(--muted)">v2.0 &middot; Точка доступа Inkubator</div>
      </div>
    </div>
    <div class="header-badges">
      <span id="connBadge" class="status-pill">Связь...</span>
    </div>
  </header>

  <div id="alertBanner" class="alert-banner">
    <span style="font-size:18px">⚠️</span>
    <span id="alertText">Авария датчика или перегрев!</span>
  </div>

  <!-- Главные показатели -->
  <div class="grid-2">
    <div class="card">
      <div class="card-label">
        <span>Температура</span>
        <span id="tTrend">●</span>
      </div>
      <div class="card-value temp-val" id="valTemp">--.-<small>°C</small></div>
      <div class="card-sub">
        <span>Уставка:</span> <b id="valSetTemp" style="color:var(--text)">--.-</b> °C
      </div>
    </div>
    <div class="card">
      <div class="card-label">
        <span>Влажность</span>
        <span id="valHumSet">Цель: 55%</span>
      </div>
      <div class="card-value hum-val" id="valHum">--<small>%</small></div>
      <div class="card-sub">
        <span id="humStatusText">Оптимально</span>
      </div>
    </div>
  </div>

  <!-- Календарь инкубации -->
  <div class="section-card">
    <div class="section-header">
      <div class="section-title"><span>📅</span> День инкубации</div>
      <div class="chip" id="presetNameBadge">Курица</div>
    </div>
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
      <div style="font-size:22px;font-weight:700">День <span id="dayNum">1</span> <span style="font-size:13px;font-weight:400;color:var(--muted)">из <span id="totalDaysNum">21</span></span></div>
      <div style="font-size:12px;color:var(--muted)" id="daysLeftText">Осталось 20 дней</div>
    </div>
    <div style="width:100%;height:6px;background:#1e293b;border-radius:3px;overflow:hidden">
      <div id="dayProgress" style="width:5%;height:100%;background:var(--accent);transition:0.3s"></div>
    </div>
    <div class="preset-grid">
      <button class="preset-btn active" onclick="setPreset('chicken')">Курица<br><small style="color:var(--muted)">21 дн</small></button>
      <button class="preset-btn" onclick="setPreset('quail')">Перепел<br><small style="color:var(--muted)">17 дн</small></button>
      <button class="preset-btn" onclick="setPreset('duck')">Утка<br><small style="color:var(--muted)">28 дн</small></button>
      <button class="preset-btn" onclick="setPreset('goose')">Гусь<br><small style="color:var(--muted)">30 дн</small></button>
    </div>
  </div>

  <!-- Обогрев и термостат -->
  <div class="section-card">
    <div class="section-header">
      <div class="section-title"><span>🔥</span> Обогрев (Реле)</div>
      <div class="chip" id="heatStatusChip">Выключен</div>
    </div>
    <div class="indicator-row">
      <span class="chip" id="modeChip">Автопилот</span>
      <span class="chip" id="hystChip">±0.2 °C</span>
    </div>
    <div class="slider-box">
      <div class="slider-header">
        <span>Целевая температура инкубатора</span>
        <b id="sliderValDisplay" style="color:var(--accent);font-size:14px">37.8 °C</b>
      </div>
      <input type="range" id="tempSlider" min="35.0" max="40.0" step="0.1" value="37.8"
             oninput="document.getElementById('sliderValDisplay').textContent=Number(this.value).toFixed(1)+' °C'"
             onchange="applySetTemp(this.value)">
    </div>
    <div class="btn-row">
      <button id="btnManualHeat" class="outline" onclick="toggleManualHeat()">
        <span>⚡</span> Ручной нагрев
      </button>
      <button class="outline" onclick="openSettingsModal()">
        <span>⚙️</span> Настройки
      </button>
    </div>
  </div>

  <!-- Поворот лотка (Шаговый двигатель) -->
  <div class="section-card">
    <div class="section-header">
      <div class="section-title"><span>🔄</span> Поворот лотка (ULN2003)</div>
      <div class="chip" id="trayPosChip">Центр (0°)</div>
    </div>
    <div class="tray-visualizer">
      <div class="tray-pivot"></div>
      <div class="tray-beam" id="trayBeam"></div>
    </div>
    <div style="display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-bottom:10px">
      <span>Авто-переворот: <b id="autoTurnText" style="color:var(--ok)">Каждые 2 ч</b></span>
      <span id="nextTurnCountdown">Следующий: скоро</span>
    </div>
    <div class="btn-row">
      <button class="primary" onclick="turnTray('tilt')"><span>↻</span> Повернуть</button>
      <button id="btnAutoTurn" class="success" onclick="toggleAutoTurn()">Авто: ВКЛ</button>
      <button class="outline" onclick="turnTray('center')">Центр</button>
    </div>
  </div>

  <!-- Системные действия -->
  <div class="section-card">
    <div class="section-header">
      <div class="section-title"><span>ℹ️</span> Система ESP32</div>
      <span id="uptimeText" style="font-size:12px;color:var(--muted)">00:00:00</span>
    </div>
    <div class="btn-row">
      <button class="outline" onclick="advanceDay()"><span>+1</span> След. день</button>
      <button class="danger" onclick="rebootEsp()"><span>⟳</span> Перезагрузка</button>
    </div>
  </div>

  <footer>
    ESP32 Incubator Automation v2.0<br>
    Защита обмоток ULN2003 &middot; NVS Память &middot; HTTP API /status
  </footer>
</div>

<script>
let state = {};

function api(url, method='GET'){
  return fetch(url, {method})
    .then(r => r.json())
    .then(d => { updateUI(d); return d; })
    .catch(err => {
      document.getElementById('connBadge').textContent = 'Нет связи';
      document.getElementById('connBadge').className = 'status-pill offline';
    });
}

function updateUI(d){
  state = d;
  const cb = document.getElementById('connBadge');
  cb.textContent = 'Онлайн';
  cb.className = 'status-pill online';

  // Температура и влажность
  const valT = document.getElementById('valTemp');
  if(d.temp == null || isNaN(d.temp)){
    valT.innerHTML = '--.-<small>°C</small>';
  } else {
    valT.innerHTML = Number(d.temp).toFixed(1) + '<small>°C</small>';
  }

  const valH = document.getElementById('valHum');
  if(d.hum == null || isNaN(d.hum)){
    valH.innerHTML = '--<small>%</small>';
  } else {
    valH.innerHTML = Math.round(d.hum) + '<small>%</small>';
  }

  document.getElementById('valSetTemp').textContent = Number(d.set).toFixed(1);
  document.getElementById('sliderValDisplay').textContent = Number(d.set).toFixed(1) + ' °C';
  document.getElementById('tempSlider').value = Number(d.set).toFixed(1);

  // Статус нагрева
  const hc = document.getElementById('heatStatusChip');
  if(d.heating){
    hc.textContent = 'Нагрев ВКЛ';
    hc.className = 'chip active-heat';
  } else {
    hc.textContent = 'Выключен';
    hc.className = 'chip';
  }

  // Ручной / Авто
  const mc = document.getElementById('modeChip');
  const btnM = document.getElementById('btnManualHeat');
  if(d.manual){
    mc.textContent = 'Ручной режим!';
    mc.className = 'chip active-heat';
    btnM.className = 'danger';
    btnM.innerHTML = '<span>✕</span> Откл. ручной';
  } else {
    mc.textContent = 'Автопилот';
    mc.className = 'chip';
    btnM.className = 'outline';
    btnM.innerHTML = '<span>⚡</span> Ручной нагрев';
  }

  // Лоток и анимация
  const beam = document.getElementById('trayBeam');
  const posChip = document.getElementById('trayPosChip');
  if(d.trayPos === -1){
    beam.style.transform = 'rotate(-25deg)';
    posChip.textContent = 'Наклон влево (-45°)';
  } else if(d.trayPos === 1){
    beam.style.transform = 'rotate(25deg)';
    posChip.textContent = 'Наклон вправо (+45°)';
  } else {
    beam.style.transform = 'rotate(0deg)';
    posChip.textContent = 'Горизонт (0°)';
  }

  // Авто-переворот
  const btnA = document.getElementById('btnAutoTurn');
  const txtA = document.getElementById('autoTurnText');
  if(d.auto){
    btnA.textContent = 'Авто: ВКЛ';
    btnA.className = 'success';
    txtA.textContent = 'Каждые ' + (d.turnInterval || 2) + ' ч';
  } else {
    btnA.textContent = 'Авто: ВЫКЛ';
    btnA.className = 'outline';
    txtA.textContent = 'Отключен';
  }

  // Дни
  document.getElementById('dayNum').textContent = d.day || 1;
  document.getElementById('totalDaysNum').textContent = d.totalDays || 21;
  const pct = Math.min(100, Math.round(((d.day || 1) / (d.totalDays || 21)) * 100));
  document.getElementById('dayProgress').style.width = pct + '%';
  const left = (d.totalDays || 21) - (d.day || 1);
  document.getElementById('daysLeftText').textContent = left > 0 ? ('Осталось: ' + left + ' дн') : 'Вылупление!';

  // Авария
  const banner = document.getElementById('alertBanner');
  if(d.overheat || d.sensorError){
    banner.className = 'alert-banner show';
    document.getElementById('alertText').textContent = d.overheat ? 'КРИТИЧЕСКИЙ ПЕРЕГРЕВ! Нагрев аварийно отключен!' : 'Ошибка датчика DHT! Проверьте проводку.';
  } else {
    banner.className = 'alert-banner';
  }

  // Аптайм
  const s = d.uptime | 0;
  const hh = String(Math.floor(s/3600)).padStart(2,'0');
  const mm = String(Math.floor(s/60)%60).padStart(2,'0');
  const ss = String(s%60).padStart(2,'0');
  document.getElementById('uptimeText').textContent = 'Аптайм: ' + hh + ':' + mm + ':' + ss;
}

function applySetTemp(v){
  api('/settemp?v=' + v);
}

function toggleManualHeat(){
  const next = state.manual ? 0 : 1;
  api('/heat?on=' + next);
}

function turnTray(mode){
  api('/turn?mode=' + mode);
}

function toggleAutoTurn(){
  api('/autoturn?on=' + (state.auto ? 0 : 1));
}

function advanceDay(){
  api('/advanceday');
}

function setPreset(name){
  api('/preset?name=' + name);
}

function rebootEsp(){
  if(confirm('Перезагрузить ESP32?')){
    fetch('/reboot');
  }
}

function openSettingsModal(){
  const hyst = prompt('Гистерезис (°C, например 0.2):', state.hyst || 0.2);
  if(hyst) api('/settings?hyst=' + hyst);
}

// Опрос раз в 2 секунды
setInterval(() => api('/status'), 2000);
api('/status');
</script>
</body>
</html>
)rawliteral";

// ------------------------ HTTP ОБРАБОТЧИКИ ------------------------
void handleStatus() {
  String json;
  json.reserve(380);
  json += "{";
  if (isnan(temperature)) json += "\"temp\":null"; else { json += "\"temp\":"; json += String(temperature, 1); }
  json += ",";
  if (isnan(humidity))    json += "\"hum\":null";  else { json += "\"hum\":";  json += String(humidity, 0); }
  json += ",\"set\":"         + String(setTemp, 1);
  json += ",\"hyst\":"        + String(hysteresis, 2);
  json += ",\"heating\":"     + String(heating ? "true" : "false");
  json += ",\"manual\":"      + String(manualHeat ? "true" : "false");
  json += ",\"trayPos\":"     + String((int)currentTrayPos);
  json += ",\"turning\":"     + String(turnInProgress ? "true" : "false");
  json += ",\"auto\":"        + String(autoTurnOn ? "true" : "false");
  json += ",\"turnInterval\":"+ String(turnIntervalH);
  json += ",\"day\":"         + String(currentDay);
  json += ",\"totalDays\":"   + String(PRESETS[selectedPreset].totalDays);
  json += ",\"preset\":\""    + String(PRESETS[selectedPreset].name) + "\"";
  json += ",\"overheat\":"    + String(overheatAlert ? "true" : "false");
  json += ",\"sensorError\":" + String(sensorError ? "true" : "false");
  json += ",\"uptime\":"      + String((millis() - bootTimeMs) / 1000);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetTemp() {
  if (server.hasArg("v")) {
    float v = server.arg("v").toFloat();
    if (v >= 30.0f && v <= 42.0f) {
      setTemp = v;
      savePreferences();
    }
  }
  handleStatus();
}

void handleHeat() {
  if (server.hasArg("on")) {
    bool on = server.arg("on") == "1";
    manualHeat = on;
    manualHeatStartMs = millis();
    if (on && !overheatAlert && !sensorError) {
      setRelayState(true);
    } else {
      setRelayState(false);
    }
  }
  handleStatus();
}

void handleTurn() {
  String mode = server.arg("mode");
  if (!turnInProgress) {
    if (mode == "center") {
      targetTrayPos = TRAY_CENTER;
    } else if (mode == "left") {
      targetTrayPos = TRAY_LEFT;
    } else if (mode == "right") {
      targetTrayPos = TRAY_RIGHT;
    } else {
      targetTrayPos = (currentTrayPos == TRAY_LEFT) ? TRAY_RIGHT : TRAY_LEFT;
    }

    long targetSteps = 0;
    if (targetTrayPos == TRAY_LEFT)  targetSteps = -STEPS_PER_TILT;
    if (targetTrayPos == TRAY_RIGHT) targetSteps = STEPS_PER_TILT;
    if (targetTrayPos == TRAY_CENTER) targetSteps = 0;

    stepper.moveTo(targetSteps);
    turnInProgress = true;
    lastTurnTimeMs = millis();
  }
  handleStatus();
}

void handleAutoTurn() {
  if (server.hasArg("on")) {
    autoTurnOn = server.arg("on") == "1";
    savePreferences();
  }
  handleStatus();
}

void handleSettings() {
  bool changed = false;
  if (server.hasArg("hyst")) {
    float h = server.arg("hyst").toFloat();
    if (h >= 0.05f && h <= 2.0f) { hysteresis = h; changed = true; }
  }
  if (server.hasArg("turnH")) {
    int th = server.arg("turnH").toInt();
    if (th >= 1 && th <= 12) { turnIntervalH = th; changed = true; }
  }
  if (server.hasArg("calT")) {
    tempOffset = server.arg("calT").toFloat();
    changed = true;
  }
  if (server.hasArg("calH")) {
    humOffset = server.arg("calH").toFloat();
    changed = true;
  }
  if (changed) savePreferences();
  handleStatus();
}

void handlePreset() {
  String name = server.arg("name");
  int idx = -1;
  if (name == "chicken") idx = 0;
  else if (name == "quail") idx = 1;
  else if (name == "duck") idx = 2;
  else if (name == "goose") idx = 3;
  else if (name == "custom") idx = 4;

  if (idx >= 0 && idx < NUM_PRESETS) {
    selectedPreset = idx;
    setTemp = PRESETS[idx].setTemp;
    savePreferences();
  }
  handleStatus();
}

void handleAdvanceDay() {
  currentDay++;
  savePreferences();
  handleStatus();
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting ESP32...");
  delay(300);
  ESP.restart();
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// ------------------------ ТЕРМОРЕГУЛЯЦИЯ И ДАТЧИКИ ------------------------
void readSensor() {
  static unsigned long lastReadMs = 0;
  if (millis() - lastReadMs < 2000) return;
  lastReadMs = millis();

  float rawT = dht.readTemperature();
  float rawH = dht.readHumidity();

  if (isnan(rawT) || isnan(rawH) || rawT < MIN_PLAUSIBLE_T || rawT > MAX_PLAUSIBLE_T) {
    sensorFailCount++;
    if (sensorFailCount >= 3) {
      sensorError = true;
      temperature = NAN;
      humidity = NAN;
    }
    return;
  }

  sensorFailCount = 0;
  sensorError = false;
  rawTemperature = rawT;
  rawHumidity = rawH;

  temperature = rawT + tempOffset;
  humidity = constrain(rawH + humOffset, 5.0f, 99.0f);

  if (temperature >= CRITICAL_MAX_TEMP) {
    overheatAlert = true;
  } else if (temperature < CRITICAL_MAX_TEMP - 0.5f) {
    overheatAlert = false;
  }
}

void regulateTemperature() {
  if (overheatAlert || sensorError) {
    setRelayState(false);
    return;
  }

  if (manualHeat) {
    if (millis() - manualHeatStartMs > MANUAL_TIMEOUT_MS) {
      manualHeat = false;
    } else {
      setRelayState(true);
      return;
    }
  }

  if (isnan(temperature)) {
    setRelayState(false);
    return;
  }

  if (!heating && temperature < (setTemp - hysteresis)) {
    setRelayState(true);
  } else if (heating && temperature > (setTemp + hysteresis)) {
    setRelayState(false);
  }
}

// ------------------------ АВТО-ПЕРЕВОРОТ ЛОТКА ------------------------
void processAutoTurn() {
  if (!autoTurnOn || turnInProgress) return;

  if (currentDay >= PRESETS[selectedPreset].stopTurnDay) {
    return;
  }

  unsigned long intervalMs = (unsigned long)turnIntervalH * 3600000UL;
  if (millis() - lastTurnTimeMs >= intervalMs) {
    lastTurnTimeMs = millis();
    targetTrayPos = (currentTrayPos == TRAY_LEFT) ? TRAY_RIGHT : TRAY_LEFT;
    long targetSteps = (targetTrayPos == TRAY_LEFT) ? -STEPS_PER_TILT : STEPS_PER_TILT;
    stepper.moveTo(targetSteps);
    turnInProgress = true;
  }
}

void runStepperMovement() {
  stepper.run();
  if (turnInProgress && stepper.distanceToGo() == 0) {
    turnInProgress = false;
    currentTrayPos = targetTrayPos;
    // Обесточивание обмоток шагового двигателя после поворота
    releaseStepperOutputs();
  }
}

// ------------------------ OLED ДИСПЛЕЙ ------------------------
void drawOled() {
  static unsigned long lastOledMs = 0;
  if (!oledReady || millis() - lastOledMs < 400) return;
  lastOledMs = millis();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("D" + String(currentDay) + " ");
  display.print(PRESETS[selectedPreset].name);

  display.setCursor(84, 0);
  if (overheatAlert) display.print("!ALARM!");
  else if (heating)  display.print("[HEAT]");
  else               display.print(" Norm ");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 15);
  if (isnan(temperature)) display.print("--.-C");
  else display.print(String(temperature, 1) + "C");

  display.setCursor(72, 15);
  if (isnan(humidity)) display.print("--%");
  else display.print(String((int)humidity) + "%");

  display.setTextSize(1);
  display.setCursor(0, 35);
  display.print("Set:" + String(setTemp, 1) + "C  H:" + String(hysteresis, 1));

  display.setCursor(0, 46);
  display.print("Tray: ");
  if (turnInProgress) display.print("TURNING...");
  else if (currentTrayPos == TRAY_LEFT)  display.print("LEFT (\\)");
  else if (currentTrayPos == TRAY_RIGHT) display.print("RIGHT (/)");
  else display.print("CENTER (-)");

  display.setCursor(0, 56);
  display.print("AP:Inkubator 192.168.4.1");

  display.display();
}

void checkDayTimer() {
  const unsigned long ONE_DAY_MS = 24UL * 3600000UL;
  if (millis() - lastDayTickMs >= ONE_DAY_MS) {
    lastDayTickMs = millis();
    currentDay++;
    savePreferences();
  }
}

// ------------------------ SETUP ------------------------
void setup() {
  Serial.begin(115200);
  bootTimeMs = millis();
  lastTurnTimeMs = millis();
  lastDayTickMs = millis();

  pinMode(RELAY_PIN, OUTPUT);
  setRelayState(false);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(MOTOR_PIN_3, OUTPUT);
  pinMode(MOTOR_PIN_4, OUTPUT);
  releaseStepperOutputs();

  stepper.setMaxSpeed(600);
  stepper.setAcceleration(400);

  loadPreferences();
  dht.begin();

  I2C_OLED.begin(I2C_SDA, I2C_SCL, 400000);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledReady) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Inkubator ESP32 v2.0");
    display.println("Starting Wi-Fi AP...");
    display.println("SSID: Inkubator");
    display.println("Pass: 12345678");
    display.println("IP: 192.168.4.1");
    display.display();
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/",            HTTP_GET, handleRoot);
  server.on("/status",      HTTP_GET, handleStatus);
  server.on("/settemp",     HTTP_GET, handleSetTemp);
  server.on("/heat",        HTTP_GET, handleHeat);
  server.on("/turn",        HTTP_GET, handleTurn);
  server.on("/autoturn",    HTTP_GET, handleAutoTurn);
  server.on("/settings",    HTTP_GET, handleSettings);
  server.on("/preset",      HTTP_GET, handlePreset);
  server.on("/advanceday",  HTTP_GET, handleAdvanceDay);
  server.on("/reboot",      HTTP_GET, handleReboot);
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
}

// ------------------------ LOOP ------------------------
void loop() {
  server.handleClient();
  readSensor();
  regulateTemperature();
  processAutoTurn();
  runStepperMovement();
  drawOled();
  checkDayTimer();
  delay(2);
}
