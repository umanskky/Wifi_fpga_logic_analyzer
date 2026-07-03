Подключение модулей

Этот документ описывает аппаратные соединения проекта Wi-Fi FPGA Logic Analyzer.

Проект состоит из трёх основных плат:

FPGA DE0-Nano Cyclone IV
STM32H743ZIT6
ESP32-C6

Общая цепочка:

External logic inputs / STM32 test signal
        ↓
FPGA
        ↓ SPI
STM32
        ↓ UART
ESP32-C6
        ↓ Wi-Fi
Browser

⸻

1. Общие правила подключения

1.1 Общий GND

Все платы должны иметь общий ground:

FPGA GND  ↔  STM32 GND  ↔  ESP32 GND

Без общего GND UART, SPI и логические входы будут работать нестабильно или не будут работать вообще.

1.2 Уровни сигналов

Все управляющие сигналы в проекте должны быть совместимы по уровню логики.

STM32H743: 3.3 V logic
ESP32-C6: 3.3 V logic
DE0-Nano GPIO: 3.3 V logic

Нельзя подавать 5 V напрямую на входы STM32, ESP32 или FPGA GPIO.

⸻

2. Подключение STM32 ↔ FPGA по SPI

STM32 управляет FPGA через SPI.

STM32 SPI master  ↔  FPGA SPI slave

2.1 Сигналы SPI

Сигнал	Направление	Назначение
SCK	STM32 → FPGA	SPI clock
MOSI	STM32 → FPGA	Данные от STM32 к FPGA
MISO	FPGA → STM32	Данные от FPGA к STM32
CS	STM32 → FPGA	Chip select
GND	Общий	Общая земля

2.2 Режим SPI

В проекте используется SPI mode 0:

CPOL = 0
CPHA = 0

То есть:

Clock idle low
Data sampled on rising edge

2.3 Назначение SPI

Через SPI STM32:

* читает FPGA_ID;
* читает VERSION;
* записывает настройки capture;
* запускает capture;
* читает статус;
* читает captured samples.

⸻

3. Подключение ESP32-C6 ↔ STM32 по UART

ESP32-C6 общается со STM32 через UART machine protocol.

ESP32-C6 UART  ↔  STM32 USART2

3.1 Соединение UART

ESP32-C6	STM32	Назначение
GPIO4 / TX	USART2_RX	Команды ESP32 → STM32
GPIO5 / RX	USART2_TX	Ответы STM32 → ESP32
GND	GND	Общая земля

Важно:

TX одного устройства подключается к RX другого устройства.
RX одного устройства подключается к TX другого устройства.

3.2 Параметры UART

Baudrate: 115200
Data bits: 8
Parity: none
Stop bits: 1
Flow control: none

3.3 Назначение UART STM32 ↔ ESP32

Через этот UART ESP32 отправляет команды:

@PING
@STATUS
@CFG
@RUN
@READ

STM32 отвечает:

@PONG
@OK
@DONE
@DATA
@END
@ERR

⸻

4. Подключение STM32 ↔ PC debug terminal

Для отладки STM32 используется отдельный UART.

STM32 USART3 ↔ PC terminal

Через него доступны human commands:

help
show
rate 1000000
count 512
pre 128
ch 0
rising
falling
run
imm

Этот UART используется только для отладки и не нужен для работы Web UI.

⸻

5. Подключение логических входов к FPGA

FPGA принимает внешние цифровые сигналы на 8 каналов:

CH0
CH1
CH2
CH3
CH4
CH5
CH6
CH7

Каждый канал соответствует одному bit во входной шине FPGA:

CH0 → logic_inputs[0]
CH1 → logic_inputs[1]
CH2 → logic_inputs[2]
CH3 → logic_inputs[3]
CH4 → logic_inputs[4]
CH5 → logic_inputs[5]
CH6 → logic_inputs[6]
CH7 → logic_inputs[7]

5.1 Тестовый сигнал STM32

Для проверки проекта STM32 может генерировать тестовый прямоугольный сигнал.

В текущей версии тестовый сигнал формируется на GPIO:

STM32 PB9

Он подключается к одному из входов FPGA, например:

STM32 PB9  →  FPGA CH0

Это позволяет проверить:

* immediate capture;
* triggered capture;
* rising edge trigger;
* falling edge trigger;
* pre-trigger capture;
* отображение waveform в Web UI.

5.2 Пример captured samples

Если CH0 подключён к тестовому сигналу, а остальные каналы подтянуты в 1, то можно увидеть такие значения:

0x7F = CH0 high
0x7E = CH0 low

⸻

6. Проверочный порядок подключения

6.1 Минимальное подключение для Web UI

Для работы Web UI нужны:

STM32 ↔ FPGA SPI
STM32 ↔ ESP32 UART
общий GND
питание всех плат

6.2 Минимальное подключение для отладки STM32

Для отладки STM32 дополнительно нужен:

STM32 USART3 ↔ USB/UART terminal

6.3 Минимальное подключение для тестового захвата

Для проверки capture нужен хотя бы один входной сигнал:

STM32 PB9 test signal → FPGA CH0

⸻

7. Типовая последовательность проверки

7.1 Проверка STM32 ↔ FPGA

Через debug terminal STM32:

show
run

Ожидаемый результат:

FPGA LA version check OK
Triggered capture OK

7.2 Проверка ESP32 ↔ STM32

Через ESP32 bridge или Web UI:

@PING

Ожидаемый ответ:

@PONG

7.3 Проверка полного тракта

В Web UI:

1. Apply Config
2. Capture
3. Посмотреть waveform
4. Скачать CSV или VCD

⸻

8. Возможные проблемы

8.1 Нет ответа от STM32 на ESP32

Проверить:

* общий GND;
* TX/RX не перепутаны;
* baudrate 115200;
* включен USART2 на STM32;
* включены UART interrupts;
* ESP32 использует правильные GPIO.

8.2 FPGA не отвечает

Проверить:

* питание FPGA;
* общий GND;
* SPI SCK/MOSI/MISO/CS;
* правильный SPI mode 0;
* CS активен low;
* корректный pin assignment в Quartus;
* загружена правильная FPGA bitstream.

8.3 Захват всегда показывает 0xFF

Возможные причины:

* входы FPGA не подключены;
* входы подтянуты к high;
* нет тестового сигнала;
* неправильный mapping GPIO → logic_inputs.

8.4 Захват всегда показывает 0x00

Возможные причины:

* входы FPGA подтянуты к low;
* короткое замыкание на GND;
* неправильный pin assignment;
* ошибка в wiring.

8.5 UART работает на неправильной скорости

Если в коде указано 115200, но фактически работает другая скорость, нужно проверить:

* HSE frequency в CubeMX;
* HSE_VALUE в STM32 system file;
* режим HSE ON/BYPASS;
* clock tree;
* UART kernel clock.

⸻

9. Что добавить позже

Этот документ можно дополнить:

* таблицей конкретных пинов DE0-Nano;
* таблицей конкретных пинов STM32;
* таблицей конкретных пинов ESP32-C6;
* фотографией подключения;
* схемой в KiCad/Fritzing;
* скриншотами осциллограмм;
* примерами CSV/VCD файлов.