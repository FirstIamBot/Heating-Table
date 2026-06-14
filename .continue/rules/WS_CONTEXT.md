# СПЕЦИФИКАЦИЯ ОБМЕНА ДАННЫМИ (WEBSOCKET / WEB-SERVER)

## 1. Сетевые параметры
- Веб-сервер работает на порту `80`.
- Точка доступа (AP): SSID `HeatTable`, IP `192.168.4.1`.
- Локальная сеть (STA): SSID `my_point`.
- WebSocket эндпоинт: `ws://<ESP_IP>/ws`.

## 2. Входящие JSON-пакеты от Клиента (Браузера) к ESP32
Все сообщения от JS приходят строкой, которая парсится в `handleWebSocketMessage` через `data_jsn`. Ключевой параметр — `"Start"`.

- **Стоп / Standby (`"Start": 0`)** — сброс в режим ожидания.
- **Старт теста PID (`"Start": 1`)**:
```json
  {"Start":1, "Kp":0.51, "Ki":0.0004, "Kd":0.0, "twoZone":1, "KpTracking":0.17, "KiTracking":0.0004, "KdTracking":0.0, "unitProg":600, "Curr_Temp":250, "minimize":10, "switchTemp":2}