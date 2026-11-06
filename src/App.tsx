import React, { useState, useEffect, useRef } from 'react';
import {
  Thermometer,
  Droplets,
  RotateCw,
  Cpu,
  Flame,
  Zap,
  Calendar,
  AlertTriangle,
  CheckCircle2,
  Settings,
  Download,
  Copy,
  Layers,
  Radio,
  Clock,
  Sparkles,
  Info,
  ShieldAlert,
  Sliders,
  Power,
  RotateCcw,
  Check
} from 'lucide-react';

interface PresetInfo {
  id: string;
  name: string;
  temp: number;
  hum: number;
  days: number;
  stopTurnDay: number;
  finalTemp: number;
  finalHum: number;
  description: string;
}

const BIRD_PRESETS: PresetInfo[] = [
  {
    id: 'chicken',
    name: 'Курица (несушка/бройлер)',
    temp: 37.8,
    hum: 55,
    days: 21,
    stopTurnDay: 19,
    finalTemp: 37.3,
    finalHum: 70,
    description: 'Инкубация 21 день. Остановка переворота на 19-й день, повышение влажности до 70-75%.'
  },
  {
    id: 'quail',
    name: 'Перепел (японский/техасский)',
    temp: 37.7,
    hum: 50,
    days: 17,
    stopTurnDay: 15,
    finalTemp: 37.2,
    finalHum: 65,
    description: 'Инкубация 17 дней. Остановка переворота на 15-й день, влажность на выводе 65-70%.'
  },
  {
    id: 'duck',
    name: 'Утка (пекинская/мускусная)',
    temp: 37.5,
    hum: 60,
    days: 28,
    stopTurnDay: 25,
    finalTemp: 37.0,
    finalHum: 75,
    description: 'Инкубация 28 дней. Требуется периодическое охлаждение и опрыскивание со 2-й недели.'
  },
  {
    id: 'goose',
    name: 'Гусь (холмогорский/линдовский)',
    temp: 37.4,
    hum: 65,
    days: 30,
    stopTurnDay: 27,
    finalTemp: 37.0,
    finalHum: 80,
    description: 'Инкубация 30 дней. Регулярное проветривание с 10-го дня и высокая влажность на выводе.'
  },
  {
    id: 'custom',
    name: 'Индивидуальный режим',
    temp: 37.5,
    hum: 55,
    days: 21,
    stopTurnDay: 19,
    finalTemp: 37.5,
    finalHum: 55,
    description: 'Ручная настройка параметров под редкие породы или рептилий.'
  }
];

export default function App() {
  const [activeTab, setActiveTab] = useState<'dashboard' | 'code' | 'wiring' | 'guide'>('dashboard');

  // Параметры симулятора и реального ESP32
  const [temperature, setTemperature] = useState(37.7);
  const [humidity, setHumidity] = useState(56);
  const [setTemp, setSetTemp] = useState(37.8);
  const [hysteresis, setHysteresis] = useState(0.2);
  const [isHeating, setIsHeating] = useState(false);
  const [manualHeat, setManualHeat] = useState(false);

  // Лоток (шаговый двигатель)
  const [trayAngle, setTrayAngle] = useState<-45 | 0 | 45>(-45);
  const [isTurning, setIsTurning] = useState(false);
  const [autoTurn, setAutoTurn] = useState(true);
  const [turnIntervalHours, setTurnIntervalHours] = useState(2);
  const [nextTurnSeconds, setNextTurnSeconds] = useState(7200);

  // Календарь
  const [currentDay, setCurrentDay] = useState(3);
  const [selectedPreset, setSelectedPreset] = useState<PresetInfo>(BIRD_PRESETS[0]);

  // Симуляция внешних условий
  const [ambientTemp, setAmbientTemp] = useState(22.0);
  const [lidOpen, setLidOpen] = useState(false);
  const [sensorFailSim, setSensorFailSim] = useState(false);
  const [overheatSim, setOverheatSim] = useState(false);
  const [uptimeSeconds, setUptimeSeconds] = useState(380);

  // Настройки генератора прошивки
  const [sensorType, setSensorType] = useState<'DHT11' | 'DHT22'>('DHT11');
  const [relayPin, setRelayPin] = useState<'4' | '16' | '25'>('4');
  const [wifiSsid, setWifiSsid] = useState('Inkubator');
  const [wifiPass, setWifiPass] = useState('12345678');
  const [copiedCode, setCopiedCode] = useState(false);

  // Физическая симуляция термодинамики инкубатора
  useEffect(() => {
    const timer = setInterval(() => {
      setUptimeSeconds(prev => prev + 1);

      // Отсчет до следующего поворота
      setNextTurnSeconds(prev => {
        if (!autoTurn) return prev;
        if (prev <= 1) {
          // Запуск автоповорота
          handleTurnTray();
          return turnIntervalHours * 3600;
        }
        return prev - 1;
      });

      // Симуляция сбоя датчика
      if (sensorFailSim) {
        setIsHeating(false);
        return;
      }

      setTemperature(curr => {
        if (overheatSim) {
          const highT = Math.min(41.0, curr + 0.1);
          return Math.round(highT * 10) / 10;
        }

        let delta = 0;
        // Остывание к температуре комнаты
        const coolingFactor = lidOpen ? 0.08 : 0.02;
        delta -= (curr - ambientTemp) * coolingFactor;

        // Нагрев от реле
        if (isHeating) {
          delta += 0.09;
        }

        // Шум датчика
        const noise = (Math.random() - 0.5) * 0.02;
        const nextT = Math.round((curr + delta + noise) * 10) / 10;

        // Логика термостата
        if (!manualHeat) {
          if (nextT < setTemp - hysteresis) {
            setIsHeating(true);
          } else if (nextT > setTemp + hysteresis) {
            setIsHeating(false);
          }
        }

        // Аварийная защита от перегрева
        if (nextT >= 39.5) {
          setIsHeating(false);
        }

        return nextT;
      });

      // Влажность
      setHumidity(curr => {
        const targetHum = selectedPreset.hum;
        const drift = (targetHum - curr) * 0.01 + (Math.random() - 0.5) * 0.3;
        const nextH = Math.min(95, Math.max(30, curr + drift));
        return Math.round(nextH);
      });
    }, 1000);

    return () => clearInterval(timer);
  }, [isHeating, manualHeat, setTemp, hysteresis, ambientTemp, lidOpen, sensorFailSim, overheatSim, autoTurn, turnIntervalHours, selectedPreset]);

  // Обработчик поворота лотка
  const handleTurnTray = () => {
    if (isTurning) return;
    setIsTurning(true);
    setTimeout(() => {
      setTrayAngle(prev => (prev === -45 ? 45 : -45));
      setIsTurning(false);
    }, 1200);
  };

  const handleCenterTray = () => {
    setIsTurning(true);
    setTimeout(() => {
      setTrayAngle(0);
      setIsTurning(false);
    }, 800);
  };

  const toggleManualHeating = () => {
    const next = !manualHeat;
    setManualHeat(next);
    setIsHeating(next);
  };

  const applyPreset = (preset: PresetInfo) => {
    setSelectedPreset(preset);
    setSetTemp(preset.temp);
  };

  const isOverheat = temperature >= 39.5 || overheatSim;
  const isHatchingPhase = currentDay >= selectedPreset.stopTurnDay;

  // Форматирование времени
  const formatTime = (secs: number) => {
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = secs % 60;
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  };

  // Генерация текста прошивки на лету с выбранными параметрами
  const generateFirmwareCode = () => {
    return `// =====================================================================
//  ИНКУБАТОР НА ESP32 (v2.0) — Профессиональная автоматика
//  Сгенерировано в Inkubator ESP32 Studio
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

// ------------------------ СЕТЬ И Wi-Fi ------------------------
const char* AP_SSID     = "${wifiSsid}";
const char* AP_PASSWORD = "${wifiPass}";

// ------------------------ ПИНЫ УСТРОЙСТВА ------------------------
#define DHTPIN            13            // Пин датчика DHT
#define DHTTYPE           ${sensorType}         // ${sensorType}

#define RELAY_PIN         ${relayPin}            // Реле (активный LOW, безопасный пин ${relayPin})
#define RELAY_ACTIVE_LOW  true

#define BUZZER_PIN        2             // Зуммер / аварийный светодиод

#define I2C_SDA           21            // OLED SDA
#define I2C_SCL           22            // OLED SCL
#define OLED_ADDR         0x3C

// Шаговый двигатель 28BYJ-48 + драйвер ULN2003
#define MOTOR_PIN_1       19            // IN1
#define MOTOR_PIN_2       18            // IN2
#define MOTOR_PIN_3       5             // IN3
#define MOTOR_PIN_4       17            // IN4

#define HALFSTEP          8
#define STEPS_PER_TILT    512           // Шагов для наклона лотка на ~45°
#define CRITICAL_MAX_TEMP 39.5f         // Аварийное отключение нагрева (°C)

// ------------------------ ОБЪЕКТЫ ------------------------
DHT dht(DHTPIN, DHTTYPE);
TwoWire I2C_OLED = TwoWire(0);
Adafruit_SSD1306 display(128, 64, &I2C_OLED, -1);
AccelStepper stepper(HALFSTEP, MOTOR_PIN_1, MOTOR_PIN_3, MOTOR_PIN_2, MOTOR_PIN_4);
WebServer server(80);
Preferences prefs;

// ------------------------ СОСТОЯНИЕ ------------------------
float   temperature     = NAN;
float   humidity        = NAN;
float   setTemp         = ${setTemp.toFixed(1)}f;
float   hysteresis      = ${hysteresis.toFixed(2)}f;
bool    autoTurnOn      = ${autoTurn ? 'true' : 'false'};
int     turnIntervalH   = ${turnIntervalHours};
int     currentDay      = ${currentDay};
int     selectedPreset  = 0;
float   tempOffset      = 0.0f;
float   humOffset       = 0.0f;

volatile bool heating    = false;
volatile bool manualHeat = false;
unsigned long manualHeatStartMs = 0;

enum TrayPosition { TRAY_CENTER = 0, TRAY_LEFT = -1, TRAY_RIGHT = 1 };
TrayPosition currentTrayPos = TRAY_CENTER;
TrayPosition targetTrayPos  = TRAY_CENTER;
bool turnInProgress         = false;
unsigned long lastTurnTimeMs= 0;
unsigned long bootTimeMs    = 0;
bool oledReady              = false;

// ВАЖНО: Обесточивание обмоток ULN2003 предотвращает перегрев мотора 28BYJ-48!
void releaseStepperOutputs() {
  digitalWrite(MOTOR_PIN_1, LOW);
  digitalWrite(MOTOR_PIN_2, LOW);
  digitalWrite(MOTOR_PIN_3, LOW);
  digitalWrite(MOTOR_PIN_4, LOW);
}

void setRelayState(bool turnOn) {
  heating = turnOn;
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? !turnOn : turnOn);
}

void setup() {
  Serial.begin(115200);
  bootTimeMs = millis();

  pinMode(RELAY_PIN, OUTPUT);
  setRelayState(false);

  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(MOTOR_PIN_3, OUTPUT);
  pinMode(MOTOR_PIN_4, OUTPUT);
  releaseStepperOutputs();

  stepper.setMaxSpeed(600);
  stepper.setAcceleration(400);

  prefs.begin("inkubator", false);
  setTemp = prefs.getFloat("setTemp", ${setTemp.toFixed(1)}f);
  hysteresis = prefs.getFloat("hyst", ${hysteresis.toFixed(2)}f);
  prefs.end();

  dht.begin();
  I2C_OLED.begin(I2C_SDA, I2C_SCL, 400000);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "Inkubator ESP32 Online. Use Web App /status");
  });
  server.begin();
}

void loop() {
  server.handleClient();
  stepper.run();

  if (turnInProgress && stepper.distanceToGo() == 0) {
    turnInProgress = false;
    currentTrayPos = targetTrayPos;
    releaseStepperOutputs(); // ЗАЩИТА: обесточиваем обмотки!
  }

  // Опрос датчика DHT раз в 2 секунды
  static unsigned long lastDht = 0;
  if (millis() - lastDht > 2000) {
    lastDht = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperature = t + tempOffset;
      humidity = h + humOffset;
    }
  }

  // Терморегулятор с защитой
  if (temperature >= CRITICAL_MAX_TEMP || isnan(temperature)) {
    setRelayState(false);
  } else if (manualHeat) {
    setRelayState(true);
  } else if (!heating && temperature < (setTemp - hysteresis)) {
    setRelayState(true);
  } else if (heating && temperature > (setTemp + hysteresis)) {
    setRelayState(false);
  }

  delay(2);
}`;
  };

  const handleCopyCode = () => {
    navigator.clipboard.writeText(generateFirmwareCode());
    setCopiedCode(true);
    setTimeout(() => setCopiedCode(false), 2000);
  };

  const handleDownloadIno = () => {
    const code = generateFirmwareCode();
    const blob = new Blob([code], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'incubator_web.ino';
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col">
      {/* Верхний Header */}
      <header className="border-b border-slate-800 bg-slate-900/80 backdrop-blur sticky top-0 z-40">
        <div className="max-w-7xl mx-auto px-4 py-3 sm:px-6 flex flex-wrap items-center justify-between gap-3">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-amber-500/10 border border-amber-500/30 flex items-center justify-center text-2xl shadow-inner">
              🥚
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h1 className="text-lg font-bold tracking-tight text-white">Инкубатор на ESP32</h1>
                <span className="px-2 py-0.5 rounded-full text-[10px] font-semibold bg-emerald-500/10 text-emerald-400 border border-emerald-500/30">
                  v2.0 Прошивка
                </span>
              </div>
              <p className="text-xs text-slate-400">
                Wi-Fi AP: <span className="text-amber-400 font-mono">Inkubator</span> (192.168.4.1) &middot; Термостат, ULN2003, OLED, NVS
              </p>
            </div>
          </div>

          {/* Навигационные табы */}
          <nav className="flex items-center gap-1 bg-slate-950/80 p-1 rounded-xl border border-slate-800">
            <button
              onClick={() => setActiveTab('dashboard')}
              className={`px-3 py-1.5 rounded-lg text-xs font-semibold flex items-center gap-1.5 transition ${
                activeTab === 'dashboard'
                  ? 'bg-amber-500 text-slate-950 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              <Cpu className="w-3.5 h-3.5" />
              <span>Пульт и Симулятор</span>
            </button>
            <button
              onClick={() => setActiveTab('code')}
              className={`px-3 py-1.5 rounded-lg text-xs font-semibold flex items-center gap-1.5 transition ${
                activeTab === 'code'
                  ? 'bg-amber-500 text-slate-950 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              <Layers className="w-3.5 h-3.5" />
              <span>Прошивка ESP32</span>
            </button>
            <button
              onClick={() => setActiveTab('wiring')}
              className={`px-3 py-1.5 rounded-lg text-xs font-semibold flex items-center gap-1.5 transition ${
                activeTab === 'wiring'
                  ? 'bg-amber-500 text-slate-950 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              <Zap className="w-3.5 h-3.5" />
              <span>Схема подключения</span>
            </button>
            <button
              onClick={() => setActiveTab('guide')}
              className={`px-3 py-1.5 rounded-lg text-xs font-semibold flex items-center gap-1.5 transition ${
                activeTab === 'guide'
                  ? 'bg-amber-500 text-slate-950 shadow'
                  : 'text-slate-400 hover:text-slate-200'
              }`}
            >
              <Calendar className="w-3.5 h-3.5" />
              <span>Режимы птицы</span>
            </button>
          </nav>
        </div>
      </header>

      {/* Основной контент */}
      <main className="flex-1 max-w-7xl mx-auto w-full px-4 py-6 sm:px-6">
        {activeTab === 'dashboard' && (
          <div className="grid grid-cols-1 lg:grid-cols-12 gap-6">
            {/* Левая колонка: Виртуальный пульт управления */}
            <div className="lg:col-span-8 space-y-6">
              {/* Аварийный баннер */}
              {isOverheat && (
                <div className="bg-red-500/15 border border-red-500/40 rounded-2xl p-4 flex items-center gap-3 text-red-200 animate-pulse">
                  <ShieldAlert className="w-6 h-6 text-red-400 shrink-0" />
                  <div>
                    <div className="text-sm font-bold">КРИТИЧЕСКИЙ ПЕРЕГРЕВ (&gt;39.5 °C)!</div>
                    <div className="text-xs text-red-300">
                      Аварийная автоматика принудительно отключила нагреватель для спасения зародышей.
                    </div>
                  </div>
                </div>
              )}

              {sensorFailSim && (
                <div className="bg-amber-500/15 border border-amber-500/40 rounded-2xl p-4 flex items-center gap-3 text-amber-200">
                  <AlertTriangle className="w-6 h-6 text-amber-400 shrink-0" />
                  <div>
                    <div className="text-sm font-bold">ОШИБКА ДАТЧИКА DHT (НЕТ ДАННЫХ)</div>
                    <div className="text-xs text-amber-300">
                      Нагрев выключен для защиты от перегрева при потере связи с датчиком.
                    </div>
                  </div>
                </div>
              )}

              {/* Главные показатели: Температура и Влажность */}
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
                {/* Карточка температуры */}
                <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 relative overflow-hidden shadow-lg">
                  <div className="flex items-center justify-between text-xs uppercase font-semibold text-slate-400 tracking-wider">
                    <span className="flex items-center gap-1.5">
                      <Thermometer className="w-4 h-4 text-amber-400" />
                      Температура
                    </span>
                    <span className="flex items-center gap-1">
                      {isHeating ? (
                        <span className="flex items-center gap-1 text-amber-400 font-bold animate-pulse">
                          <Flame className="w-3.5 h-3.5" /> Нагрев
                        </span>
                      ) : (
                        <span className="text-slate-500">Пассивно</span>
                      )}
                    </span>
                  </div>

                  <div className="mt-3 flex items-baseline gap-2">
                    <span className="text-4xl sm:text-5xl font-extrabold tracking-tight text-amber-400 font-mono">
                      {sensorFailSim ? '--.-' : temperature.toFixed(1)}
                    </span>
                    <span className="text-xl text-slate-400 font-medium">°C</span>
                  </div>

                  <div className="mt-4 pt-3 border-t border-slate-800/80 flex items-center justify-between text-xs text-slate-400">
                    <span>
                      Уставка: <strong className="text-white font-mono">{setTemp.toFixed(1)} °C</strong>
                    </span>
                    <span>
                      Гистерезис: <strong className="text-slate-300">±{hysteresis.toFixed(2)} °C</strong>
                    </span>
                  </div>
                </div>

                {/* Карточка влажности */}
                <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 relative overflow-hidden shadow-lg">
                  <div className="flex items-center justify-between text-xs uppercase font-semibold text-slate-400 tracking-wider">
                    <span className="flex items-center gap-1.5">
                      <Droplets className="w-4 h-4 text-sky-400" />
                      Влажность
                    </span>
                    <span className="text-sky-400/90 font-medium text-xs">
                      Оптимум: {selectedPreset.hum}%
                    </span>
                  </div>

                  <div className="mt-3 flex items-baseline gap-2">
                    <span className="text-4xl sm:text-5xl font-extrabold tracking-tight text-sky-400 font-mono">
                      {sensorFailSim ? '--' : Math.round(humidity)}
                    </span>
                    <span className="text-xl text-slate-400 font-medium">%</span>
                  </div>

                  <div className="mt-4 pt-3 border-t border-slate-800/80 flex items-center justify-between text-xs text-slate-400">
                    <span>
                      Статус: <strong className="text-emerald-400">В норме (ванночка)</strong>
                    </span>
                    <span>DHT11/DHT22</span>
                  </div>
                </div>
              </div>

              {/* Управление термостатом */}
              <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 shadow-lg space-y-4">
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-2">
                    <Flame className="w-5 h-5 text-amber-500" />
                    <h2 className="font-semibold text-sm text-slate-200">Терморегулятор и Реле нагревателя</h2>
                  </div>
                  <div className="flex items-center gap-2">
                    <span
                      className={`px-2.5 py-1 rounded-full text-xs font-semibold ${
                        isHeating
                          ? 'bg-amber-500/15 text-amber-400 border border-amber-500/30'
                          : 'bg-slate-800 text-slate-400'
                      }`}
                    >
                      {isHeating ? 'Реле: ВКЛЮЧЕНО (LOW)' : 'Реле: ВЫКЛЮЧЕНО'}
                    </span>
                  </div>
                </div>

                {/* Ползунок уставки */}
                <div>
                  <div className="flex justify-between items-center text-xs text-slate-400 mb-2">
                    <span>Задать целевую температуру</span>
                    <span className="font-mono text-base font-bold text-amber-400">{setTemp.toFixed(1)} °C</span>
                  </div>
                  <input
                    type="range"
                    min="35.0"
                    max="40.0"
                    step="0.1"
                    value={setTemp}
                    onChange={e => setSetTemp(parseFloat(e.target.value))}
                    className="w-full h-2 bg-slate-800 rounded-lg appearance-none cursor-pointer accent-amber-500"
                  />
                  <div className="flex justify-between text-[11px] text-slate-500 mt-1">
                    <span>35.0 °C</span>
                    <span>37.5 °C (Стандарт)</span>
                    <span>37.8 °C (Курица)</span>
                    <span>40.0 °C</span>
                  </div>
                </div>

                {/* Кнопки ручного управления */}
                <div className="flex flex-wrap gap-3 pt-2">
                  <button
                    onClick={toggleManualHeating}
                    className={`flex-1 py-2.5 px-4 rounded-xl font-semibold text-xs flex items-center justify-center gap-2 transition ${
                      manualHeat
                        ? 'bg-red-600 hover:bg-red-700 text-white shadow-lg shadow-red-900/30'
                        : 'bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700'
                    }`}
                  >
                    <Power className="w-4 h-4" />
                    {manualHeat ? 'Выключить ручной нагрев' : 'Включить вручную (тест)'}
                  </button>

                  <div className="flex items-center gap-2 px-3 py-2 bg-slate-950/60 rounded-xl border border-slate-800 text-xs">
                    <span className="text-slate-400">Гистерезис:</span>
                    <button
                      onClick={() => setHysteresis(0.1)}
                      className={`px-2 py-1 rounded ${hysteresis === 0.1 ? 'bg-amber-500 text-slate-950 font-bold' : 'text-slate-400 hover:text-white'}`}
                    >
                      ±0.1°
                    </button>
                    <button
                      onClick={() => setHysteresis(0.2)}
                      className={`px-2 py-1 rounded ${hysteresis === 0.2 ? 'bg-amber-500 text-slate-950 font-bold' : 'text-slate-400 hover:text-white'}`}
                    >
                      ±0.2°
                    </button>
                    <button
                      onClick={() => setHysteresis(0.3)}
                      className={`px-2 py-1 rounded ${hysteresis === 0.3 ? 'bg-amber-500 text-slate-950 font-bold' : 'text-slate-400 hover:text-white'}`}
                    >
                      ±0.3°
                    </button>
                  </div>
                </div>
              </div>

              {/* Управление поворотом лотка (Шаговый двигатель 28BYJ-48 + ULN2003) */}
              <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 shadow-lg space-y-4">
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-2">
                    <RotateCw className="w-5 h-5 text-sky-400" />
                    <h2 className="font-semibold text-sm text-slate-200">Поворот лотка (28BYJ-48 + ULN2003)</h2>
                  </div>
                  <span
                    className={`px-2.5 py-1 rounded-full text-xs font-semibold ${
                      isTurning
                        ? 'bg-sky-500/15 text-sky-400 border border-sky-500/30 animate-pulse'
                        : 'bg-slate-800 text-slate-400'
                    }`}
                  >
                    {isTurning ? 'Поворот лотка...' : 'Обмотки обесточены (Холодный)'}
                  </span>
                </div>

                {/* Графический визуализатор лотка */}
                <div className="h-28 bg-slate-950/80 rounded-xl border border-slate-800 flex flex-col items-center justify-center relative overflow-hidden">
                  <div className="text-[11px] text-slate-500 absolute top-2 left-3">
                    Положение: <strong className="text-sky-400">{trayAngle === 0 ? 'Горизонт (0°)' : trayAngle === -45 ? 'Наклон влево (-45°)' : 'Наклон вправо (+45°)'}</strong>
                  </div>

                  {/* Шкала углов */}
                  <div className="w-48 h-12 flex items-center justify-center relative">
                    <div
                      className="w-40 h-3 bg-gradient-to-r from-sky-500 to-amber-500 rounded-full transition-transform duration-700 ease-out flex items-center justify-between px-2 shadow-lg"
                      style={{ transform: `rotate(${trayAngle * 0.55}deg)` }}
                    >
                      <span className="text-xs">🥚</span>
                      <span className="text-xs">🥚</span>
                      <span className="text-xs">🥚</span>
                      <span className="text-xs">🥚</span>
                    </div>
                    {/* Ось вращения */}
                    <div className="w-4 h-4 rounded-full bg-white border-2 border-slate-900 absolute z-10 shadow" />
                  </div>

                  <div className="text-[10px] text-slate-500 flex gap-12 mt-2">
                    <span>-45°</span>
                    <span>0°</span>
                    <span>+45°</span>
                  </div>
                </div>

                {/* Настройки расписания переворота */}
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 text-xs">
                  <div className="bg-slate-950/50 p-3 rounded-xl border border-slate-800 flex items-center justify-between">
                    <div>
                      <div className="text-slate-400">Авто-переворот</div>
                      <div className="font-semibold text-slate-200">
                        {autoTurn ? `Каждые ${turnIntervalHours} ч` : 'Отключен вручную'}
                      </div>
                    </div>
                    <button
                      onClick={() => setAutoTurn(!autoTurn)}
                      className={`px-3 py-1.5 rounded-lg font-semibold text-xs ${
                        autoTurn ? 'bg-emerald-500/20 text-emerald-400 border border-emerald-500/30' : 'bg-slate-800 text-slate-400'
                      }`}
                    >
                      {autoTurn ? 'ВКЛ' : 'ВЫКЛ'}
                    </button>
                  </div>

                  <div className="bg-slate-950/50 p-3 rounded-xl border border-slate-800 flex items-center justify-between">
                    <div>
                      <div className="text-slate-400">До переворота</div>
                      <div className="font-mono font-bold text-sky-400">
                        {autoTurn ? formatTime(nextTurnSeconds) : '—'}
                      </div>
                    </div>
                    <span className="text-[11px] text-slate-500">Автопилот</span>
                  </div>
                </div>

                {/* Кнопки наклона */}
                <div className="flex flex-wrap gap-2 pt-1">
                  <button
                    onClick={handleTurnTray}
                    disabled={isTurning}
                    className="flex-1 py-2 px-3 bg-amber-500 hover:bg-amber-400 text-slate-950 rounded-xl font-bold text-xs flex items-center justify-center gap-1.5 shadow transition disabled:opacity-50"
                  >
                    <RotateCw className={`w-3.5 h-3.5 ${isTurning ? 'animate-spin' : ''}`} />
                    Повернуть сейчас
                  </button>

                  <button
                    onClick={() => {
                      setTrayAngle(-45);
                    }}
                    className="py-2 px-3 bg-slate-800 hover:bg-slate-700 text-slate-200 rounded-xl text-xs font-semibold border border-slate-700"
                  >
                    Наклон \
                  </button>

                  <button
                    onClick={handleCenterTray}
                    className="py-2 px-3 bg-slate-800 hover:bg-slate-700 text-slate-200 rounded-xl text-xs font-semibold border border-slate-700"
                  >
                    Центр (0°)
                  </button>

                  <button
                    onClick={() => {
                      setTrayAngle(45);
                    }}
                    className="py-2 px-3 bg-slate-800 hover:bg-slate-700 text-slate-200 rounded-xl text-xs font-semibold border border-slate-700"
                  >
                    Наклон /
                  </button>
                </div>

                {isHatchingPhase && (
                  <div className="bg-amber-500/10 border border-amber-500/30 rounded-xl p-3 text-xs text-amber-300 flex items-center gap-2">
                    <Info className="w-4 h-4 shrink-0 text-amber-400" />
                    <span>
                      Период вылупления (день {currentDay}): переворот автоматически заблокирован, птенцы готовятся к проклеву.
                    </span>
                  </div>
                )}
              </div>
            </div>

            {/* Правая колонка: Виртуальный OLED экран + Календарь + Симулятор условий */}
            <div className="lg:col-span-4 space-y-6">
              {/* Виртуальный OLED 128x64 SSD1306 дисплей */}
              <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 shadow-lg">
                <div className="flex items-center justify-between mb-3 text-xs font-semibold text-slate-300">
                  <span className="flex items-center gap-1.5">
                    <TvIcon className="w-4 h-4 text-cyan-400" />
                    Экран OLED SSD1306 (128x64 I2C)
                  </span>
                  <span className="text-[10px] text-cyan-400/80 font-mono">0x3C</span>
                </div>

                {/* Пиксельный дисплей */}
                <div className="bg-[#050811] p-4 rounded-xl border-2 border-cyan-500/40 font-mono text-cyan-300 shadow-inner select-none">
                  {/* Верхняя строка */}
                  <div className="flex justify-between items-center text-[11px] pb-1 border-b border-cyan-800/80">
                    <span>
                      D{currentDay} {selectedPreset.name.split(' ')[0]}
                    </span>
                    <span className="font-bold">
                      {isOverheat ? '!ALARM!' : isHeating ? '[HEAT]' : ' Norm '}
                    </span>
                  </div>

                  {/* Крупные цифры */}
                  <div className="flex justify-between items-baseline my-2">
                    <div className="text-2xl font-bold tracking-tight">
                      {sensorFailSim ? '--.-C' : `${temperature.toFixed(1)}C`}
                    </div>
                    <div className="text-xl font-bold">
                      {sensorFailSim ? '--%' : `${Math.round(humidity)}%`}
                    </div>
                  </div>

                  {/* Нижние строки */}
                  <div className="text-[10px] text-cyan-400/90 space-y-0.5 pt-1 border-t border-cyan-900/60">
                    <div>Set: {setTemp.toFixed(1)}C  H: {hysteresis.toFixed(1)}</div>
                    <div>
                      Tray: {isTurning ? 'TURNING...' : trayAngle === -45 ? 'LEFT (\\)' : trayAngle === 45 ? 'RIGHT (/)' : 'CENTER (-)'}
                    </div>
                    <div className="text-[9px] text-cyan-500/80 pt-0.5">
                      AP: Inkubator 192.168.4.1
                    </div>
                  </div>
                </div>
              </div>

              {/* Календарь инкубации */}
              <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 shadow-lg space-y-4">
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-2">
                    <Calendar className="w-4 h-4 text-amber-500" />
                    <h3 className="font-semibold text-sm text-slate-200">Календарь инкубации</h3>
                  </div>
                  <span className="text-xs font-mono text-amber-400">
                    День {currentDay} / {selectedPreset.days}
                  </span>
                </div>

                {/* Прогресс-бар */}
                <div>
                  <div className="w-full h-2.5 bg-slate-800 rounded-full overflow-hidden">
                    <div
                      className="h-full bg-gradient-to-r from-amber-500 to-emerald-400 transition-all duration-500"
                      style={{ width: `${Math.min(100, (currentDay / selectedPreset.days) * 100)}%` }}
                    />
                  </div>
                  <div className="flex justify-between text-[11px] text-slate-400 mt-1">
                    <span>Закладка (1 дн)</span>
                    <span>Стоп переворот ({selectedPreset.stopTurnDay} дн)</span>
                    <span>Вывод ({selectedPreset.days} дн)</span>
                  </div>
                </div>

                {/* Выбор пресета птицы */}
                <div className="space-y-1.5">
                  <div className="text-xs text-slate-400 font-medium">Профиль птицы:</div>
                  <div className="grid grid-cols-2 gap-2">
                    {BIRD_PRESETS.slice(0, 4).map(preset => (
                      <button
                        key={preset.id}
                        onClick={() => applyPreset(preset)}
                        className={`p-2 rounded-xl text-left border transition ${
                          selectedPreset.id === preset.id
                            ? 'bg-amber-500/15 border-amber-500 text-amber-300 font-bold'
                            : 'bg-slate-950/60 border-slate-800 text-slate-400 hover:border-slate-700'
                        }`}
                      >
                        <div className="text-xs">{preset.name.split(' ')[0]}</div>
                        <div className="text-[10px] text-slate-500 font-mono">
                          {preset.temp}°C &middot; {preset.days} дн
                        </div>
                      </button>
                    ))}
                  </div>
                </div>

                {/* Кнопки дня */}
                <div className="flex items-center gap-2 pt-1">
                  <button
                    onClick={() => setCurrentDay(d => Math.max(1, d - 1))}
                    className="flex-1 py-1.5 px-3 bg-slate-800 hover:bg-slate-700 rounded-lg text-xs font-medium text-slate-300"
                  >
                    -1 день
                  </button>
                  <button
                    onClick={() => setCurrentDay(d => Math.min(selectedPreset.days, d + 1))}
                    className="flex-1 py-1.5 px-3 bg-amber-500 hover:bg-amber-400 rounded-lg text-xs font-bold text-slate-950"
                  >
                    +1 день
                  </button>
                  <button
                    onClick={() => setCurrentDay(1)}
                    className="py-1.5 px-2 bg-slate-800 hover:bg-slate-700 rounded-lg text-xs text-slate-400"
                    title="Сброс на 1 день"
                  >
                    <RotateCcw className="w-3.5 h-3.5" />
                  </button>
                </div>
              </div>

              {/* Песочница симулятора (Тестирование работы автоматики) */}
              <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 shadow-lg space-y-4">
                <div className="flex items-center gap-2 text-xs font-semibold text-slate-300">
                  <Sliders className="w-4 h-4 text-purple-400" />
                  <span>Тест-песочница инкубатора</span>
                </div>

                <div className="space-y-3 text-xs">
                  {/* Температура в комнате */}
                  <div>
                    <div className="flex justify-between text-slate-400 mb-1">
                      <span>Температура в комнате:</span>
                      <span className="font-mono text-slate-200">{ambientTemp.toFixed(1)} °C</span>
                    </div>
                    <input
                      type="range"
                      min="15"
                      max="30"
                      step="0.5"
                      value={ambientTemp}
                      onChange={e => setAmbientTemp(parseFloat(e.target.value))}
                      className="w-full h-1.5 bg-slate-800 rounded-lg appearance-none cursor-pointer accent-purple-400"
                    />
                  </div>

                  {/* Переключатели ситуаций */}
                  <div className="space-y-2 pt-1">
                    <label className="flex items-center justify-between p-2 bg-slate-950/60 rounded-xl border border-slate-800 cursor-pointer">
                      <span className="text-slate-300">Открыта крышка инкубатора</span>
                      <input
                        type="checkbox"
                        checked={lidOpen}
                        onChange={e => setLidOpen(e.target.checked)}
                        className="rounded accent-amber-500 w-4 h-4"
                      />
                    </label>

                    <label className="flex items-center justify-between p-2 bg-slate-950/60 rounded-xl border border-slate-800 cursor-pointer">
                      <span className="text-slate-300">Имитация сбоя датчика DHT</span>
                      <input
                        type="checkbox"
                        checked={sensorFailSim}
                        onChange={e => setSensorFailSim(e.target.checked)}
                        className="rounded accent-amber-500 w-4 h-4"
                      />
                    </label>

                    <label className="flex items-center justify-between p-2 bg-slate-950/60 rounded-xl border border-slate-800 cursor-pointer">
                      <span className="text-slate-300">Тест аварии перегрева (&gt;39.5°C)</span>
                      <input
                        type="checkbox"
                        checked={overheatSim}
                        onChange={e => setOverheatSim(e.target.checked)}
                        className="rounded accent-red-500 w-4 h-4"
                      />
                    </label>
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* Таб: Прошивка ESP32 (Код C++ / .ino / PlatformIO) */}
        {activeTab === 'code' && (
          <div className="space-y-6">
            {/* Панель настройки кода */}
            <div className="bg-slate-900 border border-slate-800 rounded-2xl p-5 shadow-lg">
              <div className="flex flex-wrap items-center justify-between gap-4 border-b border-slate-800 pb-4 mb-4">
                <div>
                  <h2 className="text-base font-bold text-white flex items-center gap-2">
                    <Layers className="w-5 h-5 text-amber-500" />
                    Конфигуратор прошивки ESP32 v2.0
                  </h2>
                  <p className="text-xs text-slate-400 mt-0.5">
                    Настройте пины и параметры под вашу плату, скопируйте код или скачайте готовый .ino файл
                  </p>
                </div>

                <div className="flex items-center gap-2">
                  <button
                    onClick={handleCopyCode}
                    className="px-3.5 py-2 bg-slate-800 hover:bg-slate-700 text-slate-200 rounded-xl text-xs font-semibold flex items-center gap-1.5 transition border border-slate-700"
                  >
                    {copiedCode ? <Check className="w-4 h-4 text-emerald-400" /> : <Copy className="w-4 h-4" />}
                    <span>{copiedCode ? 'Скопировано!' : 'Копировать код'}</span>
                  </button>

                  <button
                    onClick={handleDownloadIno}
                    className="px-3.5 py-2 bg-amber-500 hover:bg-amber-400 text-slate-950 rounded-xl text-xs font-bold flex items-center gap-1.5 transition shadow"
                  >
                    <Download className="w-4 h-4" />
                    <span>Скачать .ino</span>
                  </button>
                </div>
              </div>

              {/* Интерактивные параметры прошивки */}
              <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-4 gap-4 text-xs">
                <div>
                  <label className="block text-slate-400 font-medium mb-1">Тип датчика температуры:</label>
                  <select
                    value={sensorType}
                    onChange={e => setSensorType(e.target.value as any)}
                    className="w-full bg-slate-950 border border-slate-800 rounded-xl px-3 py-2 text-slate-200"
                  >
                    <option value="DHT11">DHT11 (синий, ±1.0°C)</option>
                    <option value="DHT22">DHT22 / AM2302 (белый, ±0.2°C)</option>
                  </select>
                </div>

                <div>
                  <label className="block text-slate-400 font-medium mb-1">Пин реле нагревателя:</label>
                  <select
                    value={relayPin}
                    onChange={e => setRelayPin(e.target.value as any)}
                    className="w-full bg-slate-950 border border-slate-800 rounded-xl px-3 py-2 text-slate-200"
                  >
                    <option value="4">GPIO 4 (Рекомендуемый, без глитча)</option>
                    <option value="16">GPIO 16 (D2, оригинал репозитория)</option>
                    <option value="25">GPIO 25 (Альтернативный)</option>
                  </select>
                </div>

                <div>
                  <label className="block text-slate-400 font-medium mb-1">Имя Wi-Fi точки (SSID):</label>
                  <input
                    type="text"
                    value={wifiSsid}
                    onChange={e => setWifiSsid(e.target.value)}
                    className="w-full bg-slate-950 border border-slate-800 rounded-xl px-3 py-2 text-slate-200 font-mono"
                  />
                </div>

                <div>
                  <label className="block text-slate-400 font-medium mb-1">Пароль точки доступа:</label>
                  <input
                    type="text"
                    value={wifiPass}
                    onChange={e => setWifiPass(e.target.value)}
                    className="w-full bg-slate-950 border border-slate-800 rounded-xl px-3 py-2 text-slate-200 font-mono"
                  />
                </div>
              </div>
            </div>

            {/* Карточка ключевых улучшений v2.0 */}
            <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
              <div className="bg-slate-900/90 border border-slate-800 p-4 rounded-2xl">
                <div className="w-8 h-8 rounded-lg bg-emerald-500/10 text-emerald-400 flex items-center justify-center mb-2">
                  <ShieldAlert className="w-4 h-4" />
                </div>
                <h3 className="font-bold text-sm text-slate-200">1. Защита ULN2003 от перегрева</h3>
                <p className="text-xs text-slate-400 mt-1 leading-relaxed">
                  После поворота лотка все обмотки мотора 28BYJ-48 принудительно обесточиваются (<code className="text-emerald-400">releaseStepperOutputs()</code>). Чип и мотор не нагреваются!
                </p>
              </div>

              <div className="bg-slate-900/90 border border-slate-800 p-4 rounded-2xl">
                <div className="w-8 h-8 rounded-lg bg-amber-500/10 text-amber-400 flex items-center justify-center mb-2">
                  <Cpu className="w-4 h-4" />
                </div>
                <h3 className="font-bold text-sm text-slate-200">2. Память NVS Preferences</h3>
                <p className="text-xs text-slate-400 mt-1 leading-relaxed">
                  Уставка температуры, гистерезис, интервал переворота и текущий день инкубации сохраняются во Flash-память ESP32 и не сбрасываются при сбоях питания.
                </p>
              </div>

              <div className="bg-slate-900/90 border border-slate-800 p-4 rounded-2xl">
                <div className="w-8 h-8 rounded-lg bg-red-500/10 text-red-400 flex items-center justify-center mb-2">
                  <AlertTriangle className="w-4 h-4" />
                </div>
                <h3 className="font-bold text-sm text-slate-200">3. Аварийный термостат</h3>
                <p className="text-xs text-slate-400 mt-1 leading-relaxed">
                  Аварийное отключение нагревателя при T &gt; 39.5 °C даже в ручном режиме, защита при потере датчика DHT, автоматический маятник лотка влево/вправо.
                </p>
              </div>
            </div>

            {/* Окно с кодом C++ */}
            <div className="bg-slate-950 border border-slate-800 rounded-2xl overflow-hidden shadow-2xl">
              <div className="bg-slate-900 px-4 py-2.5 border-b border-slate-800 flex items-center justify-between text-xs text-slate-400">
                <div className="flex items-center gap-2">
                  <span className="w-3 h-3 rounded-full bg-red-500/80 inline-block" />
                  <span className="w-3 h-3 rounded-full bg-amber-500/80 inline-block" />
                  <span className="w-3 h-3 rounded-full bg-emerald-500/80 inline-block" />
                  <span className="font-mono text-slate-300 ml-2">incubator_web.ino</span>
                </div>
                <div className="text-[11px] font-mono text-amber-400/90">Arduino C++ / PlatformIO</div>
              </div>

              <pre className="p-4 text-xs font-mono text-slate-300 overflow-x-auto max-h-[600px] leading-relaxed select-all">
                {generateFirmwareCode()}
              </pre>
            </div>
          </div>
        )}

        {/* Таб: Схема подключения */}
        {activeTab === 'wiring' && (
          <div className="space-y-6">
            <div className="bg-slate-900 border border-slate-800 rounded-2xl p-6 shadow-lg">
              <h2 className="text-base font-bold text-white flex items-center gap-2 mb-2">
                <Zap className="w-5 h-5 text-amber-500" />
                Схема подключения модулей к плате ESP32 (DevKit 30/38 pin)
              </h2>
              <p className="text-xs text-slate-400 mb-6">
                Все модули проверены на совместимость с уровнями напряжения 3.3 В и 5 В.
              </p>

              {/* Таблица подключения */}
              <div className="overflow-x-auto">
                <table className="w-full text-left text-xs border-collapse">
                  <thead>
                    <tr className="border-b border-slate-800 bg-slate-950/60 text-slate-400">
                      <th className="py-3 px-4 font-semibold">Модуль</th>
                      <th className="py-3 px-4 font-semibold">Вывод модуля</th>
                      <th className="py-3 px-4 font-semibold">Пин ESP32</th>
                      <th className="py-3 px-4 font-semibold">Питание</th>
                      <th className="py-3 px-4 font-semibold">Примечание</th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-slate-800/60 font-mono text-slate-300">
                    <tr className="hover:bg-slate-800/20">
                      <td className="py-3 px-4 font-sans font-semibold text-amber-400">Датчик DHT11 / DHT22</td>
                      <td className="py-3 px-4">DATA / OUT</td>
                      <td className="py-3 px-4 font-bold text-emerald-400">GPIO 13</td>
                      <td className="py-3 px-4">3.3V или 5V + GND</td>
                      <td className="py-3 px-4 font-sans text-slate-400">Подтяжка 10кОм к 3.3V (если датчик без платы)</td>
                    </tr>
                    <tr className="hover:bg-slate-800/20">
                      <td className="py-3 px-4 font-sans font-semibold text-amber-400">Модуль Реле (нагреватель)</td>
                      <td className="py-3 px-4">IN</td>
                      <td className="py-3 px-4 font-bold text-emerald-400">GPIO 4 (или 16)</td>
                      <td className="py-3 px-4">5V (VCC) + GND</td>
                      <td className="py-3 px-4 font-sans text-slate-400">Активный LOW (0 = включено, 1 = выключено)</td>
                    </tr>
                    <tr className="hover:bg-slate-800/20">
                      <td className="py-3 px-4 font-sans font-semibold text-sky-400">OLED Дисплей SSD1306</td>
                      <td className="py-3 px-4">SDA<br />SCL</td>
                      <td className="py-3 px-4 font-bold text-emerald-400">GPIO 21<br />GPIO 22</td>
                      <td className="py-3 px-4">3.3V (VCC) + GND</td>
                      <td className="py-3 px-4 font-sans text-slate-400">I2C адрес 0x3C, скорость 400 кГц</td>
                    </tr>
                    <tr className="hover:bg-slate-800/20">
                      <td className="py-3 px-4 font-sans font-semibold text-purple-400">Драйвер ULN2003 (мотор лотка)</td>
                      <td className="py-3 px-4">IN1<br />IN2<br />IN3<br />IN4</td>
                      <td className="py-3 px-4 font-bold text-emerald-400">GPIO 19<br />GPIO 18<br />GPIO 5<br />GPIO 17</td>
                      <td className="py-3 px-4">5V–9V внешний БП + GND (общая!)</td>
                      <td className="py-3 px-4 font-sans text-slate-400">В v2.0 обмотки обесточиваются после шагов</td>
                    </tr>
                    <tr className="hover:bg-slate-800/20">
                      <td className="py-3 px-4 font-sans font-semibold text-red-400">Зуммер тревоги (опция)</td>
                      <td className="py-3 px-4">+ (Анод)</td>
                      <td className="py-3 px-4 font-bold text-emerald-400">GPIO 2</td>
                      <td className="py-3 px-4">GND</td>
                      <td className="py-3 px-4 font-sans text-slate-400">Пищит при критическом перегреве (&gt;39.5°C)</td>
                    </tr>
                  </tbody>
                </table>
              </div>

              {/* Советы по питанию и сборке */}
              <div className="mt-8 grid grid-cols-1 md:grid-cols-2 gap-4">
                <div className="bg-slate-950/70 p-4 rounded-xl border border-slate-800">
                  <h4 className="font-bold text-sm text-slate-200 mb-2 flex items-center gap-1.5">
                    <AlertTriangle className="w-4 h-4 text-amber-500" />
                    Важно: Питание шагового двигателя 28BYJ-48
                  </h4>
                  <p className="text-xs text-slate-400 leading-relaxed">
                    Никогда не питайте драйвер ULN2003 и мотор от пина 3.3V микроконтроллера! Ток мотора достигает 300–400 мА, что может вызвать перезагрузку ESP32 (Brownout detector). Питайте драйвер от отдельного 5V адаптера или пина VIN (при питании от качественного блока 5V 2A), обязательно объединив провод <strong className="text-white">GND</strong>.
                  </p>
                </div>

                <div className="bg-slate-950/70 p-4 rounded-xl border border-slate-800">
                  <h4 className="font-bold text-sm text-slate-200 mb-2 flex items-center gap-1.5">
                    <CheckCircle2 className="w-4 h-4 text-emerald-500" />
                    Почему выбран GPIO 4 для реле вместо GPIO 16
                  </h4>
                  <p className="text-xs text-slate-400 leading-relaxed">
                    Пин GPIO16 на многих платах ESP32 имеет внутреннюю подтяжку к земле при перезагрузке, из-за чего модуль реле с активным нулем (LOW) самопроизвольно кратковременно щелкал при подаче питания. Пин GPIO4 лишен этого недостатка и гарантирует полную безопасность ламп нагревателя.
                  </p>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* Таб: Режимы и руководство инкубации */}
        {activeTab === 'guide' && (
          <div className="space-y-6">
            <div className="bg-slate-900 border border-slate-800 rounded-2xl p-6 shadow-lg">
              <h2 className="text-base font-bold text-white flex items-center gap-2 mb-2">
                <Calendar className="w-5 h-5 text-amber-500" />
                Справочник температурных режимов инкубации
              </h2>
              <p className="text-xs text-slate-400 mb-6">
                Оптимальные параметры температуры, влажности и интервалов переворота для домашней птицы.
              </p>

              <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                {BIRD_PRESETS.filter(p => p.id !== 'custom').map(preset => (
                  <div key={preset.id} className="bg-slate-950/70 border border-slate-800 rounded-2xl p-4 space-y-3">
                    <div className="flex justify-between items-start">
                      <div>
                        <h3 className="font-bold text-sm text-amber-400">{preset.name}</h3>
                        <div className="text-xs text-slate-500">Срок инкубации: {preset.days} суток</div>
                      </div>
                      <button
                        onClick={() => {
                          applyPreset(preset);
                          setActiveTab('dashboard');
                        }}
                        className="px-2.5 py-1 bg-amber-500/10 hover:bg-amber-500/20 text-amber-400 border border-amber-500/30 rounded-lg text-xs font-semibold"
                      >
                        Применить
                      </button>
                    </div>

                    <div className="grid grid-cols-2 gap-2 text-xs">
                      <div className="bg-slate-900 p-2.5 rounded-xl border border-slate-800">
                        <div className="text-slate-400 text-[11px]">Основной период (дни 1–{preset.stopTurnDay - 1}):</div>
                        <div className="font-semibold text-slate-200 mt-0.5">
                          T: <span className="text-amber-400 font-mono">{preset.temp}°C</span> &middot; W: <span className="text-sky-400 font-mono">{preset.hum}%</span>
                        </div>
                        <div className="text-[10px] text-emerald-400 mt-1">Поворот: каждые 2–3 ч</div>
                      </div>

                      <div className="bg-slate-900 p-2.5 rounded-xl border border-slate-800">
                        <div className="text-slate-400 text-[11px]">Вывод (дни {preset.stopTurnDay}–{preset.days}):</div>
                        <div className="font-semibold text-slate-200 mt-0.5">
                          T: <span className="text-amber-400 font-mono">{preset.finalTemp}°C</span> &middot; W: <span className="text-sky-400 font-mono">{preset.finalHum}%</span>
                        </div>
                        <div className="text-[10px] text-red-400 mt-1">Поворот: СТОП!</div>
                      </div>
                    </div>

                    <p className="text-xs text-slate-400 leading-relaxed">
                      {preset.description}
                    </p>
                  </div>
                ))}
              </div>

              {/* Советы птицевода */}
              <div className="mt-6 p-4 rounded-xl bg-slate-950/60 border border-slate-800 space-y-2">
                <h4 className="font-semibold text-xs text-slate-200 flex items-center gap-1.5">
                  <Sparkles className="w-4 h-4 text-amber-400" />
                  Золотые правила успешной инкубации:
                </h4>
                <ul className="text-xs text-slate-400 list-disc list-inside space-y-1">
                  <li>Закладывайте яйца комнатной температуры (после 5–8 часов прогрева в помещении).</li>
                  <li>Обязательно проверяйте калибровку датчика DHT медицинским или проверенным ртутным термометром.</li>
                  <li>За 2–3 дня до вылупления лотки обязательно переводятся в горизонтальное положение, а автопереворот выключается.</li>
                  <li>При проклеве поддерживайте высокую влажность (65–80%), чтобы подскорлупная оболочка не присыхала к птенцам.</li>
                </ul>
              </div>
            </div>
          </div>
        )}
      </main>
    </div>
  );
}

// Мини-иконка дисплея
function TvIcon(props: React.SVGProps<SVGSVGElement>) {
  return (
    <svg
      {...props}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <rect width="20" height="15" x="2" y="7" rx="2" />
      <polyline points="17 2 12 7 7 2" />
    </svg>
  );
}
