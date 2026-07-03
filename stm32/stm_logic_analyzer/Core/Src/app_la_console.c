#include "app_la_console.h"
#include "fpga_logic_analyzer.h"
#include <stdio.h>
#include <string.h>

#define APP_LA_CMD_BUF_SIZE 64u
#define APP_LA_MAX_SAMPLES  512u
#define APP_LA_FPGA_CLK_HZ  50000000u

typedef struct
{
    uint8_t rx_byte;
    char line_buf[APP_LA_CMD_BUF_SIZE];
    volatile uint16_t line_len;
    volatile uint8_t line_ready;
} app_uart_line_t;

static UART_HandleTypeDef *s_debug_uart = NULL;
static UART_HandleTypeDef *s_esp_uart = NULL;
static UART_HandleTypeDef *s_reply_uart = NULL;

static app_uart_line_t s_debug_line;
static app_uart_line_t s_esp_line;
static char s_cmd_work_buf[APP_LA_CMD_BUF_SIZE];

static fpga_spi_t s_fpga;
static fpga_la_t s_la;

static uint8_t s_samples[APP_LA_MAX_SAMPLES];
static uint16_t s_samples_read = 0;

static fpga_la_capture_config_t s_cfg = {
    .input_mode       = FPGA_LA_INPUT_EXTERNAL,
    .sample_rate_div  = 499,       // 100 kHz with 50 MHz FPGA clock
    .sample_count     = 256,
    .pretrigger_count = 64,
    .trigger_enable   = 1,
    .trigger_channel  = 0,
    .trigger_edge     = FPGA_LA_TRIGGER_RISING
};

static void app_uart_print(const char *s)
{
    if (s_reply_uart == NULL || s == NULL) {
        return;
    }

    HAL_UART_Transmit(
        s_reply_uart,
        (uint8_t *)s,
        strlen(s),
        HAL_MAX_DELAY
    );
}

static void app_debug_print(const char *s)
{
    if (s_debug_uart == NULL || s == NULL) {
        return;
    }

    HAL_UART_Transmit(
        s_debug_uart,
        (uint8_t *)s,
        strlen(s),
        HAL_MAX_DELAY
    );
}

static void app_str_trim(char *s)
{
    if (s == NULL) {
        return;
    }

    size_t len = strlen(s);

    while (len > 0) {
        char c = s[len - 1];

        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            s[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }

    char *p = s;

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (p != s) {
        memmove(s, p, strlen(p) + 1u);
    }
}

static int app_starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static uint16_t app_sample_rate_to_div(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0u) {
        return 0u;
    }

    if (sample_rate_hz >= APP_LA_FPGA_CLK_HZ) {
        return 0u;
    }

    uint32_t div = APP_LA_FPGA_CLK_HZ / sample_rate_hz;

    if (div > 0u) {
        div -= 1u;
    }

    if (div > 0xFFFFu) {
        div = 0xFFFFu;
    }

    return (uint16_t)div;
}

static uint32_t app_div_to_sample_rate(uint16_t div)
{
    return APP_LA_FPGA_CLK_HZ / ((uint32_t)div + 1u);
}

static void app_print_raw_samples(const uint8_t *samples, uint16_t count, uint16_t max_print)
{
    char msg[32];

    app_uart_print("Raw samples:\r\n");

    if (count > max_print) {
        count = max_print;
    }

    for (uint16_t i = 0; i < count; i++) {
        snprintf(msg, sizeof(msg), "%02X ", samples[i]);
        app_uart_print(msg);

        if (((i + 1u) % 16u) == 0u) {
            app_uart_print("\r\n");
        }
    }

    app_uart_print("\r\n");
}

static void app_print_waveform_multi_ch(
    const uint8_t *samples,
    uint16_t count,
    uint8_t first_channel,
    uint8_t channel_count,
    int32_t marker_index
)
{
    char msg[96];

    if (first_channel > 7u) {
        return;
    }

    if ((first_channel + channel_count) > 8u) {
        channel_count = 8u - first_channel;
    }

    snprintf(msg, sizeof(msg),
             "Waveform CH%u..CH%u, marker=%ld:\r\n",
             first_channel,
             (uint8_t)(first_channel + channel_count - 1u),
             (long)marker_index);
    app_uart_print(msg);

    for (uint16_t line_start = 0; line_start < count; line_start += 80u) {
        uint16_t line_end = line_start + 80u;

        if (line_end > count) {
            line_end = count;
        }

        snprintf(msg, sizeof(msg), "\r\n[%u..%u]\r\n", line_start, (uint16_t)(line_end - 1u));
        app_uart_print(msg);

        if (marker_index >= (int32_t)line_start && marker_index < (int32_t)line_end) {
            app_uart_print("TRG: ");

            for (uint16_t i = line_start; i < line_end; i++) {
                app_uart_print(((int32_t)i == marker_index) ? "^" : " ");
            }

            app_uart_print("\r\n");
        }

        for (uint8_t ch = first_channel; ch < (first_channel + channel_count); ch++) {
            uint8_t mask = (uint8_t)(1u << ch);

            snprintf(msg, sizeof(msg), "CH%u: ", ch);
            app_uart_print(msg);

            for (uint16_t i = line_start; i < line_end; i++) {
                app_uart_print((samples[i] & mask) ? "#" : "_");
            }

            app_uart_print("\r\n");
        }
    }

    app_uart_print("\r\n");
}

static void app_print_config(void)
{
    char msg[128];
    uint32_t sample_rate_hz = app_div_to_sample_rate(s_cfg.sample_rate_div);

    app_uart_print("\r\nCurrent config:\r\n");

    snprintf(msg, sizeof(msg), "  sample_rate_div  = %u\r\n", s_cfg.sample_rate_div);
    app_uart_print(msg);

    snprintf(msg, sizeof(msg), "  sample_rate_hz   = %lu\r\n", sample_rate_hz);
    app_uart_print(msg);

    snprintf(msg, sizeof(msg), "  sample_count     = %u\r\n", s_cfg.sample_count);
    app_uart_print(msg);

    snprintf(msg, sizeof(msg), "  pretrigger_count = %u\r\n", s_cfg.pretrigger_count);
    app_uart_print(msg);

    snprintf(msg, sizeof(msg), "  trigger_channel  = CH%u\r\n", s_cfg.trigger_channel);
    app_uart_print(msg);

    snprintf(msg, sizeof(msg),
             "  trigger_edge     = %s\r\n",
             s_cfg.trigger_edge == FPGA_LA_TRIGGER_RISING ? "rising" : "falling");
    app_uart_print(msg);

    app_uart_print("\r\n");
}

static void app_print_help(void)
{
    app_uart_print("\r\nCommands:\r\n");
    app_uart_print("  help / h / ?\r\n");
    app_uart_print("  show / s\r\n");
    app_uart_print("\r\n");
    app_uart_print("Capture:\r\n");
    app_uart_print("  imm / i          - immediate capture\r\n");
    app_uart_print("  run / t          - triggered capture\r\n");
    app_uart_print("  hex              - print last capture as HEX\r\n");
    app_uart_print("\r\n");
    app_uart_print("Config:\r\n");
    app_uart_print("  rate 100000      - set sample rate Hz\r\n");
    app_uart_print("  count 256        - set sample count\r\n");
    app_uart_print("  pre 64           - set pre-trigger samples\r\n");
    app_uart_print("  ch 0             - set trigger channel\r\n");
    app_uart_print("  rising           - trigger rising edge\r\n");
    app_uart_print("  falling          - trigger falling edge\r\n");
    app_uart_print("  trig ch0 rising  - set channel and edge\r\n");
    app_uart_print("\r\n");
    app_uart_print("Machine commands for ESP32:\r\n");
    app_uart_print("  @PING, @STATUS, @SHOW, @CFG, @RUN, @IMM, @READ\r\n");
    app_uart_print("\r\n");
}

static void app_fpga_check(void)
{
    app_uart_print("\r\nFPGA logic analyzer check\r\n");

    if (fpga_la_check_version(&s_la) == HAL_OK) {
        app_uart_print("FPGA LA version check OK\r\n");
    } else {
        app_uart_print("FPGA LA version check FAILED\r\n");
    }
}

static void app_capture_immediate(void)
{
    uint16_t samples_read = 0;
    fpga_la_capture_config_t cfg = s_cfg;

    cfg.trigger_enable = 0;
    cfg.pretrigger_count = 0;
    cfg.input_mode = FPGA_LA_INPUT_EXTERNAL;

    app_uart_print("\r\nImmediate capture\r\n");

    HAL_StatusTypeDef st = fpga_la_capture(
        &s_la,
        &cfg,
        s_samples,
        sizeof(s_samples),
        &samples_read
    );

    if (st != HAL_OK) {
        app_uart_print("Immediate capture FAILED\r\n");
        return;
    }

    s_samples_read = samples_read;

    app_uart_print("Immediate capture OK\r\n");
    app_print_raw_samples(s_samples, s_samples_read, 64);
    app_print_waveform_multi_ch(s_samples, s_samples_read, 0, 4, -1);
}

static void app_capture_triggered(void)
{
    uint16_t samples_read = 0;
    fpga_la_capture_config_t cfg = s_cfg;

    cfg.trigger_enable = 1;
    cfg.input_mode = FPGA_LA_INPUT_EXTERNAL;

    app_uart_print("\r\nTriggered capture\r\n");

    HAL_StatusTypeDef st = fpga_la_capture(
        &s_la,
        &cfg,
        s_samples,
        sizeof(s_samples),
        &samples_read
    );

    if (st != HAL_OK) {
        app_uart_print("Triggered capture FAILED\r\n");
        return;
    }

    s_samples_read = samples_read;

    app_uart_print("Triggered capture OK\r\n");
    app_print_raw_samples(s_samples, s_samples_read, 64);
    app_print_waveform_multi_ch(s_samples, s_samples_read, 0, 4, cfg.pretrigger_count);
}

static void app_print_last_hex(void)
{
    char msg[64];

    if (s_samples_read == 0u) {
        app_uart_print("ERR: no capture data\r\n");
        return;
    }

    snprintf(msg, sizeof(msg), "BEGIN_HEX count=%u\r\n", s_samples_read);
    app_uart_print(msg);

    for (uint16_t i = 0; i < s_samples_read; i++) {
        snprintf(msg, sizeof(msg), "%02X", s_samples[i]);
        app_uart_print(msg);

        if (((i + 1u) % 32u) == 0u) {
            app_uart_print("\r\n");
        }
    }

    app_uart_print("\r\nEND_HEX\r\n");
}

static void app_machine_help(void)
{
    app_uart_print("@HELP\r\n");
    app_uart_print("@PING\r\n");
    app_uart_print("@STATUS\r\n");
    app_uart_print("@SHOW\r\n");
    app_uart_print("@CFG <rate_hz> <count> <pre> <ch> <R|F>\r\n");
    app_uart_print("@RUN\r\n");
    app_uart_print("@IMM\r\n");
    app_uart_print("@READ\r\n");
    app_uart_print("@END\r\n");
}

static void app_machine_show(void)
{
    char msg[160];
    uint32_t sample_rate_hz = app_div_to_sample_rate(s_cfg.sample_rate_div);

    snprintf(msg, sizeof(msg),
             "@CFG rate=%lu div=%u count=%u pre=%u ch=%u edge=%c\r\n",
             sample_rate_hz,
             s_cfg.sample_rate_div,
             s_cfg.sample_count,
             s_cfg.pretrigger_count,
             s_cfg.trigger_channel,
             s_cfg.trigger_edge == FPGA_LA_TRIGGER_RISING ? 'R' : 'F');

    app_uart_print(msg);
}

static void app_machine_status(void)
{
    char msg[96];
    HAL_StatusTypeDef st = fpga_la_check_version(&s_la);

    snprintf(msg, sizeof(msg),
             "@STATUS fpga=%s samples=%u\r\n",
             st == HAL_OK ? "OK" : "ERR",
             s_samples_read);

    app_uart_print(msg);
}

static void app_machine_read_hex(void)
{
    char msg[64];

    if (s_samples_read == 0u) {
        app_uart_print("@ERR NO_DATA\r\n");
        return;
    }

    snprintf(msg, sizeof(msg), "@DATA %u\r\n", s_samples_read);
    app_uart_print(msg);

    for (uint16_t i = 0; i < s_samples_read; i++) {
        snprintf(msg, sizeof(msg), "%02X", s_samples[i]);
        app_uart_print(msg);

        if (((i + 1u) % 32u) == 0u) {
            app_uart_print("\r\n");
        }
    }

    app_uart_print("\r\n@END\r\n");
}

static void app_machine_run(uint8_t triggered)
{
    uint16_t samples_read = 0;
    fpga_la_capture_config_t cfg = s_cfg;

    cfg.input_mode = FPGA_LA_INPUT_EXTERNAL;

    if (triggered) {
        cfg.trigger_enable = 1;
    } else {
        cfg.trigger_enable = 0;
        cfg.pretrigger_count = 0;
    }

    HAL_StatusTypeDef st = fpga_la_capture(
        &s_la,
        &cfg,
        s_samples,
        sizeof(s_samples),
        &samples_read
    );

    if (st != HAL_OK) {
        app_uart_print(triggered ? "@ERR RUN_FAILED\r\n" : "@ERR IMM_FAILED\r\n");
        return;
    }

    s_samples_read = samples_read;

    char msg[64];
    snprintf(msg, sizeof(msg), "@DONE %u\r\n", s_samples_read);
    app_uart_print(msg);
}

static void app_machine_cfg(char *cmd)
{
    uint32_t rate = 0;
    uint32_t count = 0;
    uint32_t pre = 0;
    uint32_t ch = 0;
    char edge = 0;

    int n = sscanf(cmd, "@CFG %lu %lu %lu %lu %c", &rate, &count, &pre, &ch, &edge);

    if (n != 5) {
        app_uart_print("@ERR CFG_USAGE\r\n");
        return;
    }

    if (count < 1u) {
        count = 1u;
    }

    if (count > sizeof(s_samples)) {
        count = sizeof(s_samples);
    }

    if (pre > count) {
        pre = count;
    }

    if (ch > 7u) {
        app_uart_print("@ERR BAD_CHANNEL\r\n");
        return;
    }

    if (edge == 'R' || edge == 'r') {
        s_cfg.trigger_edge = FPGA_LA_TRIGGER_RISING;
    } else if (edge == 'F' || edge == 'f') {
        s_cfg.trigger_edge = FPGA_LA_TRIGGER_FALLING;
    } else {
        app_uart_print("@ERR BAD_EDGE\r\n");
        return;
    }

    s_cfg.sample_rate_div = app_sample_rate_to_div(rate);
    s_cfg.sample_count = (uint16_t)count;
    s_cfg.pretrigger_count = (uint16_t)pre;
    s_cfg.trigger_channel = (uint8_t)ch;
    s_cfg.trigger_enable = 1;
    s_cfg.input_mode = FPGA_LA_INPUT_EXTERNAL;

    app_uart_print("@OK\r\n");
}

static int app_handle_machine_command(char *cmd)
{
    app_str_trim(cmd);

    if (cmd[0] != '@') {
        return 0;
    }

    if (strcmp(cmd, "@PING") == 0) {
        app_uart_print("@PONG\r\n");
        return 1;
    }

    if (strcmp(cmd, "@HELP") == 0) {
        app_machine_help();
        return 1;
    }

    if (strcmp(cmd, "@STATUS") == 0) {
        app_machine_status();
        return 1;
    }

    if (strcmp(cmd, "@SHOW") == 0) {
        app_machine_show();
        return 1;
    }

    if (app_starts_with(cmd, "@CFG ")) {
        app_machine_cfg(cmd);
        return 1;
    }

    if (strcmp(cmd, "@RUN") == 0) {
        app_machine_run(1);
        return 1;
    }

    if (strcmp(cmd, "@IMM") == 0) {
        app_machine_run(0);
        return 1;
    }

    if (strcmp(cmd, "@READ") == 0) {
        app_machine_read_hex();
        return 1;
    }

    app_uart_print("@ERR UNKNOWN\r\n");
    return 1;
}

static void app_handle_human_command(char *cmd)
{
    char msg[128];

    app_str_trim(cmd);

    if (cmd[0] == '\0') {
        return;
    }

    if (app_handle_machine_command(cmd)) {
        return;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
        app_print_help();
        return;
    }

    if (strcmp(cmd, "show") == 0 || strcmp(cmd, "s") == 0) {
        app_print_config();
        return;
    }

    if (strcmp(cmd, "imm") == 0 || strcmp(cmd, "i") == 0) {
        app_capture_immediate();
        return;
    }

    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "t") == 0) {
        app_capture_triggered();
        return;
    }

    if (strcmp(cmd, "hex") == 0) {
        app_print_last_hex();
        return;
    }

    if (app_starts_with(cmd, "rate ")) {
        uint32_t rate = 0;

        if (sscanf(cmd + 5, "%lu", &rate) == 1) {
            s_cfg.sample_rate_div = app_sample_rate_to_div(rate);

            snprintf(msg, sizeof(msg), "rate set: %lu Hz, div=%u\r\n", rate, s_cfg.sample_rate_div);
            app_uart_print(msg);
        } else {
            app_uart_print("ERR: usage rate 100000\r\n");
        }

        return;
    }

    if (app_starts_with(cmd, "count ")) {
        uint32_t count = 0;

        if (sscanf(cmd + 6, "%lu", &count) == 1) {
            if (count < 1u) {
                count = 1u;
            }

            if (count > sizeof(s_samples)) {
                count = sizeof(s_samples);
            }

            s_cfg.sample_count = (uint16_t)count;

            if (s_cfg.pretrigger_count > s_cfg.sample_count) {
                s_cfg.pretrigger_count = s_cfg.sample_count;
            }

            snprintf(msg, sizeof(msg), "sample_count set: %u\r\n", s_cfg.sample_count);
            app_uart_print(msg);
        } else {
            app_uart_print("ERR: usage count 256\r\n");
        }

        return;
    }

    if (app_starts_with(cmd, "pre ")) {
        uint32_t pre = 0;

        if (sscanf(cmd + 4, "%lu", &pre) == 1) {
            if (pre > s_cfg.sample_count) {
                pre = s_cfg.sample_count;
            }

            s_cfg.pretrigger_count = (uint16_t)pre;

            snprintf(msg, sizeof(msg), "pretrigger_count set: %u\r\n", s_cfg.pretrigger_count);
            app_uart_print(msg);
        } else {
            app_uart_print("ERR: usage pre 64\r\n");
        }

        return;
    }

    if (app_starts_with(cmd, "ch ")) {
        uint32_t ch = 0;

        if (sscanf(cmd + 3, "%lu", &ch) == 1 && ch < 8u) {
            s_cfg.trigger_channel = (uint8_t)ch;

            snprintf(msg, sizeof(msg), "trigger_channel set: CH%u\r\n", s_cfg.trigger_channel);
            app_uart_print(msg);
        } else {
            app_uart_print("ERR: usage ch 0..7\r\n");
        }

        return;
    }

    if (strcmp(cmd, "rising") == 0) {
        s_cfg.trigger_edge = FPGA_LA_TRIGGER_RISING;
        app_uart_print("trigger edge set: rising\r\n");
        return;
    }

    if (strcmp(cmd, "falling") == 0) {
        s_cfg.trigger_edge = FPGA_LA_TRIGGER_FALLING;
        app_uart_print("trigger edge set: falling\r\n");
        return;
    }

    if (app_starts_with(cmd, "trig ")) {
        uint32_t ch = 0;
        char edge[16];

        if (sscanf(cmd, "trig ch%lu %15s", &ch, edge) == 2 && ch < 8u) {
            s_cfg.trigger_channel = (uint8_t)ch;

            if (strcmp(edge, "rising") == 0) {
                s_cfg.trigger_edge = FPGA_LA_TRIGGER_RISING;
            } else if (strcmp(edge, "falling") == 0) {
                s_cfg.trigger_edge = FPGA_LA_TRIGGER_FALLING;
            } else {
                app_uart_print("ERR: edge must be rising or falling\r\n");
                return;
            }

            snprintf(msg, sizeof(msg),
                     "trigger set: CH%u %s\r\n",
                     s_cfg.trigger_channel,
                     s_cfg.trigger_edge == FPGA_LA_TRIGGER_RISING ? "rising" : "falling");
            app_uart_print(msg);
        } else {
            app_uart_print("ERR: usage trig ch0 rising\r\n");
        }

        return;
    }

    app_uart_print("ERR: unknown command. Type help\r\n");
}

static void app_uart_collect_byte(
    UART_HandleTypeDef *huart,
    app_uart_line_t *line,
    uint8_t echo
)
{
    uint8_t ch = line->rx_byte;

    if (line->line_ready) {
        return;
    }

    if (ch == '\r' || ch == '\n') {
        if (line->line_len > 0u) {
            line->line_buf[line->line_len] = '\0';
            line->line_ready = 1u;
        }

        if (echo && huart == s_debug_uart) {
            const char *nl = "\r\n";
            HAL_UART_Transmit(s_debug_uart, (uint8_t *)nl, 2, 10);
        }

        return;
    }

    if (ch == 0x08u || ch == 0x7Fu) {
        if (line->line_len > 0u) {
            line->line_len--;

            if (echo && huart == s_debug_uart) {
                const char *bs = "\b \b";
                HAL_UART_Transmit(s_debug_uart, (uint8_t *)bs, 3, 10);
            }
        }

        return;
    }

    if (ch >= 32u && ch <= 126u) {
        if (line->line_len < (APP_LA_CMD_BUF_SIZE - 1u)) {
            line->line_buf[line->line_len] = (char)ch;
            line->line_len++;

            if (echo && huart == s_debug_uart) {
                HAL_UART_Transmit(s_debug_uart, &ch, 1, 10);
            }
        }
    }
}

static void app_process_line(app_uart_line_t *line, UART_HandleTypeDef *reply_uart, uint8_t is_esp)
{
    __disable_irq();

    strncpy(s_cmd_work_buf, line->line_buf, APP_LA_CMD_BUF_SIZE);
    s_cmd_work_buf[APP_LA_CMD_BUF_SIZE - 1u] = '\0';

    line->line_len = 0u;
    line->line_ready = 0u;
    memset(line->line_buf, 0, sizeof(line->line_buf));

    __enable_irq();

    if (is_esp) {
        app_debug_print("\r\nESP32 CMD: ");
        app_debug_print(s_cmd_work_buf);
        app_debug_print("\r\n");
    }

    s_reply_uart = reply_uart;
    app_handle_human_command(s_cmd_work_buf);
    s_reply_uart = s_debug_uart;

    if (!is_esp) {
        app_uart_print("> ");
    }
}

void app_la_init(
    SPI_HandleTypeDef *hspi,
    GPIO_TypeDef *fpga_cs_port,
    uint16_t fpga_cs_pin,
    UART_HandleTypeDef *debug_uart,
    UART_HandleTypeDef *esp_uart
)
{
    s_debug_uart = debug_uart;
    s_esp_uart = esp_uart;
    s_reply_uart = s_debug_uart;

    memset(&s_debug_line, 0, sizeof(s_debug_line));
    memset(&s_esp_line, 0, sizeof(s_esp_line));

    fpga_init(&s_fpga, hspi, fpga_cs_port, fpga_cs_pin);
    fpga_la_init(&s_la, &s_fpga);

    if (s_debug_uart != NULL) {
        HAL_UART_Receive_IT(s_debug_uart, &s_debug_line.rx_byte, 1);
    }

    if (s_esp_uart != NULL) {
        HAL_UART_Receive_IT(s_esp_uart, &s_esp_line.rx_byte, 1);
    }
}

void app_la_start(void)
{
    s_reply_uart = s_debug_uart;
    app_fpga_check();
    app_print_help();
    app_uart_print("> ");
}

void app_la_process(void)
{
    if (s_debug_line.line_ready) {
        app_process_line(&s_debug_line, s_debug_uart, 0);
    }

    if (s_esp_line.line_ready) {
        app_process_line(&s_esp_line, s_esp_uart, 1);
    }
}

void app_la_uart_rx_cplt_callback(UART_HandleTypeDef *huart)
{
    if (huart == s_debug_uart) {
        app_uart_collect_byte(huart, &s_debug_line, 1);
        HAL_UART_Receive_IT(s_debug_uart, &s_debug_line.rx_byte, 1);
    }
    else if (huart == s_esp_uart) {
        app_uart_collect_byte(huart, &s_esp_line, 0);
        HAL_UART_Receive_IT(s_esp_uart, &s_esp_line.rx_byte, 1);
    }
}
