Архитектура проекта

Этот документ описывает архитектуру проекта Wi-Fi FPGA Logic Analyzer.

Проект состоит из трёх основных вычислительных узлов:

FPGA DE0-Nano  →  STM32H743ZIT6  →  ESP32-C6  →  Browser

Каждый узел выполняет отдельную роль:

* FPGA отвечает за точный по времени захват цифровых сигналов.
* STM32 управляет FPGA, настраивает захват и считывает данные.
* ESP32-C6 предоставляет Wi-Fi Web UI и визуализирует данные в браузере.

⸻

1. Общая схема

External Logic Inputs
        |
        v
+--------------------------+
| FPGA DE0-Nano Cyclone IV |
|                          |
|  logic_inputs[7:0]       |
|        |                 |
|        v                 |
|  +-----------+           |
|  | Sampler   |           |
|  +-----------+           |
|        |                 |
|        v                 |
|  +-----------+           |
|  | Trigger   |           |
|  +-----------+           |
|        |                 |
|        v                 |
|  +-------------------+   |
|  | Capture Memory    |   |
|  | Pre-trigger logic |   |
|  +-------------------+   |
|        |                 |
|        v                 |
|  +-------------------+   |
|  | SPI Register Map  |   |
|  +-------------------+   |
+-----------+--------------+
            |
            | SPI
            v
+--------------------------+
| STM32H743ZIT6            |
|                          |
|  fpga_spi                |
|        |                 |
|  fpga_logic_analyzer     |
|        |                 |
|  app_la_console          |
|        |                 |
|  UART machine protocol   |
+-----------+--------------+
            |
            | UART
            v
+--------------------------+
| ESP32-C6                 |
|                          |
|  UART client             |
|  HTTP server             |
|  Web UI                  |
|  Canvas waveform viewer  |
+-----------+--------------+
            |
            | Wi-Fi
            v
+--------------------------+
| Browser                  |
+--------------------------+

⸻

2. FPGA

FPGA выполняет нижний уровень логического анализатора.

Главная задача FPGA — независимо от STM32 и ESP32 захватывать входные цифровые сигналы с заданной частотой и сохранять результат во внутренней памяти.

2.1 Основные функции FPGA

FPGA реализует:

* 8-битный входной sampler;
* programmable sample rate divider;
* immediate capture;
* triggered capture;
* rising/falling edge trigger;
* selectable trigger channel;
* pre-trigger circular buffer;
* capture memory;
* SPI slave register map;
* последовательное чтение samples через SPI.

2.2 Входные данные

Входные сигналы представлены как 8-битная шина:

logic_inputs[7:0]

Каждый bit соответствует отдельному каналу логического анализатора:

bit0 → CH0
bit1 → CH1
bit2 → CH2
bit3 → CH3
bit4 → CH4
bit5 → CH5
bit6 → CH6
bit7 → CH7

2.3 Режимы захвата

FPGA поддерживает два основных режима:

Immediate capture
Triggered capture

Immediate capture

В этом режиме захват начинается сразу после команды START.

Используется для простой проверки сигналов или отладки.

Triggered capture

В этом режиме FPGA сначала переходит в состояние ожидания trigger. Захват завершается после обнаружения заданного фронта на выбранном канале и записи нужного количества samples.

Trigger задаётся параметрами:

trigger_enable
trigger_channel
trigger_edge

Где trigger_edge может быть:

rising
falling

2.4 Pre-trigger

Pre-trigger позволяет сохранить часть сигнала до события trigger.

FPGA постоянно ведёт кольцевую запись последних samples. Когда trigger найден, FPGA формирует итоговый буфер так, чтобы в начале были samples до события, а затем samples после события.

Пример:

sample_count = 512
pretrigger_count = 128

Это означает:

128 samples до trigger
384 samples после trigger

2.5 Register map

STM32 управляет FPGA через SPI register map.

Основные группы регистров:

ID / VERSION
STATUS
CONTROL
SAMPLE_RATE_DIV
SAMPLE_COUNT
SAMPLES_DONE
SAMPLER_DATA
INPUT_MODE
TRIGGER_CFG
PRETRIGGER_COUNT

Register map позволяет STM32:

* проверить наличие FPGA;
* настроить частоту дискретизации;
* задать количество samples;
* задать trigger;
* задать pre-trigger;
* запустить capture;
* считать количество захваченных samples;
* считать captured data.

⸻

3. STM32

STM32 является центральным управляющим контроллером системы.

Он соединяет FPGA и ESP32:

FPGA ← SPI → STM32 ← UART → ESP32

3.1 Основные задачи STM32

STM32 выполняет:

* инициализацию SPI;
* инициализацию UART;
* проверку FPGA ID/version;
* запись конфигурации в FPGA;
* запуск capture;
* ожидание завершения capture;
* чтение samples из FPGA;
* выдачу данных ESP32;
* debug CLI для человека.

3.2 Слои STM32 firmware

Рекомендуемая структура STM32 firmware:

main.c
    |
    v
app_la_console.c
    |
    v
fpga_logic_analyzer.c
    |
    v
fpga_spi.c
    |
    v
STM32 HAL SPI

3.3 fpga_spi

fpga_spi — низкоуровневый SPI-драйвер доступа к FPGA register map.

Он предоставляет операции:

fpga_read_reg()
fpga_write_reg()
fpga_check_id()

Этот слой ничего не знает о логическом анализаторе. Он умеет только читать и писать FPGA-регистры.

3.4 fpga_logic_analyzer

fpga_logic_analyzer — high-level API логического анализатора.

Он предоставляет функции:

fpga_la_set_sample_rate_div()
fpga_la_set_sample_count()
fpga_la_set_pretrigger_count()
fpga_la_set_trigger()
fpga_la_capture()
fpga_la_read_samples()

Этот слой уже знает register map FPGA и умеет выполнять полный цикл захвата.

3.5 app_la_console

app_la_console — прикладной слой STM32.

Он отвечает за:

* human UART CLI;
* machine UART protocol;
* хранение текущей конфигурации;
* буфер samples;
* обработку команд;
* ответы ESP32;
* печать debug-информации.

После выноса этого слоя main.c остаётся компактным и содержит только:

* CubeMX initialization;
* запуск таймера тестового сигнала;
* app_la_init();
* app_la_start();
* app_la_process();
* HAL callbacks.

⸻

4. ESP32-C6

ESP32-C6 является пользовательским интерфейсом проекта.

Он не занимается непосредственным захватом сигналов. Его задача — дать удобный доступ к STM32 и отобразить результат в браузере.

4.1 Основные функции ESP32

ESP32 выполняет:

* запуск Wi-Fi Access Point;
* запуск HTTP server;
* отображение Web UI;
* отправку команд STM32;
* приём captured samples;
* парсинг HEX-данных;
* отображение waveform через Canvas;
* экспорт CSV;
* экспорт VCD;
* сохранение настроек во flash.

4.2 Web UI

Web UI позволяет управлять логическим анализатором без отдельной desktop-программы.

Основные элементы UI:

Config
Capture
Waveform
Data export

В Config задаются:

sample rate
sample count
pre-trigger count
trigger channel
trigger edge

В Waveform доступны:

выбор каналов
zoom
pan
center trigger
time scale
trigger marker

⸻

5. Machine Protocol

ESP32 и STM32 обмениваются простым текстовым протоколом по UART.

5.1 Команды

@PING
@STATUS
@SHOW
@CFG <rate_hz> <count> <pre> <ch> <R|F>
@RUN
@IMM
@READ

5.2 Пример настройки

@CFG 1000000 512 128 0 R

Означает:

sample_rate = 1 MHz
sample_count = 512
pretrigger_count = 128
trigger_channel = CH0
trigger_edge = rising

Ответ:

@OK

5.3 Пример запуска capture

@RUN

Ответ:

@DONE 512

5.4 Чтение данных

Команда:

@READ

Ответ:

@DATA 512
7F7F7E7E7E7F...
@END

Данные передаются в HEX-формате. Каждый sample — один байт, где каждый bit соответствует одному каналу.

Пример:

0x7F = CH0 high
0x7E = CH0 low

⸻

6. Типовой сценарий работы

1. Пользователь подключается к Wi-Fi AP ESP32.
2. Открывает Web UI в браузере.
3. Задаёт sample rate, count, pre-trigger, channel и edge.
4. Нажимает Capture.
5. ESP32 отправляет @CFG на STM32.
6. STM32 записывает настройки в FPGA.
7. ESP32 отправляет @RUN.
8. STM32 запускает FPGA capture.
9. FPGA ждёт trigger и сохраняет samples.
10. STM32 считывает samples из FPGA.
11. ESP32 отправляет @READ.
12. STM32 возвращает данные в HEX.
13. ESP32 отображает waveform в браузере.
14. Пользователь экспортирует CSV или VCD.

⸻

7. Границы ответственности

FPGA

FPGA отвечает за:

точное время
sampling
trigger
pre-trigger
capture memory
SPI register interface

FPGA не отвечает за:

Web UI
UART protocol
CSV/VCD export
высокоуровневую логику интерфейса

STM32

STM32 отвечает за:

управление FPGA
SPI transactions
capture sequence
UART protocol
debug CLI

STM32 не отвечает за:

отрисовку waveform
Wi-Fi
HTTP server
браузерный интерфейс

ESP32

ESP32 отвечает за:

Wi-Fi
HTTP API
Web UI
получение данных от STM32
визуализацию
экспорт
сохранение настроек UI

ESP32 не отвечает за:

точный sampling
trigger на уровне clock FPGA
низкоуровневое управление capture memory

⸻

8. Почему архитектура разделена именно так

Такое разделение позволяет использовать сильные стороны каждого устройства:

Узел	Сильная сторона	Использование в проекте
FPGA	Параллельная логика и точное время	Sampling, trigger, pre-trigger
STM32	Надёжный embedded controller	SPI control, capture API, protocol
ESP32-C6	Wi-Fi и Web UI	Browser interface, export, settings

FPGA не перегружается интерфейсной логикой, STM32 не занимается Wi-Fi и HTML, а ESP32 не участвует в критичном по времени sampling.

⸻

9. Возможные улучшения архитектуры

В будущем архитектуру можно расширить:

* добавить FPGA IRQ line DONE к STM32;
* перейти с HEX UART protocol на binary protocol;
* добавить CRC/checksum;
* увеличить sample buffer;
* добавить trigger mask/value;
* добавить multi-channel trigger;
* добавить streaming mode;
* добавить отдельную debug page на ESP32;
* добавить AP+STA режим Wi-Fi;
* добавить автоматическую генерацию VCD с ns timescale для высоких частот.