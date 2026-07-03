Файлы .md лучше смотреть в режиме code. В папке docs/screenshot изображение работающего устройства. 

Wi-Fi FPGA Logic Analyzer

Wi-Fi FPGA Logic Analyzer — это демонстрационный embedded-проект логического анализатора на базе FPGA DE0-Nano Cyclone IV, STM32H743ZIT6 и ESP32-C6.

Проект реализует полный путь от захвата цифровых сигналов в FPGA до отображения осциллограмм в браузере через Wi-Fi.

Внешние цифровые сигналы
        ↓
FPGA DE0-Nano
        ↓ SPI
STM32H743ZIT6
        ↓ UART
ESP32-C6
        ↓ Wi-Fi
Браузер / Web UI

Назначение проекта

Цель проекта — показать практическую интеграцию нескольких уровней embedded-системы:

* цифровая логика на FPGA;
* firmware на STM32;
* обмен по SPI и UART;
* Wi-Fi веб-интерфейс на ESP32;
* визуализация цифровых сигналов в браузере;
* экспорт данных для дальнейшего анализа.

Проект задуман как инженерная “визитка”: он демонстрирует не отдельный пример работы с микроконтроллером, а законченную систему из нескольких аппаратных и программных уровней.

Возможности

* Захват 8 цифровых логических каналов
* FPGA-sampler
* Настраиваемая частота дискретизации
* Настраиваемое количество samples
* Trigger по rising/falling edge
* Выбор trigger channel
* Pre-trigger capture
* SPI register map между STM32 и FPGA
* UART machine protocol между STM32 и ESP32
* Human UART CLI для отладки
* ESP32-C6 в режиме Wi-Fi Access Point
* Web UI в браузере
* Canvas waveform viewer
* Выбор отображаемых каналов
* Zoom / pan
* Маркер позиции trigger
* Отображение временной шкалы
* Экспорт CSV
* Экспорт VCD для GTKWave и других waveform viewer
* Сохранение настроек ESP32 во flash

Аппаратная архитектура

+--------------------------+
|  External Logic Inputs   |
+------------+-------------+
             |
             v
+--------------------------+
| FPGA DE0-Nano Cyclone IV |
|                          |
| - 8-channel sampler      |
| - trigger detector       |
| - pre-trigger buffer     |
| - capture memory         |
| - SPI slave registers    |
+------------+-------------+
             |
             | SPI
             v
+--------------------------+
| STM32H743ZIT6            |
|                          |
| - FPGA SPI driver        |
| - capture API            |
| - UART CLI               |
| - machine protocol       |
+------------+-------------+
             |
             | UART
             v
+--------------------------+
| ESP32-C6                 |
|                          |
| - Wi-Fi AP               |
| - HTTP server            |
| - Web UI                 |
| - waveform viewer        |
| - CSV/VCD export         |
+------------+-------------+
             |
             | Wi-Fi
             v
+--------------------------+
| Browser                  |
+--------------------------+

Используемое железо

Узел	Назначение
DE0-Nano Cyclone IV	Захват цифровых сигналов, trigger, pre-trigger
STM32H743ZIT6	Управление FPGA, SPI, UART protocol
ESP32-C6	Wi-Fi AP, Web UI, HTTP API
PC / телефон / планшет	Доступ к Web UI через браузер

Интерфейсы

Интерфейс	Направление	Назначение
GPIO	Внешние сигналы → FPGA	Логические входы CH0..CH7
SPI	STM32 ↔ FPGA	Доступ к register map и чтение samples
UART	STM32 ↔ ESP32	Machine protocol
UART	STM32 ↔ PC	Debug terminal / human CLI
Wi-Fi	ESP32 ↔ Browser	Web UI

FPGA

FPGA выполняет критичную по времени часть проекта — захват цифровых сигналов.

Основные блоки FPGA:

* входной sampler;
* trigger detector;
* rising/falling edge detection;
* pre-trigger circular buffer;
* capture memory;
* SPI slave register interface;
* readout logic.

FPGA Register Map

Address	Register	Description
0x00	FPGA_ID	ID устройства
0x01	VERSION	Версия register map
0x02	STATUS	Статус sampler
0x03	CONTROL	Start / clear / read reset
0x04	SAMPLE_RATE_DIV_L	Делитель частоты, младший байт
0x05	SAMPLE_RATE_DIV_H	Делитель частоты, старший байт
0x06	SAMPLE_COUNT_L	Количество samples, младший байт
0x07	SAMPLE_COUNT_H	Количество samples, старший байт
0x08	SAMPLES_DONE_L	Количество захваченных samples, младший байт
0x09	SAMPLES_DONE_H	Количество захваченных samples, старший байт
0x0A	SAMPLER_DATA	Чтение sample data
0x0B	TEST_PATTERN	Тестовый pattern
0x0C	INPUT_MODE	Источник входных данных
0x0D	TRIGGER_CFG	Настройка trigger
0x0E	PRETRIGGER_COUNT_L	Pre-trigger count, младший байт
0x0F	PRETRIGGER_COUNT_H	Pre-trigger count, старший байт

STM32 Firmware

STM32 является центральным контроллером системы.

Основные задачи STM32:

* инициализация SPI и UART;
* проверка FPGA ID/version;
* настройка регистров FPGA;
* запуск immediate или triggered capture;
* чтение samples из FPGA;
* human CLI через debug UART;
* machine protocol для ESP32.

Human CLI

Через debug UART доступны команды:

help
show
rate 1000000
count 512
pre 128
ch 0
rising
falling
imm
run

Пример:

rate 1000000
count 512
pre 128
ch 0
rising
run

Machine Protocol

ESP32 общается со STM32 через текстовый UART-протокол.

Команды:

@PING
@STATUS
@SHOW
@CFG <rate_hz> <count> <pre> <ch> <R|F>
@RUN
@IMM
@READ

Пример настройки и запуска:

@CFG 1000000 512 128 0 R
@RUN
@READ

Пример ответа:

@OK
@DONE 512
@DATA 512
...
@END

ESP32-C6 Firmware

ESP32-C6 отвечает за пользовательский интерфейс.

Основные задачи ESP32:

* создание Wi-Fi Access Point;
* запуск HTTP web server;
* отправка команд STM32;
* чтение captured samples;
* отображение waveform в браузере;
* экспорт данных;
* сохранение пользовательских настроек во flash.

Wi-Fi Access Point

По умолчанию ESP32 создаёт точку доступа:

SSID: FPGA-LA
Password: 12345678
Address: http://192.168.4.1

Web UI

Веб-интерфейс позволяет:

* задать sample rate;
* задать sample count;
* задать pre-trigger count;
* выбрать trigger channel;
* выбрать rising/falling edge;
* запустить capture;
* выбрать отображаемые каналы;
* использовать zoom и pan;
* центрировать waveform по trigger marker;
* экспортировать данные в CSV;
* экспортировать данные в VCD.

Поток данных

1. Пользователь открывает Web UI в браузере.
2. ESP32 отправляет конфигурацию на STM32.
3. STM32 записывает настройки в FPGA по SPI.
4. FPGA ожидает trigger или запускает immediate capture.
5. FPGA сохраняет samples во внутренней памяти.
6. STM32 считывает samples из FPGA по SPI.
7. ESP32 получает samples от STM32 по UART.
8. Браузер отображает waveform через Canvas.
9. Пользователь экспортирует данные в CSV или VCD.

Экспорт данных

CSV

CSV содержит:

* индекс sample;
* время;
* raw sample byte;
* состояния каналов CH0..CH7.

Формат удобен для анализа в таблицах, Python, MATLAB или других инструментах.

VCD

VCD можно открыть в waveform viewer, например в GTKWave.

Это позволяет анализировать захваченные цифровые сигналы как обычную временную диаграмму.

Текущее состояние проекта

Реализовано и проверено:

* FPGA SPI slave register interface
* FPGA sampler
* immediate capture
* triggered capture
* rising/falling trigger
* pre-trigger capture
* STM32 SPI FPGA driver
* STM32 high-level logic analyzer API
* STM32 human UART CLI
* STM32 machine UART protocol
* ESP32 UART client
* ESP32 Wi-Fi Access Point
* ESP32 Web UI
* Canvas waveform viewer
* CSV export
* VCD export
* сохранение настроек ESP32 во flash

Предлагаемая структура репозитория

wifi-fpga-logic-analyzer/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── wiring.md
│   ├── protocol.md
|      ...
│   └── screenshots/
│
├── fpga/
│   ├── rtl/
│   └── quartus/
│   
│
├── stm32/
│   └── STM32H743_LogicAnalyzer/
│
├── esp32/
│   └── esp32_c6_web_ui/



Возможные улучшения

* Увеличение sample buffer
* Binary UART protocol
* CRC/checksum для machine protocol
* Multi-channel trigger
* Trigger mask/value mode
* FPGA interrupt line to STM32
* AP + STA режим ESP32
* Отдельная debug page в Web UI
* Hardware wiring diagram
* Скриншоты Web UI в README
* Корпус или компактная сборка устройства

Статус

Проект находится в рабочем состоянии и предназначен для демонстрации навыков разработки embedded-систем на стыке FPGA, STM32, ESP32, SPI/UART-протоколов и браузерной визуализации цифровых сигналов.

License

TBD.
