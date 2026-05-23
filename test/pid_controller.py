#!/home/gena/Документы/PlatformIO/Projects/Heating Table/.venv/bin/python
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
import matplotlib
matplotlib.use('TkAgg')
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from collections import deque
import time
import queue
import numpy as np

# ======================== Конфигурация ========================
CONFIG = {
    'buffer_size': 200,          # Размер буфера данных
    'ws_reconnect_delay': 3,     # Задержка переподключения в сек
    'graph_update_interval': 500, # Интервал обновления графика в мс
}

# ======================== Класс WebSocket-клиента ========================
class WebSocketClient(threading.Thread):
    def __init__(self, uri, on_message_callback):
        super().__init__(daemon=True)
        self.uri = uri
        self.on_message = on_message_callback
        self.connected = False
        self.ws = None
        self.send_queue = queue.Queue()
        self.stop_event = threading.Event()
        
    def run(self):
        """Запуск WebSocket клиента в отдельном потоке"""
        asyncio.run(self.connect_and_listen())
    
    async def connect_and_listen(self):
        """Подключение к WebSocket и прослушивание сообщений"""
        retry_count = 0
        max_retries = 5
        
        while not self.stop_event.is_set():
            try:
                print(f"Подключение к {self.uri}...")
                async with websockets.connect(self.uri) as ws:
                    self.ws = ws
                    self.connected = True
                    retry_count = 0
                    print("✓ Подключено к WebSocket-серверу")
                    
                    # Запуск задачи для отправки сообщений
                    send_task = asyncio.create_task(self.send_messages_task())
                    
                    try:
                        async for message in ws:
                            try:
                                data = json.loads(message)
                                self.on_message(data)
                            except json.JSONDecodeError:
                                print(f"✗ Ошибка парсинга JSON: {message}")
                    except websockets.exceptions.ConnectionClosed:
                        print("✗ Соединение закрыто сервером")
                    finally:
                        send_task.cancel()
                        try:
                            await send_task
                        except asyncio.CancelledError:
                            pass
                        
            except (ConnectionRefusedError, OSError) as e:
                retry_count += 1
                wait_time = min(CONFIG['ws_reconnect_delay'] * retry_count, 30)
                print(f"✗ Ошибка подключения: {e}")
                print(f"Переподключение через {wait_time} сек...")
                await asyncio.sleep(wait_time)
                
            except Exception as e:
                print(f"✗ Неожиданная ошибка: {e}")
                await asyncio.sleep(CONFIG['ws_reconnect_delay'])
            finally:
                self.connected = False
    
    async def send_messages_task(self):
        """Асинхронная задача для отправки сообщений из очереди"""
        while not self.stop_event.is_set():
            try:
                # Попытка получить сообщение из очереди (неблокирующая)
                data = self.send_queue.get_nowait()
                if self.ws and self.connected:
                    try:
                        await self.ws.send(json.dumps(data))
                        print(f"→ Отправлено: {data}")
                    except Exception as e:
                        print(f"✗ Ошибка отправки: {e}")
                else:
                    print("✗ WebSocket не подключен")
            except queue.Empty:
                await asyncio.sleep(0.1)
            except Exception as e:
                print(f"✗ Ошибка в send_messages_task: {e}")
                await asyncio.sleep(0.1)
    
    def send_async(self, data):
        """Отправка сообщения асинхронно через очередь"""
        self.send_queue.put(data)
    
    def stop(self):
        """Остановка клиента"""
        self.stop_event.set()

# ======================== Основное приложение ========================
class PIDControllerApp:
    def __init__(self, root, ws_uri="ws://192.168.1.100/ws"):
        self.root = root
        self.root.title("PID Controller Configuration Tool")
        self.root.geometry("1400x800")
        
        self.ws_uri = ws_uri
        self.ws_client = None
        self.connected = False
        self.incoming_data = queue.Queue()
        
        # Флаг для автонастройки CHR
        self.auto_tuning = False
        self.auto_tune_start_time = None
        self.auto_tune_max_duration = 120  # максимум 120 секунд
        
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
        
        # Обработка закрытия окна
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        
    def create_ui(self):
        """Создание пользовательского интерфейса"""
        # Главный контейнер
        main_frame = ttk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # ===== Левая панель (управление) =====
        left_frame = ttk.Frame(main_frame, width=300)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, padx=(0, 5))
        left_frame.pack_propagate(False)
        
        # Статус подключения
        status_frame = ttk.LabelFrame(left_frame, text="Статус", padding=10)
        status_frame.pack(fill=tk.X, pady=(0, 10))
        
        self.status_label = ttk.Label(status_frame, text="🔴 Не подключено", foreground="red", font=("Arial", 10, "bold"))
        self.status_label.pack()
        
        self.ip_entry = ttk.Entry(status_frame, width=30)
        self.ip_entry.insert(0, self.ws_uri.replace("ws://", "").replace("/ws", ""))
        self.ip_entry.pack(pady=5)
        
        ttk.Button(status_frame, text="Подключить", command=self.reconnect_ws).pack(pady=5)
        
        # ===== PID параметры =====
        pid_frame = ttk.LabelFrame(left_frame, text="PID Параметры", padding=10)
        pid_frame.pack(fill=tk.X, pady=(0, 10))
        
        # Kp
        ttk.Label(pid_frame, text="Kp (ручной ввод):").pack(anchor=tk.W)
        self.kp_var = tk.DoubleVar(value=25.0)
        self.kp_spinbox = ttk.Spinbox(
            pid_frame,
            from_=0.0,
            to=100.0,
            increment=0.1,
            textvariable=self.kp_var,
            width=15,
        )
        self.kp_spinbox.pack(anchor=tk.W)
        
        # Ki
        ttk.Label(pid_frame, text="Ki (ручной ввод):").pack(anchor=tk.W, pady=(10, 0))
        self.ki_var = tk.DoubleVar(value=0.0)
        self.ki_spinbox = ttk.Spinbox(
            pid_frame,
            from_=0.0,
            to=10.0,
            increment=0.01,
            textvariable=self.ki_var,
            width=15,
        )
        self.ki_spinbox.pack(anchor=tk.W)
        
        # Kd
        ttk.Label(pid_frame, text="Kd (ручной ввод):").pack(anchor=tk.W, pady=(10, 0))
        self.kd_var = tk.DoubleVar(value=0.0)
        self.kd_spinbox = ttk.Spinbox(
            pid_frame,
            from_=0.0,
            to=100.0,
            increment=0.1,
            textvariable=self.kd_var,
            width=15,
        )
        self.kd_spinbox.pack(anchor=tk.W)

        # ===== Двухзонная регулировка =====
        zone_frame = ttk.LabelFrame(left_frame, text="Двухзонная регулировка", padding=10)
        zone_frame.pack(fill=tk.X, pady=(0, 10))

        self.two_zone_enabled = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            zone_frame,
            text="Включить 2-зонный режим",
            variable=self.two_zone_enabled,
        ).pack(anchor=tk.W)

        ttk.Label(zone_frame, text="Дальняя зона (разгон)").pack(anchor=tk.W, pady=(8, 0))
        far_row = ttk.Frame(zone_frame)
        far_row.pack(fill=tk.X)
        self.kp_far_var = tk.DoubleVar(value=25.0)
        self.ki_far_var = tk.DoubleVar(value=0.0)
        self.kd_far_var = tk.DoubleVar(value=0.0)
        ttk.Label(far_row, text="Kp").pack(side=tk.LEFT)
        ttk.Spinbox(far_row, from_=0.0, to=100.0, increment=0.1, textvariable=self.kp_far_var, width=8).pack(side=tk.LEFT, padx=(2, 6))
        ttk.Label(far_row, text="Ki").pack(side=tk.LEFT)
        ttk.Spinbox(far_row, from_=0.0, to=10.0, increment=0.01, textvariable=self.ki_far_var, width=8).pack(side=tk.LEFT, padx=(2, 6))
        ttk.Label(far_row, text="Kd").pack(side=tk.LEFT)
        ttk.Spinbox(far_row, from_=0.0, to=100.0, increment=0.1, textvariable=self.kd_far_var, width=8).pack(side=tk.LEFT, padx=(2, 0))

        ttk.Label(zone_frame, text="Ближняя зона (подход к уставке)").pack(anchor=tk.W, pady=(8, 0))
        near_row = ttk.Frame(zone_frame)
        near_row.pack(fill=tk.X)
        self.kp_near_var = tk.DoubleVar(value=12.0)
        self.ki_near_var = tk.DoubleVar(value=0.0)
        self.kd_near_var = tk.DoubleVar(value=0.0)
        ttk.Label(near_row, text="Kp").pack(side=tk.LEFT)
        ttk.Spinbox(near_row, from_=0.0, to=100.0, increment=0.1, textvariable=self.kp_near_var, width=8).pack(side=tk.LEFT, padx=(2, 6))
        ttk.Label(near_row, text="Ki").pack(side=tk.LEFT)
        ttk.Spinbox(near_row, from_=0.0, to=10.0, increment=0.01, textvariable=self.ki_near_var, width=8).pack(side=tk.LEFT, padx=(2, 6))
        ttk.Label(near_row, text="Kd").pack(side=tk.LEFT)
        ttk.Spinbox(near_row, from_=0.0, to=100.0, increment=0.1, textvariable=self.kd_near_var, width=8).pack(side=tk.LEFT, padx=(2, 0))
        
        # ===== Температура и параметры =====
        temp_frame = ttk.LabelFrame(left_frame, text="Параметры", padding=10)
        temp_frame.pack(fill=tk.X, pady=(0, 10))
        
        ttk.Label(temp_frame, text="Температура (°C):").pack(anchor=tk.W)
        self.temp_var = tk.DoubleVar(value=100.0)
        self.temp_spinbox = ttk.Spinbox(temp_frame, from_=0, to=400, textvariable=self.temp_var, width=15)
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
        ttk.Button(control_frame, text="⚙ CHR", command=self.auto_tune_chr).pack(fill=tk.X, pady=5)
        ttk.Button(control_frame, text="⚙ PI инерционный (2 зоны)", command=self.tune_inertial_pi).pack(fill=tk.X, pady=5)
        
        # ===== Правая панель (график) =====
        right_frame = ttk.Frame(main_frame)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        # Создание Matplotlib фигуры без pyplot manager (стабильнее для Tk)
        self.fig = Figure(figsize=(10, 6), tight_layout={'pad': 3})
        self.ax1 = self.fig.add_subplot(2, 1, 1)
        self.ax2 = self.fig.add_subplot(2, 1, 2)
        
        # График 1: Температура
        self.ax1.set_title("Температура (°C)")
        self.ax1.set_ylabel("Температура")
        self.ax1.grid(True, alpha=0.3)
        self.line_measured, = self.ax1.plot([], [], label="Измеренная (vMesT)", color='red', linewidth=2)
        self.line_setpoint, = self.ax1.plot([], [], label="Уставка (vCurrT)", color='blue', linewidth=2)
        self.ax1.legend(loc='upper left')
        # Текстовый элемент для отображения текущей температуры и крутизны
        self.temp_info_text = self.ax1.text(0.98, 0.98, '', transform=self.ax1.transAxes,
                                            fontsize=10, verticalalignment='top', horizontalalignment='right',
                                            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
        
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
    
    def connect_ws(self):
        """Подключение к WebSocket"""
        def on_message(data):
            # Передаем данные в UI-поток через очередь, чтобы избежать гонок потоков.
            self.incoming_data.put(data)
        
        ip = self.ip_entry.get()
        self.ws_uri = f"ws://{ip}/ws"
        self.ws_client = WebSocketClient(self.ws_uri, on_message)
        self.ws_client.start()
        self.update_connection_status()
    
    def reconnect_ws(self):
        """Переподключение"""
        if self.ws_client:
            self.ws_client.stop()
            self.ws_client.join(timeout=2)
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
            # Служебные пакеты Start/Stop не содержат телеметрию для графика.
            if not any(k in data for k in ("vCurrT", "vMesT", "vComputePID", "vgetPower", "vtProg")):
                return

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

    def drain_incoming_data(self):
        """Перенос входящих WS-сообщений из очереди в буферы графика."""
        while True:
            try:
                data = self.incoming_data.get_nowait()
            except queue.Empty:
                break
            self.handle_ws_message(data)

    def reset_plot_data(self):
        """Очистка буферов графика при новом запуске теста."""
        for key in self.data_buffer:
            self.data_buffer[key].clear()
        self.start_time = None

    def _safe_get_float(self, var, field_name):
        """Безопасно считывает число из Tk-переменной, поддерживая ',' и '.'."""
        try:
            raw = str(self.root.tk.globalgetvar(var._name)).strip()
        except Exception:
            messagebox.showerror("Ошибка ввода", f"Не удалось прочитать поле: {field_name}")
            return None

        normalized = raw.replace(' ', '').replace(',', '.')
        if normalized == "":
            messagebox.showerror("Ошибка ввода", f"Поле '{field_name}' пустое")
            return None

        try:
            return float(normalized)
        except ValueError:
            messagebox.showerror(
                "Ошибка ввода",
                f"Поле '{field_name}' должно быть числом.\n"
                f"Получено: '{raw}'\n"
                "Используйте формат вида 0.34 или 0,34"
            )
            return None

    def _safe_get_int(self, var, field_name):
        """Безопасно считывает целое значение из Tk-переменной."""
        value = self._safe_get_float(var, field_name)
        if value is None:
            return None
        return int(value)
    
    def calculate_steepness(self):
        """Вычисление крутизны температуры (°C/сек)."""
        if len(self.data_buffer['time']) < 2 or self.data_buffer['time'][0] == self.data_buffer['time'][-1]:
            return 0.0
        
        # Берем последние 10 точек для более стабильного расчета
        time_points = list(self.data_buffer['time'])[-10:]
        temp_points = list(self.data_buffer['vMesT'])[-10:]
        
        if len(time_points) < 2:
            return 0.0
        
        time_diff = time_points[-1] - time_points[0]
        if time_diff == 0:
            return 0.0
        
        temp_diff = temp_points[-1] - temp_points[0]
        return temp_diff / time_diff
    
    def update_graph(self):
        """Обновление графика"""
        try:
            self.drain_incoming_data()

            if len(self.data_buffer['time']) > 0:
                # График 1: Температура
                self.line_measured.set_data(self.data_buffer['time'], self.data_buffer['vMesT'])
                self.line_setpoint.set_data(self.data_buffer['time'], self.data_buffer['vCurrT'])
                
                if len(self.data_buffer['time']) > 1:
                    self.ax1.set_xlim([0, max(self.data_buffer['time'])])
                    temps = list(self.data_buffer['vMesT']) + list(self.data_buffer['vCurrT'])
                    if temps:
                        self.ax1.set_ylim([min(temps) - 10, max(temps) + 10])
                
                # Обновление текста с текущей температурой и крутизной
                current_temp = self.data_buffer['vMesT'][-1] if self.data_buffer['vMesT'] else 0.0
                steepness = self.calculate_steepness()
                info_text = f"T: {current_temp:.1f}°C\nКрутизна: {steepness:.2f}°C/сек"
                self.temp_info_text.set_text(info_text)
                
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
        
        self.reset_plot_data()

        temp = self._safe_get_float(self.temp_var, "Температура")
        minimize = self._safe_get_int(self.minimize_var, "Minimize")
        switch_temp = self._safe_get_int(self.switch_var, "Switch Temp")
        unit_prog = self._safe_get_int(self.unitprog_var, "Unit Prog")
        if None in (temp, minimize, switch_temp, unit_prog):
            return
        
        two_zone = self.two_zone_enabled.get()
        if two_zone:
            kp_main = self._safe_get_float(self.kp_far_var, "Kp (дальняя зона)")
            ki_main = self._safe_get_float(self.ki_far_var, "Ki (дальняя зона)")
            kd_main = self._safe_get_float(self.kd_far_var, "Kd (дальняя зона)")
            kp_tracking = self._safe_get_float(self.kp_near_var, "KpTracking (ближняя зона)")
            ki_tracking = self._safe_get_float(self.ki_near_var, "KiTracking (ближняя зона)")
            kd_tracking = self._safe_get_float(self.kd_near_var, "KdTracking (ближняя зона)")
            if None in (kp_main, ki_main, kd_main, kp_tracking, ki_tracking, kd_tracking):
                return
        else:
            kp_main = self._safe_get_float(self.kp_var, "Kp")
            ki_main = self._safe_get_float(self.ki_var, "Ki")
            kd_main = self._safe_get_float(self.kd_var, "Kd")
            if None in (kp_main, ki_main, kd_main):
                return

        command = {
            "Start": 1,
            "Kp": kp_main,
            "Ki": ki_main,
            "Kd": kd_main,
            "Curr_Temp": temp,
            "minimize": minimize,
            "switchTemp": switch_temp,
            "unitProg": unit_prog,
        }

        # Параметры второй зоны отправляем отдельными полями.
        # Если прошивка их не поддерживает, они будут безопасно проигнорированы.
        if two_zone:
            command.update({
                "twoZone": 1,
                "KpTracking": kp_tracking,
                "KiTracking": ki_tracking,
                "KdTracking": kd_tracking,
            })

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
    
    def estimate_system_response(self):
        """Оценка параметров системы на основе переходной характеристики (L, T, K)"""
        temps = np.array(list(self.data_buffer['vMesT']))
        times = np.array(list(self.data_buffer['time']))
        
        if len(temps) < 10:
            return None, None, None

        # Начальное и текущее квазистационарное значения.
        head_count = min(5, len(temps))
        tail_count = min(10, len(temps))
        y0 = float(np.mean(temps[:head_count]))
        yss = float(np.mean(temps[-tail_count:]))
        delta_y = yss - y0

        # Для CHR нужна реакция объекта на шаг; если прирост слишком мал, данных пока недостаточно.
        if abs(delta_y) < 2.0:
            return None, None, None

        threshold_28 = y0 + 0.283 * delta_y
        threshold_63 = y0 + 0.632 * delta_y

        if delta_y > 0:
            idx_28 = np.where(temps >= threshold_28)[0]
            idx_63 = np.where(temps >= threshold_63)[0]
        else:
            idx_28 = np.where(temps <= threshold_28)[0]
            idx_63 = np.where(temps <= threshold_63)[0]

        if len(idx_28) == 0 or len(idx_63) == 0:
            return None, None, None

        t_28 = float(times[idx_28[0]])
        t_63 = float(times[idx_63[0]])

        if t_63 <= t_28:
            return None, None, None

        # Расчет времени задержки L и постоянной времени T
        L = 1.5 * (t_63 - t_28)  # Время задержки
        T = t_63 - L              # Постоянная времени

        if L <= 0 or T <= 0:
            return None, None, None

        # K - коэффициент усиления процесса (для единичного шага входа).
        K = abs(delta_y)
        return L, T, K
    
    def chr_tuning(self, L, T, K):
        """Расчет коэффициентов PID методом CHR (0% overshoot)"""
        if L == 0 or T == 0 or K == 0:
            return None, None, None

        # CHR (Chien-Hrones-Reswick), настройка PID для 0% перерегулирования
        # Используем классические коэффициенты для модели FOPDT с параметрами L, T, K.
        Kp = 0.6 * (T / (K * L))
        Ti = T
        Td = 0.5 * L

        Ki = Kp / Ti
        Kd = Kp * Td
        
        return Kp, Ki, Kd

    def imc_pi_tuning(self, L, T, K, lam):
        """IMC-PI настройка для объектов с запаздыванием (FOPDT).

        Формулы для PI:
          Kp = T / (K * (lambda + L))
          Ti = T
          Ki = Kp / Ti
        """
        if L < 0 or T <= 0 or K <= 0:
            return None, None
        lam = max(0.1, float(lam))
        den = K * (lam + L)
        if den <= 0:
            return None, None
        kp = T / den
        ki = kp / T
        return kp, ki

    def tune_inertial_pi(self):
        """Подбор PI для инерционного нагревателя (двухзонный подход).

        1) По собранной переходной характеристики оцениваем L, T, K.
        2) Считаем базовые PI (дальняя зона) по IMC.
        3) Формируем мягкие коэффициенты для зоны около уставки.
        4) В поля Kp/Ki/Kd записываем безопасный рабочий компромисс.
        """
        L, T, K = self.estimate_system_response()
        if L is None or T is None or K is None:
            messagebox.showwarning(
                "Недостаточно данных",
                "Сначала запустите тест и соберите переходную характеристику\n"
                "(не менее 30-60 секунд активного разгона)."
            )
            return

        # Базовая (дальняя) зона: быстрее набор температуры.
        lam_far = max(L, 0.35 * T)
        kp_far, ki_far = self.imc_pi_tuning(L, T, K, lam_far)
        if kp_far is None or ki_far is None:
            messagebox.showerror("Ошибка", "Не удалось рассчитать дальнюю PI-настройку.")
            return

        # Ближняя зона: мягче для снижения перерегулирования на инерционном объекте.
        kp_near = 0.45 * kp_far
        ki_near = 0.20 * ki_far

        # Рабочий набор для текущей прошивки (один комплект Kp/Ki/Kd).
        # Делаем компромисс между быстрой и мягкой зонами.
        kp_apply = 0.70 * kp_far
        ki_apply = 0.35 * ki_far
        kd_apply = 0.0

        # Ограничиваем диапазоны интерфейса.
        kp_apply = max(0.05, min(kp_apply, 100.0))
        ki_apply = max(0.0, min(ki_apply, 10.0))

        # Рекомендации по вашей логике near-zone: switchTemp и minimize.
        # Чем больше шаг и задержка, тем шире зона мягкого сопровождения.
        start_temp = float(np.mean(list(self.data_buffer['vMesT'])[:5])) if len(self.data_buffer['vMesT']) >= 5 else 25.0
        target_temp = self._safe_get_float(self.temp_var, "Температура")
        if target_temp is None:
            return
        step = abs(target_temp - start_temp)
        switch_rec = int(max(8, min(25, round(0.12 * step + 0.5 * L))))

        minimize_current = self._safe_get_int(self.minimize_var, "Minimize")
        if minimize_current is None:
            return
        minimize_rec = minimize_current
        if len(self.data_buffer['vgetPower']) >= 10 and len(self.data_buffer['vMesT']) >= 10 and len(self.data_buffer['vCurrT']) >= 10:
            tail_power = np.mean(list(self.data_buffer['vgetPower'])[-10:])
            tail_err = np.mean(np.abs(np.array(list(self.data_buffer['vCurrT'])[-10:]) - np.array(list(self.data_buffer['vMesT'])[-10:])))
            if tail_err <= switch_rec + 2:
                minimize_rec = int(max(5, min(40, round(0.8 * tail_power))))

        # Применяем рассчитанные значения в UI.
        self.kp_var.set(round(kp_apply, 4))
        self.ki_var.set(round(ki_apply, 4))
        self.kd_var.set(round(kd_apply, 4))

        self.kp_far_var.set(round(kp_far, 4))
        self.ki_far_var.set(round(ki_far, 4))
        self.kd_far_var.set(0.0)

        self.kp_near_var.set(round(kp_near, 4))
        self.ki_near_var.set(round(ki_near, 4))
        self.kd_near_var.set(0.0)

        self.switch_var.set(switch_rec)
        self.minimize_var.set(minimize_rec)

        result_text = (
            "✓ Подбор для инерционного нагревателя завершен\n\n"
            f"Оценка объекта: L={L:.3f} c, T={T:.3f} c, K={K:.3f}\n\n"
            "Двухзонные рекомендации:\n"
            f"  Дальняя зона: Kp={kp_far:.4f}, Ki={ki_far:.4f}, Kd=0\n"
            f"  Ближняя зона: Kp={kp_near:.4f}, Ki={ki_near:.4f}, Kd=0\n\n"
            "Установлен рабочий компромисс в поля:\n"
            f"  Kp={kp_apply:.4f}, Ki={ki_apply:.4f}, Kd=0\n"
            f"  switchTemp={switch_rec}, minimize={minimize_rec}\n\n"
            "Совет: запустите тест и проверьте перерегулирование;\n"
            "если перелет > 5C, уменьшите Ki на 20-30%."
        )
        messagebox.showinfo("PI инерционный (2 зоны)", result_text)
    
    def auto_tune_chr(self):
        """Автоматическая настройка PID методом CHR с запуском нагревателя до 250°C"""
        if not self.connected:
            messagebox.showwarning("Предупреждение", "WebSocket не подключен!")
            return
        
        if self.auto_tuning:
            messagebox.showwarning("Предупреждение", "Автонастройка уже в процессе!")
            return
        
        self.auto_tuning = True
        self.auto_tune_start_time = time.time()
        self.reset_plot_data()
        
        # Устанавливаем температуру на 250°C
        self.temp_var.set(250.0)

        kp = self._safe_get_float(self.kp_var, "Kp")
        ki = self._safe_get_float(self.ki_var, "Ki")
        kd = self._safe_get_float(self.kd_var, "Kd")
        minimize = self._safe_get_int(self.minimize_var, "Minimize")
        switch_temp = self._safe_get_int(self.switch_var, "Switch Temp")
        if None in (kp, ki, kd, minimize, switch_temp):
            self.auto_tuning = False
            return
        
        # Запускаем тест
        command = {
            "Start": 1,
            "Kp": kp,
            "Ki": ki,
            "Kd": kd,
            "Curr_Temp": 250.0,  # Температура для автонастройки
            "minimize": minimize,
            "switchTemp": switch_temp,
            "unitProg": self.auto_tune_max_duration,  # Максимальная длительность
        }
        self.ws_client.send_async(command)
        messagebox.showinfo("CHR", "Запуск автонастройки...\nНагреватель включен, ожидание сбора данных...")
        
        # Запускаем проверку собранных данных
        self._check_auto_tune_progress()
    
    def _check_auto_tune_progress(self):
        """Проверка достаточности данных для автонастройки"""
        if not self.auto_tuning:
            return
        
        elapsed = time.time() - self.auto_tune_start_time
        
        # Проверяем что собралось достаточно данных и система откликнулась
        temps = list(self.data_buffer['vMesT'])
        
        if len(temps) >= 30:  # Минимум 30 точек (15 секунд)
            # Завершаем только когда оценка L/T/K уже валидна.
            L, T, K = self.estimate_system_response()
            if L is not None and T is not None and K is not None:
                self._finish_auto_tune()
                return
        
        # Если время исчерпано, завершаем
        if elapsed >= self.auto_tune_max_duration:
            self._finish_auto_tune()
            return
        
        # Продолжаем проверку каждые 500мс
        self.root.after(500, self._check_auto_tune_progress)
    
    def _finish_auto_tune(self):
        """Завершение автонастройки и вычисление коэффициентов"""
        self.auto_tuning = False
        
        # Останавливаем тест
        command = {"Start": 0}
        self.ws_client.send_async(command)
        
        # Вычисляем коэффициенты
        L, T, K = self.estimate_system_response()
        
        if L is None or T is None or K is None or T <= 0 or L < 0:
            messagebox.showerror("Ошибка", 
                                "Не удалось определить параметры системы.\n"
                                f"L={L}, T={T}, K={K}\n"
                                "Убедитесь, что система достаточно откликнулась на возмущение.")
            return
        
        # Рассчитываем коэффициенты CHR
        Kp, Ki, Kd = self.chr_tuning(L, T, K)
        
        if Kp is None:
            messagebox.showerror("Ошибка", "Не удалось рассчитать коэффициенты PID.")
            return
        
        # Ограничиваем коэффициенты разумными пределами
        Kp = max(0.1, min(Kp, 100.0))
        Ki = max(0.0, min(Ki, 10.0))
        Kd = max(0.0, min(Kd, 100.0))
        
        # Обновляем значения на интерфейсе
        self.kp_var.set(Kp)
        self.ki_var.set(Ki)
        self.kd_var.set(Kd)
        
        # Выводим результаты в диалоговое окно
        result_text = (
            f"✓ Автонастройка методом CHR завершена\n\n"
            f"Параметры системы:\n"
            f"  Время задержки (L):    {L:.3f} сек\n"
            f"  Постоянная времени (T): {T:.3f} сек\n"
            f"  Коэффициент усиления (K): {K:.3f}\n\n"
            f"Рассчитанные коэффициенты PID:\n"
            f"  Kp (Пропорц.):  {Kp:.4f}\n"
            f"  Ki (Интегр.):   {Ki:.4f}\n"
            f"  Kd (Дифферец.): {Kd:.4f}\n\n"
            f"Коэффициенты установлены на слайдеры."
        )
        
        messagebox.showinfo("✓ CHR - Результаты", result_text)
    
    def on_closing(self):
        """Обработка закрытия окна"""
        if self.ws_client:
            self.ws_client.stop()
        self.root.destroy()

# ======================== Главная функция ========================
if __name__ == "__main__":
    root = tk.Tk()
    app = PIDControllerApp(root, ws_uri="ws://10.207.40.213/ws")
    root.mainloop()
