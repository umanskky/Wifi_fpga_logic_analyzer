UART Machine Protocol

Этот документ описывает текстовый UART-протокол обмена между ESP32-C6 и STM32H743ZIT6 в проекте Wi-Fi FPGA Logic Analyzer.

Протокол используется для управления логическим анализатором со стороны ESP32 Web UI.

ESP32-C6  ← UART →  STM32H743  ← SPI →  FPGA

⸻

1. Назначение протокола

Machine protocol нужен для того, чтобы ESP32 мог:

* проверить связь со STM32;
* запросить текущий статус;
* настроить параметры захвата;
* запустить immediate capture;
* запустить triggered capture;
* запросить captured samples;
* передать данные дальше в Web UI.

Протокол сделан текстовым, чтобы его можно было легко отлаживать через обычный UART terminal.

⸻

2. Физический уровень

2.1 UART

Параметры UART:

Baudrate: 115200
Data bits: 8
Parity: none
Stop bits: 1
Flow control: none

2.2 Соединение

ESP32 TX  →  STM32 RX
ESP32 RX  ←  STM32 TX
GND       ↔  GND

Для ESP32-C6 в текущей версии используются GPIO:

ESP32 UART RX: GPIO5
ESP32 UART TX: GPIO4

⸻

3. Общий формат

Все machine-команды начинаются с символа @.

@COMMAND [arguments]\r\n

STM32 отвечает строками, также начинающимися с @, кроме строк HEX-данных после @DATA.

Пример:

@PING

Ответ:

@PONG

⸻

4. Список команд

Команда	Назначение
@PING	Проверка связи
@HELP	Список команд
@STATUS	Краткий статус STM32/FPGA
@SHOW	Текущая конфигурация
@CFG	Установка параметров capture
@RUN	Triggered capture
@IMM	Immediate capture
@READ	Чтение последнего capture buffer

⸻

5. Команды

5.1 @PING

Проверка связи ESP32 со STM32.

Запрос:

@PING

Ответ:

@PONG

⸻

5.2 @HELP

Запрос списка поддерживаемых команд.

Запрос:

@HELP

Пример ответа:

@HELP
@PING
@SHOW
@CFG <rate_hz> <count> <pre> <ch> <R|F>
@RUN
@IMM
@READ
@STATUS
@END

⸻

5.3 @STATUS

Запрос краткого статуса.

Запрос:

@STATUS

Ответ:

@STATUS fpga=OK samples=512

Поля:

Поле	Значение
fpga=OK	FPGA найдена и version check прошёл
fpga=ERR	FPGA не отвечает или version check не прошёл
samples=N	Количество samples в последнем capture buffer

⸻

5.4 @SHOW

Запрос текущей конфигурации.

Запрос:

@SHOW

Ответ:

@CFG rate=1000000 div=49 count=512 pre=128 ch=0 edge=R

Поля:

Поле	Значение
rate	Частота дискретизации в Hz
div	Значение FPGA sample rate divider
count	Количество samples
pre	Количество pre-trigger samples
ch	Trigger channel
edge	R rising или F falling

⸻

5.5 @CFG

Настройка параметров захвата.

Формат:

@CFG <rate_hz> <count> <pre> <ch> <R|F>

Пример:

@CFG 1000000 512 128 0 R

Означает:

Параметр	Значение
rate_hz	Частота дискретизации в Hz
count	Количество samples
pre	Количество pre-trigger samples
ch	Trigger channel 0..7
R/F	Rising или falling edge

Ответ при успехе:

@OK

Возможные ошибки:

@ERR CFG_USAGE
@ERR BAD_CHANNEL
@ERR BAD_EDGE

Ограничения

* count не может быть меньше 1;
* count ограничивается размером буфера STM32;
* pre не может быть больше count;
* ch должен быть в диапазоне 0..7;
* edge должен быть R, r, F или f.

⸻

5.6 @RUN

Запуск triggered capture.

Перед использованием желательно выполнить @CFG.

Запрос:

@RUN

Ответ при успехе:

@DONE 512

Где 512 — фактическое количество захваченных samples.

Ошибка:

@ERR RUN_FAILED

⸻

5.7 @IMM

Запуск immediate capture без ожидания trigger.

Запрос:

@IMM

Ответ при успехе:

@DONE 512

Ошибка:

@ERR IMM_FAILED

⸻

5.8 @READ

Чтение последнего capture buffer.

Запрос:

@READ

Если данных нет:

@ERR NO_DATA

Если данные есть:

@DATA 512
7F7F7F7E7E7E7F7F...
...
@END

Формат:

@DATA <count>
<HEX data>
@END

Каждый sample передаётся как один байт в HEX-формате:

7F
7E
FF
00

Для удобства чтения STM32 может переносить строку после каждых 32 samples.

⸻

6. Формат sample

Каждый sample — это один байт.

bit0 → CH0
bit1 → CH1
bit2 → CH2
bit3 → CH3
bit4 → CH4
bit5 → CH5
bit6 → CH6
bit7 → CH7

Пример:

0x7F = 0111 1111
0x7E = 0111 1110

Если CH0 подключён к тестовому сигналу, а остальные каналы находятся в 1, то:

0x7F → CH0 = 1
0x7E → CH0 = 0

⸻

7. Типовая последовательность обмена

7.1 Проверка связи

ESP32 → STM32: @PING
STM32 → ESP32: @PONG

7.2 Настройка capture

ESP32 → STM32: @CFG 1000000 512 128 0 R
STM32 → ESP32: @OK

7.3 Запуск capture

ESP32 → STM32: @RUN
STM32 → ESP32: @DONE 512

7.4 Чтение данных

ESP32 → STM32: @READ
STM32 → ESP32: @DATA 512
STM32 → ESP32: 7F7F7F7E7E7E7F...
STM32 → ESP32: @END

⸻

8. Ошибки

Все ошибки начинаются с:

@ERR

Текущие ошибки:

Ошибка	Причина
@ERR UNKNOWN	Неизвестная команда
@ERR CFG_USAGE	Неверный формат @CFG
@ERR BAD_CHANNEL	Неверный trigger channel
@ERR BAD_EDGE	Неверный trigger edge
@ERR RUN_FAILED	Ошибка triggered capture
@ERR IMM_FAILED	Ошибка immediate capture
@ERR NO_DATA	Нет данных для @READ

⸻

9. Почему протокол текстовый

Текущая версия протокола сделана текстовой по нескольким причинам:

* легко отлаживать через UART terminal;
* удобно читать лог обмена;
* просто реализовать на STM32 и ESP32;
* удобно тестировать вручную;
* достаточно для текущего объёма данных.

Недостатки текстового протокола:

* HEX-передача увеличивает объём данных примерно в 2 раза;
* нет CRC/checksum;
* нет sequence ID;
* нет binary framing;
* сложнее эффективно передавать большие capture buffer.

⸻

10. Возможные улучшения протокола

В будущих версиях можно добавить:

* binary packet format;
* CRC16;
* sequence ID;
* packet length;
* command ACK/NACK;
* streaming mode;
* chunked sample transfer;
* timeout/error codes;
* версию протокола;
* команду чтения FPGA raw status;
* команду чтения register map.