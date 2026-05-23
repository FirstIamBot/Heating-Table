#!/usr/bin/env python3
"""
PID Controller Configuration Tool with WebSocket and Matplotlib
Инструмент для настройки PID-регулятора с WebSocket и Matplotlib

Подключается к ESP32 WebSocket-серверу и позволяет:
- Получать данные PID-регулятора в реальном времени
- Настраивать параметры (Kp, Ki, Kd, температура и т.д.)
- Визуализировать работу регулятора на графике
"""

import asyncio
import websockets
import json
import tkinter as tk
from tkinter import ttk, messagebox
import threading
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from collections import deque
import time

# ======================== Конфигурация ========================
CONFIG = {
    'buffer_size': 200,          # Размер буфера данных
    'ws_reconnect_delay': 3,     # Задержка переподключения в сек
    'graph_update_interval': 500, # Интервал обновления графика в мс
}

# ======================== Класс WebSocket-клиента ========================
class WebSocketClient:
    def __init__(self, uri, on_message_callback):
        self.uri = uri
        self.on_message = on_message_callback
        self.ws = None
        self.connected = False
        self.loop = None
        self.thread = None
        
    async def connect_and_listen(self):
        """Подключение к WebSocket и прослушивание сообщений"""
        retry_count = 0
        max_retries = 5
        
        while True:
            try:
                print(f"Подключение к {self.uri}...")
                async with websockets.connect(self.uri) as ws:
                    self.ws = ws
                    self.connected = True
                    retry_count = 0
                    print("✓ Подключено к WebSocket-серверу")
                    
                    try:
                        async for message in ws:
                            try:
                                data = json.loads(message)
                                self.on_message(data)
                            except json.JSONDecodeError:
                                print(f"✗ Ошибка парсинга JSON: {message}")
                    except websockets.exceptions.ConnectionClosed:
                        print("✗ Соединение закрыто сервером")
                        
            except (ConnectionRefusedError, OSError) as e:
                retry_count += 1
                wait_time = min(CONFIG['ws_reconnect_delay'] * retry_count, 30)
                print(f"✗ Ошибка подключения: {e}")
                print(f"Переподключение через {wait_time} сек (попытка {retry_count}/{max_retries})...")
                await asyncio.sleep(wait_time)
                
            except Exception as e:
                print(f"✗ Неожиданная ошибка: {e}")
                await asyncio.sleep(CONFIG['ws_reconnect_delay'])
            finally:
                self.connected = False
    
    async def send_message(self, data):
        """Отправка сообщения серверу"""
        if self.ws and self.connected:
            try:
                await self.ws.send(json.dumps(data))
                print(f"→ Отправлено: {data}")
            except Exception as e:
                print(f"✗ Ошибка отправки: {e}")
        else:
            print("✗ WebSocket не подключен")
    
    def start(self):
        """Запуск клиента в отдельном потоке"""
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()
    
    def _run_loop(self):
        """Запуск event loop в отдельном потоке"""
        asyncio.set_event_loop(self.loop)
        self.loop.run_until_complete(self.connect_and_listen())
    
    def send_async(self, data):
        """Отправка сообщения асинхронно"""
        if self.loop:
            asyncio.run_coroutine_threadsafe(self.send_message(data), self.loop)
    
    def stop(self):
        """Остановка клиента"""
        if self.loop:
            self.loop.call_soon_threadsafe(self.loop.stop)

# ======================== Основное приложение ========================
class PIDControllerApp:
    def __init__(self, root, ws_uri="ws://192.168.1.100/ws"):
        self.root = root
        self.root.title("PID Controller Configuration Tool")
        self.root.geometry("1400x800")
        
        self.ws_uri = ws_uri
        self.ws_client = None
        self.connected = False
        
        # Буферы для данных
        self.data_buffer = {
            'time': deque(maxlen=CONFIG['buffer_size']),
            'vCurrT': deque(maxlen=CONFIG['buffer_size']),
            'vMesT': deque(maxlen=CONFIG['buffer_size']),
            'vComputePID': deque(maxlen=CONFIG['buffer_size']),
            'vgetPower': deque(maxlen=CONFIG['buffer_size']),
            'vtProg': deque(maxlen=CONFIG['buffer_size']),
        }
        self.start_time = None
        
        # Создание UI
        self.create_ui()
        
        # Запуск WebSocket-клиента
        self.connect_ws()
        
    def create_ui(self):
        """Создание пользовательского интерфейса"""
        # Главный контейнер
        main_frame = ttk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # ===== Левая панель (управление) =====
        left_frame = ttk.Frame(main_frame, width=300)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, padx=(0, 5))
        
        # Статус подключения
        status_frame = ttk.LabelFrame(left_frame, text="Статус", padding=10)
        status_frame.pack(fill=tk.X, pady=(0, 10))
        
        self.status_label = ttk.Label(status_frame, text="⚫ Не подключено", foreground="red", font=("Arial", 10, "bold"))
        self.status_label.pack()
        
        self.ip_entry = ttk.Entry(status_frame, width=30)
        self.ip_entry.insert(0, self.ws_uri.replace("ws://", "").replace("/ws", ""))
        self.ip_entry.pack(pady=5)
        
        ttk.Button(status_frame, text="Подключить", command=self.reconnect_ws).pack(pady=5)
        
        # ===== PID параметры =====
        pid_frame = ttk.LabelFrame(left_frame, text="PID Параметры", padding=10)
        pid_frame.pack(fill=tk.X, pady=(0, 10))
        
        # Kp
        ttk.Label(pid_frame, text="Kp:").pack(anchor=tk.W)
        self.kp_var = tk.DoubleVar(value=25.0)
        self.kp_scale = ttk.Scale(pid_frame, from_=0, to=100, variable=self.kp_var, orient=tk.HORIZONTAL)
        self.kp_scale.pack(fill=tk.X)
        self.kp_label = ttk.Label(pid_frame, text="25.00")
        self.kp_label.pack(anchor=tk.W)
        self.kp_var.trace_add("write", lambda *args: self.update_pid_labels())
        
        # Ki
        ttk.Label(pid_frame, text="Ki:").pack(anchor=tk.W, pady=(10, 0))
        self.ki_var = tk.DoubleVar(value=0.0)
        self.ki_scale = ttk.Scale(pid_frame, from_=0, to=10, variable=self.ki_var, orient=tk.HORIZONTAL)
        self.ki_scale.pack(fill=tk.X)
        self.ki_label = ttk.Label(pid_frame, text="0.00")
        self.ki_label.pack(anchor=tk.W)
        self.ki_var.trace_add("write", lambda *args: self.update_pid_labels())
        
        # Kd
        ttk.Label(pid_frame, text="Kd:").pack(anchor=tk.W, pady=(10, 0))
        self.kd_var = tk.DoubleVar(value=0.0)
        self.kd_scale = ttk.Scale(pid_frame, from_=0, to=100, variable=self.kd_var, orient=tk.HORIZONTAL)
        self.kd_scale.pack(fill=tk.X)
        self.kd_label = ttk.Label(pid_frame, text="0.00")
        self.kd_label.pack(anchor=tk.W)
        self.kd_var.trace_add("write", lambda *args: self.update_pid_labels())
        
        # ===== Температура и параметры =====
        temp_frame = ttk.LabelFrame(left_frame, text="Параметры", padding=10)
        temp_frame.pack(fill=tk.X, pady=(0, 10))
        
        ttk.Label(temp_frame, text="Температура (°C):").pack(anchor=tk.W)
        self.temp_var = tk.DoubleVar(value=100.0)
        self.temp_spinbox = ttk.Spinbox(temp_frame, from_=0, to=300, textvariable=self.temp_var, width=15)
        self.temp_spinbox.pack(anchor=tk.W)
        
        ttk.Label(temp_frame, text="Minimize (%):").pack(anchor=tk.W, pady=(10, 0))
        self.minimize_var = tk.IntVar(value=12)
        self.minimize_spinbox = ttk.Spinbox(temp_frame, from_=0, to=100, textvariable=self.minimize_var, width=15)
        self.minimize_spinbox.pack(anchor=tk.W)
        
        ttk.Label(temp_frame, text="Switch Temp (°C):").pack(anchor=tk.W, pady=(10, 0))
        self.switch_var = tk.IntVar(value=15)
        self.switch_spinbox = ttk.Spinbox(temp_frame, from_=0, to=50, textvariable=self.switch_var, width=15)
        self.switch_spinbox.pack(anchor=tk.W)
        
        ttk.Label(temp_frame, text="Unit Prog (sec):").pack(anchor=tk.W, pady=(10, 0))
        self.unitprog_var = tk.IntVar(value=600)
        self.unitprog_spinbox = ttk.Spinbox(temp_frame, from_=100, to=1000, textvariable=self.unitprog_var, width=15)
        self.unitprog_spinbox.pack(anchor=tk.W)
        
        # ===== Кнопки управления =====
        control_frame = ttk.LabelFrame(left_frame, text="Управление", padding=10)
        control_frame.pack(fill=tk.X, pady=(0, 10))
        
        ttk.Button(control_frame, text="▶ Запустить (TEST)", command=self.start_test).pack(fill=tk.X, pady=5)
        ttk.Button(control_frame, text="⏹ Остановить", command=self.stop_test).pack(fill=tk.X, pady=5)
        
        # ===== Правая панель (график) =====
        right_frame = ttk.Frame(main_frame)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        # Создание Matplotlib фигуры
        self.fig, (self.ax1, self.ax2) = plt.subplots(2, 1, figsize=(10, 6))
        self.fig.tight_layout(pad=3)
        
        # График 1: Температура
        self.ax1.set_title("Температура (°C)")
        self.ax1.set_ylabel("Температура")
        self.ax1.grid(True, alpha=0.3)
        self.line_measured, = self.ax1.plot([], [], label="Измеренная (vMesT)", color='red', linewidth=2)
        self.line_setpoint, = self.ax1.plot([], [], label="Уставка (vCurrT)", color='blue', linewidth=2)
        self.ax1.legend(loc='upper left')
        
        # График 2: PID выход
        self.ax2.set_title("PID регулятор")
        self.ax2.set_xlabel("Время (сек)")
        self.ax2.set_ylabel("Выход (%)")
        self.ax2.grid(True, alpha=0.3)
        self.line_output, = self.ax2.plot([], [], label="PID Output (vComputePID)", color='green', linewidth=2)
        self.line_power, = self.ax2.plot([], [], label="Power (vgetPower)", color='orange', linewidth=1, linestyle='--')
        self.ax2.set_ylim([0, 100])
        self.ax2.legend(loc='upper left')
        
        # Встраивание Matplotlib в tkinter
        self.canvas = FigureCanvasTkAgg(self.fig, master=right_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # Запуск обновления графика
        self.update_graph()
    
    def update_pid_labels(self):
        """Обновление отображения значений PID"""
        self.kp_label.config(text=f"{self.kp_var.get():.2f}")
        self.ki_label.config(text=f"{self.ki_var.get():.2f}")
        self.kd_label.config(text=f"{self.kd_var.get():.2f}")
    
    def connect_ws(self):
        """Подключение к WebSocket"""
        def on_message(data):
            self.handle_ws_message(data)
        
        ip = self.ip_entry.get()
        self.ws_uri = f"ws://{ip}/ws"
        self.ws_client = WebSocketClient(self.ws_uri, on_message)
        self.ws_client.start()
        self.update_connection_status()
    
    def reconnect_ws(self):
        """Переподключение"""
        if self.ws_client:
            self.ws_client.stop()
        self.connect_ws()
    
    def update_connection_status(self):
        """Проверка статуса подключения"""
        connected = self.ws_client and self.ws_client.connected
        if connected != self.connected:
            self.connected = connected
            if connected:
                self.status_label.config(text="🟢 Подключено", foreground="green")
            else:
                self.status_label.config(text="🔴 Не подключено", foreground="red")
        self.root.after(500, self.update_connection_status)
    
    def handle_ws_message(self, data):
        """Обработка сообщений от сервера"""
        try:
            if self.start_time is None:
                self.start_time = time.time()
            
            elapsed_time = time.time() - self.start_time
            
            self.data_buffer['time'].append(elapsed_time)
            self.data_buffer['vCurrT'].append(float(data.get('vCurrT', 0)))
            self.data_buffer['vMesT'].append(float(data.get('vMesT', 0)))
            self.data_buffer['vComputePID'].append(float(data.get('vComputePID', 0)))
            self.data_buffer['vgetPower'].append(float(data.get('vgetPower', 0)))
            self.data_buffer['vtProg'].append(float(data.get('vtProg', 0)))
        except Exception as e:
            print(f"✗ Ошибка обработки данных: {e}")

    def reset_graph_data(self):
        """Сброс буферов и графика перед новым запуском"""
        for key in self.data_buffer:
            self.data_buffer[key].clear()

        self.start_time = None

        self.line_measured.set_data([], [])
        self.line_setpoint.set_data([], [])
        self.line_output.set_data([], [])
        self.line_power.set_data([], [])

        self.ax1.set_xlim([0, 10])
        self.ax1.set_ylim([0, 100])
        self.ax2.set_xlim([0, 10])
        self.ax2.set_ylim([0, 100])

        self.canvas.draw_idle()
    
    def update_graph(self):
        """Обновление графика"""
        try:
            if len(self.data_buffer['time']) > 0:
                # График 1: Температура
                self.line_measured.set_data(self.data_buffer['time'], self.data_buffer['vMesT'])
                self.line_setpoint.set_data(self.data_buffer['time'], self.data_buffer['vCurrT'])
                
                if len(self.data_buffer['time']) > 1:
                    self.ax1.set_xlim([0, max(self.data_buffer['time'])])
                    temps = list(self.data_buffer['vMesT']) + list(self.data_buffer['vCurrT'])
                    if temps:
                        self.ax1.set_ylim([min(temps) - 10, max(temps) + 10])
                
                # График 2: PID выход
                self.line_output.set_data(self.data_buffer['time'], self.data_buffer['vComputePID'])
                self.line_power.set_data(self.data_buffer['time'], self.data_buffer['vgetPower'])
                
                if len(self.data_buffer['time']) > 1:
                    self.ax2.set_xlim([0, max(self.data_buffer['time'])])
                
                self.canvas.draw_idle()
        except Exception as e:
            print(f"✗ Ошибка обновления графика: {e}")
        
        self.root.after(CONFIG['graph_update_interval'], self.update_graph)
    
    def start_test(self):
        """Запуск тестирования"""
        if not self.connected:
            messagebox.showwarning("Предупреждение", "WebSocket не подключен!")
            return
        
        self.reset_graph_data()
        
        command = {
            "Start": 1,
            "Kp": self.kp_var.get(),
            "Ki": self.ki_var.get(),
            "Kd": self.kd_var.get(),
            "Curr_Temp": self.temp_var.get(),
            "minimize": self.minimize_var.get(),
            "switchTemp": self.switch_var.get(),
            "unitProg": self.unitprog_var.get(),
        }
        self.ws_client.send_async(command)
        messagebox.showinfo("Информация", "Команда запуска отправлена!")
    
    def stop_test(self):
        """Остановка тестирования"""
        if not self.connected:
            messagebox.showwarning("Предупреждение", "WebSocket не подключен!")
            return
        
        command = {"Start": 0}
        self.ws_client.send_async(command)
        messagebox.showinfo("Информация", "Команда остановки отправлена!")

# ======================== Главная функция ========================
if __name__ == "__main__":
    root = tk.Tk()
    app = PIDControllerApp(root, ws_uri="ws://192.168.1.100/ws")
    root.mainloop()