STM32 Firmware

Этот документ описывает структуру STM32 firmware в проекте Wi-Fi FPGA Logic Analyzer.

STM32H743ZIT6 является центральным управляющим контроллером между FPGA и ESP32-C6.

FPGA DE0-Nano  ← SPI →  STM32H743ZIT6  ← UART →  ESP32-C6

⸻

1. Назначение STM32

STM32 выполняет роль embedded controller.

Основные задачи:

* инициализация периферии;
* управление FPGA по SPI;
* настройка параметров логического анализатора;
* запуск capture;
* ожидание завершения capture;
* чтение captured samples из FPGA;
* human UART CLI для отладки;
* machine UART protocol для ESP32;
* передача данных в ESP32 Web UI.

STM32 не занимается Wi-Fi, HTTP, HTML или отрисовкой waveform. Эти задачи выполняет ESP32-C6.

⸻

2. Общая структура firmware

Рекомендуемая структура STM32-кода:

Core/
├── Inc/
│   ├── main.h
│   ├── fpga_spi.h
│   ├── fpga_logic_analyzer.h
│   └── app_la_console.h
│
└── Src/
    ├── main.c
    ├── fpga_spi.c
    ├── fpga_logic_analyzer.c
    └── app_la_console.c

Слои firmware:

main.c
  ↓
app_la_console.c
  ↓
fpga_logic_analyzer.c
  ↓
fpga_spi.c
  ↓
STM32 HAL SPI

⸻

3. Разделение ответственности

3.1 main.c

main.c должен оставаться максимально компактным.

Его задача:

* CubeMX-generated initialization;
* запуск таймера тестового сигнала;
* вызов app_la_init();
* вызов app_la_start();
* периодический вызов app_la_process();
* передача UART callback в application layer.

main.c не должен содержать:

* парсинг команд;
* machine protocol;
* печать waveform;
* FPGA register logic;
* sample buffer;
* UART line buffers.

После выноса логики в app_la_console.c, main.c становится точкой запуска проекта, а не местом хранения всей прикладной логики.

⸻

3.2 fpga_spi.c/.h

fpga_spi — это низкоуровневый SPI-драйвер FPGA.

Он знает только:

* SPI handle;
* GPIO port/pin для CS;
* как читать FPGA register;
* как писать FPGA register.

Основной public API:

HAL_StatusTypeDef fpga_init(
    fpga_spi_t *dev,
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *cs_port,
    uint16_t cs_pin
);
HAL_StatusTypeDef fpga_read_reg(
    fpga_spi_t *dev,
    uint8_t addr,
    uint8_t *data
);
HAL_StatusTypeDef fpga_write_reg(
    fpga_spi_t *dev,
    uint8_t addr,
    uint8_t data
);
HAL_StatusTypeDef fpga_check_id(
    fpga_spi_t *dev
);

Этот слой не знает, что такое capture, trigger или pre-trigger.

⸻

3.3 fpga_logic_analyzer.c/.h

fpga_logic_analyzer — high-level API логического анализатора.

Он знает FPGA register map и умеет выполнять полный цикл захвата.

Основные задачи:

* проверка FPGA_ID и VERSION;
* запись SAMPLE_RATE_DIV;
* запись SAMPLE_COUNT;
* запись PRETRIGGER_COUNT;
* настройка TRIGGER_CFG;
* запуск capture;
* ожидание DONE;
* чтение SAMPLES_DONE;
* чтение samples из SAMPLER_DATA.

Основной public API:

HAL_StatusTypeDef fpga_la_init(...);
HAL_StatusTypeDef fpga_la_check_version(...);
HAL_StatusTypeDef fpga_la_set_sample_rate_div(...);
HAL_StatusTypeDef fpga_la_set_sample_count(...);
HAL_StatusTypeDef fpga_la_set_pretrigger_count(...);
HAL_StatusTypeDef fpga_la_set_trigger(...);
HAL_StatusTypeDef fpga_la_capture(...);
HAL_StatusTypeDef fpga_la_read_samples(...);

Этот слой не должен знать про UART, ESP32, CLI или Web UI.

⸻

3.4 app_la_console.c/.h

app_la_console — прикладной слой STM32.

Он связывает FPGA logic analyzer API с UART-интерфейсами.

Основные задачи:

* хранить текущую конфигурацию capture;
* хранить последний sample buffer;
* принимать команды с debug UART;
* принимать machine-команды от ESP32;
* запускать capture;
* возвращать данные в human-readable или machine-readable виде;
* обрабатывать UART RX interrupt line buffers.

Public API модуля:

void app_la_init(
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *fpga_cs_port,
    uint16_t fpga_cs_pin,
    UART_HandleTypeDef *debug_uart,
    UART_HandleTypeDef *esp_uart
);
void app_la_start(void);
void app_la_process(void);
void app_la_uart_rx_cplt_callback(UART_HandleTypeDef *huart);

⸻

4. Инициализация приложения

В main.c, после инициализации периферии CubeMX, вызывается:

app_la_init(
    &hspi1,
    CS_spi1_GPIO_Port,
    CS_spi1_Pin,
    &huart3,
    &huart2
);

Где:

Аргумент	Назначение
&hspi1	SPI для связи с FPGA
CS_spi1_GPIO_Port	GPIO port chip select FPGA
CS_spi1_Pin	GPIO pin chip select FPGA
&huart3	debug UART / human CLI
&huart2	ESP32 UART / machine protocol

После этого вызывается:

app_la_start();

Эта функция:

* проверяет FPGA version;
* печатает help в debug terminal;
* выводит prompt >.

⸻

5. Главный цикл

В основном цикле main.c должен вызываться только один application-level обработчик:

while (1)
{
    app_la_process();
}

app_la_process() проверяет, пришла ли полная строка с debug UART или ESP32 UART, и передаёт её в командный обработчик.

⸻

6. UART receive callback

HAL callback в main.c должен быть коротким:

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    app_la_uart_rx_cplt_callback(huart);
}

Вся логика определения UART, echo, line buffer и повторного запуска HAL_UART_Receive_IT() находится внутри app_la_console.c.

⸻

7. UART-интерфейсы

7.1 Debug UART

Debug UART предназначен для человека.

Обычно используется:

USART3

Через него доступны команды:

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
hex

Особенности:

* echo включён;
* после команды выводится prompt >;
* ответы человекочитаемые;
* waveform может печататься ASCII-графиком.

⸻

7.2 ESP32 UART

ESP32 UART предназначен для machine protocol.

Обычно используется:

USART2

Через него ESP32 отправляет команды:

@PING
@STATUS
@SHOW
@CFG
@RUN
@IMM
@READ

Особенности:

* echo выключен;
* ответы начинаются с @;
* данные передаются в HEX;
* формат удобен для парсинга ESP32.

⸻

8. Конфигурация capture

Текущая конфигурация хранится в структуре:

fpga_la_capture_config_t

Основные поля:

input_mode
sample_rate_div
sample_count
pretrigger_count
trigger_enable
trigger_channel
trigger_edge

Значения по умолчанию:

input_mode       = external
sample_rate      = 100 kHz
sample_count     = 256
pretrigger_count = 64
trigger_enable   = 1
trigger_channel  = CH0
trigger_edge     = rising

⸻

9. Sample buffer

STM32 хранит последний capture buffer в RAM.

Текущий размер:

#define APP_LA_MAX_SAMPLES 512u

Каждый sample занимает один byte:

bit0 → CH0
bit1 → CH1
bit2 → CH2
bit3 → CH3
bit4 → CH4
bit5 → CH5
bit6 → CH6
bit7 → CH7

Количество фактически считанных samples хранится отдельно:

s_samples_read

⸻

10. Human CLI commands

10.1 Help

help
h
?

Печатает список команд.

10.2 Show config

show
s

Показывает текущие параметры capture.

10.3 Set sample rate

rate 1000000

Переводит частоту в FPGA divider:

sample_rate_div = FPGA_CLK / sample_rate - 1

10.4 Set sample count

count 512

Задаёт количество samples.

10.5 Set pre-trigger

pre 128

Задаёт количество samples до trigger.

10.6 Set trigger channel

ch 0

Выбирает trigger channel.

10.7 Set trigger edge

rising
falling

10.8 Run capture

run

Запускает triggered capture.

10.9 Immediate capture

imm

Запускает immediate capture без ожидания trigger.

10.10 Print HEX

hex

Печатает последний capture buffer в HEX-формате.

⸻

11. Machine protocol commands

Machine protocol описан подробнее в docs/protocol.md.

На уровне STM32 поддерживаются команды:

@PING
@HELP
@STATUS
@SHOW
@CFG <rate_hz> <count> <pre> <ch> <R|F>
@RUN
@IMM
@READ

⸻

12. Capture sequence

12.1 Triggered capture

1. Принять конфигурацию.
2. Записать input mode в FPGA.
3. Записать sample rate divider.
4. Записать sample count.
5. Записать pre-trigger count.
6. Записать trigger config.
7. Очистить sampler.
8. Стартовать sampler.
9. Дождаться ARMED или DONE.
10. Дождаться DONE.
11. Считать SAMPLES_DONE.
12. Выполнить READ_RESET.
13. Считать samples из SAMPLER_DATA.
14. Сохранить samples в STM32 buffer.
15. Вернуть результат пользователю или ESP32.

12.2 Immediate capture

1. Отключить trigger.
2. Установить pre-trigger = 0.
3. Записать настройки в FPGA.
4. Очистить sampler.
5. Стартовать sampler.
6. Дождаться DONE.
7. Считать samples.

⸻

13. Test signal

В текущей версии тестовый сигнал генерируется STM32 через TIM2 callback.

Пример:

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &htim2)
    {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_9);
    }
}

Этот сигнал можно подключить к FPGA CH0 для проверки capture.

⸻

14. Правила дальнейшего развития STM32-кода

Чтобы проект оставался чистым:

* не добавлять новую прикладную логику в main.c;
* не смешивать UART-протокол с FPGA SPI-драйвером;
* не добавлять Web UI-логику в STM32;
* не делать fpga_spi зависимым от логического анализатора;
* держать machine protocol в app_la_console;
* держать FPGA register access в fpga_logic_analyzer;
* держать SPI transactions в fpga_spi.

⸻

15. Возможные улучшения STM32 firmware

* Увеличить APP_LA_MAX_SAMPLES
* Добавить binary protocol
* Добавить CRC/checksum
* Добавить sequence ID
* Добавить FPGA IRQ line для DONE
* Добавить timeout error details
* Добавить raw FPGA status command
* Добавить команду чтения register map
* Добавить отдельный модуль app_test_signal
* Добавить ring buffer для UART RX