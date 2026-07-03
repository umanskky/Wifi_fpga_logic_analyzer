FPGA Register Map

Этот документ описывает SPI register map FPGA в проекте Wi-Fi FPGA Logic Analyzer.

Register map используется STM32 для управления FPGA logic analyzer:

STM32 SPI master  ↔  FPGA SPI slave

Через регистры STM32 может:

* проверить наличие FPGA;
* проверить версию логики;
* настроить частоту дискретизации;
* задать количество samples;
* настроить trigger;
* настроить pre-trigger;
* запустить capture;
* проверить статус;
* считать captured samples.

⸻

1. Общая информация

1.1 SPI mode

Используется SPI mode 0:

CPOL = 0
CPHA = 0

То есть:

SCK idle low
Data sampled on rising edge
Data shifted on falling edge

1.2 Размер регистра

Все регистры имеют размер 8 bit.

16-битные значения передаются двумя регистрами:

LOW byte
HIGH byte

Пример:

SAMPLE_COUNT_L
SAMPLE_COUNT_H

1.3 Адресация

Для записи используется адрес регистра без установленного старшего бита.

Для чтения STM32 передаёт адрес с установленным bit7:

read_cmd = 0x80 | register_address

Пример:

read FPGA_ID:
0x80 | 0x00 = 0x80

⸻

2. Register Map

Address	Register	R/W	Description
0x00	FPGA_ID	R	ID FPGA logic
0x01	VERSION	R	Register map version
0x02	STATUS	R	Sampler status
0x03	CONTROL	W	Control pulses
0x04	SAMPLE_RATE_DIV_L	R/W	Sample rate divider low byte
0x05	SAMPLE_RATE_DIV_H	R/W	Sample rate divider high byte
0x06	SAMPLE_COUNT_L	R/W	Sample count low byte
0x07	SAMPLE_COUNT_H	R/W	Sample count high byte
0x08	SAMPLES_DONE_L	R	Captured sample count low byte
0x09	SAMPLES_DONE_H	R	Captured sample count high byte
0x0A	SAMPLER_DATA	R	Captured sample readout
0x0B	TEST_PATTERN	R/W	Test pattern value
0x0C	INPUT_MODE	R/W	Input source selection
0x0D	TRIGGER_CFG	R/W	Trigger configuration
0x0E	PRETRIGGER_COUNT_L	R/W	Pre-trigger count low byte
0x0F	PRETRIGGER_COUNT_H	R/W	Pre-trigger count high byte

⸻

3. Register Details

3.1 FPGA_ID — 0x00

Read-only register.

Ожидаемое значение:

0xA5

Используется STM32 для проверки, что SPI-связь с FPGA работает и загружена правильная логика.

⸻

3.2 VERSION — 0x01

Read-only register.

Текущая версия:

0x05

Используется для проверки совместимости STM32 firmware и FPGA register map.

⸻

3.3 STATUS — 0x02

Read-only register.

Bit	Name	Description
0	alive	FPGA logic is alive
1	busy	Capture is active
2	done	Capture finished
3	overflow	Capture overflow flag
4	read_empty	Readout buffer is empty
5	read_valid	Read data is valid
6	armed	Waiting for trigger
7	reserved	Reserved

Пример:

STATUS = 0x45

Расшифровка:

bit0 alive = 1
bit2 done  = 1
bit6 armed = 1

⸻

3.4 CONTROL — 0x03

Write-only register.

Используется для отправки коротких управляющих импульсов.

Bit	Name	Description
0	START	Start capture
1	CLEAR	Clear sampler state
2	READ_RESET	Reset sample read pointer
3..7	reserved	Reserved

Типовое использование:

write CONTROL = START
write CONTROL = 0x00

То есть STM32 записывает бит, а затем сбрасывает регистр обратно в 0.

⸻

3.5 SAMPLE_RATE_DIV_L/H — 0x04 / 0x05

Read/write 16-bit register.

Задаёт делитель частоты дискретизации.

Формула:

sample_rate = FPGA_CLK / (sample_rate_div + 1)

Для текущей FPGA clock:

FPGA_CLK = 50 MHz

Примеры:

Sample rate	Divider
1 MHz	49
500 kHz	99
100 kHz	499
10 kHz	4999

⸻

3.6 SAMPLE_COUNT_L/H — 0x06 / 0x07

Read/write 16-bit register.

Задаёт требуемое количество samples.

Пример:

sample_count = 512

В регистрах:

SAMPLE_COUNT_L = 0x00
SAMPLE_COUNT_H = 0x02

⸻

3.7 SAMPLES_DONE_L/H — 0x08 / 0x09

Read-only 16-bit register.

Показывает фактическое количество captured samples после завершения capture.

STM32 читает это значение перед чтением данных из SAMPLER_DATA.

⸻

3.8 SAMPLER_DATA — 0x0A

Read-only register.

Используется для последовательного чтения captured samples.

Каждое чтение возвращает один sample:

1 sample = 8 bit

Соответствие bit/channel:

bit0 → CH0
bit1 → CH1
bit2 → CH2
bit3 → CH3
bit4 → CH4
bit5 → CH5
bit6 → CH6
bit7 → CH7

Перед чтением данных STM32 должен выполнить READ_RESET.

Типовая последовательность:

write CONTROL = READ_RESET
write CONTROL = 0x00
read SAMPLER_DATA
read SAMPLER_DATA
read SAMPLER_DATA
...

⸻

3.9 TEST_PATTERN — 0x0B

Read/write register.

Используется для тестового режима входных данных.

Если INPUT_MODE = TEST_PATTERN, sampler вместо внешних входов использует значение из TEST_PATTERN.

Пример:

TEST_PATTERN = 0x55

⸻

3.10 INPUT_MODE — 0x0C

Read/write register.

Выбор источника данных для sampler.

Value	Mode	Description
0x00	TEST_PATTERN	Использовать TEST_PATTERN
0x01	EXTERNAL	Использовать реальные FPGA input pins

В рабочем режиме используется:

INPUT_MODE = 0x01

⸻

3.11 TRIGGER_CFG — 0x0D

Read/write register.

Настройка trigger.

Bits	Name	Description
0	trigger_enable	Enable triggered capture
1	trigger_edge	0 falling, 1 rising
4:2	trigger_channel	Channel 0..7
7:5	reserved	Reserved

Примеры:

Rising edge on CH0

trigger_enable = 1
trigger_edge   = 1
trigger_channel = 0
TRIGGER_CFG = 0b00000011 = 0x03

Falling edge on CH0

trigger_enable = 1
trigger_edge   = 0
trigger_channel = 0
TRIGGER_CFG = 0b00000001 = 0x01

Rising edge on CH3

trigger_enable = 1
trigger_edge   = 1
trigger_channel = 3
TRIGGER_CFG = 0b00001111 = 0x0F

⸻

3.12 PRETRIGGER_COUNT_L/H — 0x0E / 0x0F

Read/write 16-bit register.

Задаёт количество samples до trigger, которые должны попасть в итоговый capture buffer.

Пример:

sample_count = 512
pretrigger_count = 128

Результат:

128 samples before trigger
384 samples after trigger

Если pretrigger_count > sample_count, значение должно быть ограничено на стороне STM32 или FPGA.

⸻

4. Типовые последовательности

4.1 Проверка FPGA

read FPGA_ID
read VERSION

Ожидаемый результат:

FPGA_ID = 0xA5
VERSION = 0x05

⸻

4.2 Immediate Capture

write INPUT_MODE = EXTERNAL
write TRIGGER_CFG = 0x00
write PRETRIGGER_COUNT = 0
write SAMPLE_RATE_DIV
write SAMPLE_COUNT
write CONTROL = CLEAR
write CONTROL = 0x00
write CONTROL = START
write CONTROL = 0x00
poll STATUS until done = 1
read SAMPLES_DONE
write CONTROL = READ_RESET
write CONTROL = 0x00
read SAMPLER_DATA N times

⸻

4.3 Triggered Capture

write INPUT_MODE = EXTERNAL
write SAMPLE_RATE_DIV
write SAMPLE_COUNT
write PRETRIGGER_COUNT
write TRIGGER_CFG
write CONTROL = CLEAR
write CONTROL = 0x00
write CONTROL = START
write CONTROL = 0x00
poll STATUS until armed = 1 or done = 1
poll STATUS until done = 1
read SAMPLES_DONE
write CONTROL = READ_RESET
write CONTROL = 0x00
read SAMPLER_DATA N times

⸻

5. Пример расчёта sample rate divider

Формула:

sample_rate_div = FPGA_CLK / sample_rate - 1

Для 1 MHz:

sample_rate_div = 50,000,000 / 1,000,000 - 1
sample_rate_div = 50 - 1
sample_rate_div = 49

Для 100 kHz:

sample_rate_div = 50,000,000 / 100,000 - 1
sample_rate_div = 500 - 1
sample_rate_div = 499

⸻

6. Notes

* Все multi-byte значения передаются little-endian: low byte first.
* CONTROL используется как pulse register.
* После записи управляющего бита в CONTROL STM32 сбрасывает CONTROL в 0x00.
* Перед чтением SAMPLER_DATA нужно выполнить READ_RESET.
* Каждый read SAMPLER_DATA продвигает внутренний read pointer.
* Для triggered capture STM32 должен учитывать, что при быстром сигнале FPGA может перейти в DONE раньше, чем STM32 успеет увидеть ARMED.