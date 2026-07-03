#include "fpga_logic_analyzer.h"

static HAL_StatusTypeDef fpga_la_write_u16(
    fpga_la_t *la,
    uint8_t reg_l,
    uint8_t reg_h,
    uint16_t value
)
{
    HAL_StatusTypeDef st;

    st = fpga_write_reg(la->spi, reg_l, (uint8_t)(value & 0xFF));
    if (st != HAL_OK) {
        return st;
    }

    return fpga_write_reg(la->spi, reg_h, (uint8_t)((value >> 8) & 0xFF));
}

static HAL_StatusTypeDef fpga_la_read_u16(
    fpga_la_t *la,
    uint8_t reg_l,
    uint8_t reg_h,
    uint16_t *value
)
{
    uint8_t lo = 0;
    uint8_t hi = 0;

    HAL_StatusTypeDef st;

    st = fpga_read_reg(la->spi, reg_l, &lo);
    if (st != HAL_OK) {
        return st;
    }

    st = fpga_read_reg(la->spi, reg_h, &hi);
    if (st != HAL_OK) {
        return st;
    }

    *value = ((uint16_t)hi << 8) | lo;
    return HAL_OK;
}

static HAL_StatusTypeDef fpga_la_control_pulse(
    fpga_la_t *la,
    uint8_t mask
)
{
    HAL_StatusTypeDef st;

    st = fpga_write_reg(la->spi, FPGA_REG_CONTROL, mask);
    if (st != HAL_OK) {
        return st;
    }

    return fpga_write_reg(la->spi, FPGA_REG_CONTROL, 0x00);
}

static uint8_t fpga_la_make_trigger_cfg(
    uint8_t enable,
    uint8_t channel,
    fpga_la_trigger_edge_t edge
)
{
    uint8_t cfg = 0;

    if (enable) {
        cfg |= FPGA_TRIGGER_ENABLE;
    }

    if (edge == FPGA_LA_TRIGGER_RISING) {
        cfg |= FPGA_TRIGGER_EDGE_RISING;
    }

    cfg |= (uint8_t)((channel & 0x07u) << FPGA_TRIGGER_CH_SHIFT);

    return cfg;
}

HAL_StatusTypeDef fpga_la_init(
    fpga_la_t *la,
    fpga_spi_t *spi
)
{
    if (la == NULL || spi == NULL) {
        return HAL_ERROR;
    }

    la->spi = spi;
    return HAL_OK;
}

HAL_StatusTypeDef fpga_la_check_version(
    fpga_la_t *la
)
{
    if (la == NULL || la->spi == NULL) {
        return HAL_ERROR;
    }

    uint8_t id = 0;
    uint8_t version = 0;

    HAL_StatusTypeDef st;

    st = fpga_read_reg(la->spi, FPGA_REG_ID, &id);
    if (st != HAL_OK) {
        return st;
    }

    if (id != FPGA_EXPECTED_ID) {
        return HAL_ERROR;
    }

    st = fpga_read_reg(la->spi, FPGA_REG_VERSION, &version);
    if (st != HAL_OK) {
        return st;
    }

    if (version != FPGA_EXPECTED_VERSION) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef fpga_la_set_input_mode(
    fpga_la_t *la,
    fpga_la_input_mode_t mode
)
{
    return fpga_write_reg(
        la->spi,
        FPGA_REG_INPUT_MODE,
        (uint8_t)mode
    );
}

HAL_StatusTypeDef fpga_la_set_test_pattern(
    fpga_la_t *la,
    uint8_t pattern
)
{
    return fpga_write_reg(
        la->spi,
        FPGA_REG_TEST_PATTERN,
        pattern
    );
}

HAL_StatusTypeDef fpga_la_set_sample_rate_div(
    fpga_la_t *la,
    uint16_t div
)
{
    return fpga_la_write_u16(
        la,
        FPGA_REG_SAMPLE_RATE_DIV_L,
        FPGA_REG_SAMPLE_RATE_DIV_H,
        div
    );
}

HAL_StatusTypeDef fpga_la_set_sample_count(
    fpga_la_t *la,
    uint16_t count
)
{
    return fpga_la_write_u16(
        la,
        FPGA_REG_SAMPLE_COUNT_L,
        FPGA_REG_SAMPLE_COUNT_H,
        count
    );
}

HAL_StatusTypeDef fpga_la_set_pretrigger_count(
    fpga_la_t *la,
    uint16_t count
)
{
    return fpga_la_write_u16(
        la,
        FPGA_REG_PRETRIGGER_COUNT_L,
        FPGA_REG_PRETRIGGER_COUNT_H,
        count
    );
}

HAL_StatusTypeDef fpga_la_set_trigger(
    fpga_la_t *la,
    uint8_t enable,
    uint8_t channel,
    fpga_la_trigger_edge_t edge
)
{
    uint8_t cfg = fpga_la_make_trigger_cfg(enable, channel, edge);

    return fpga_write_reg(
        la->spi,
        FPGA_REG_TRIGGER_CFG,
        cfg
    );
}

HAL_StatusTypeDef fpga_la_clear(
    fpga_la_t *la
)
{
    return fpga_la_control_pulse(la, FPGA_CONTROL_CLEAR);
}

HAL_StatusTypeDef fpga_la_start(
    fpga_la_t *la
)
{
    return fpga_la_control_pulse(la, FPGA_CONTROL_START);
}

HAL_StatusTypeDef fpga_la_read_reset(
    fpga_la_t *la
)
{
    return fpga_la_control_pulse(la, FPGA_CONTROL_READ_RESET);
}

HAL_StatusTypeDef fpga_la_get_status(
    fpga_la_t *la,
    uint8_t *status
)
{
    if (status == NULL) {
        return HAL_ERROR;
    }

    return fpga_read_reg(
        la->spi,
        FPGA_REG_STATUS,
        status
    );
}

HAL_StatusTypeDef fpga_la_wait_armed_or_done(
    fpga_la_t *la,
    uint32_t timeout,
    uint8_t *final_status
)
{
    if (final_status == NULL) {
        return HAL_ERROR;
    }

    uint8_t status = 0;
    HAL_StatusTypeDef st;

    while (timeout > 0) {
        st = fpga_la_get_status(la, &status);
        if (st != HAL_OK) {
            return st;
        }

        if ((status & (FPGA_STATUS_ARMED | FPGA_STATUS_DONE)) != 0) {
            *final_status = status;
            return HAL_OK;
        }

        timeout--;
    }

    *final_status = status;
    return HAL_TIMEOUT;
}

HAL_StatusTypeDef fpga_la_wait_done(
    fpga_la_t *la,
    uint32_t timeout,
    uint8_t *final_status
)
{
    if (final_status == NULL) {
        return HAL_ERROR;
    }

    uint8_t status = 0;
    HAL_StatusTypeDef st;

    while (timeout > 0) {
        st = fpga_la_get_status(la, &status);
        if (st != HAL_OK) {
            return st;
        }

        if ((status & FPGA_STATUS_DONE) != 0) {
            *final_status = status;
            return HAL_OK;
        }

        timeout--;
    }

    *final_status = status;
    return HAL_TIMEOUT;
}

HAL_StatusTypeDef fpga_la_get_captured_count(
    fpga_la_t *la,
    uint16_t *count
)
{
    if (count == NULL) {
        return HAL_ERROR;
    }

    return fpga_la_read_u16(
        la,
        FPGA_REG_SAMPLES_DONE_L,
        FPGA_REG_SAMPLES_DONE_H,
        count
    );
}

HAL_StatusTypeDef fpga_la_read_samples(
    fpga_la_t *la,
    uint8_t *buffer,
    uint16_t count
)
{
    if (buffer == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st;

    st = fpga_la_read_reset(la);
    if (st != HAL_OK) {
        return st;
    }

    for (uint16_t i = 0; i < count; i++) {
        st = fpga_read_reg(
            la->spi,
            FPGA_REG_SAMPLER_DATA,
            &buffer[i]
        );

        if (st != HAL_OK) {
            return st;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef fpga_la_capture_immediate(
    fpga_la_t *la,
    fpga_la_input_mode_t input_mode,
    uint16_t sample_rate_div,
    uint16_t sample_count,
    uint8_t *buffer,
    uint16_t buffer_size,
    uint16_t *samples_read
)
{
    if (buffer == NULL || samples_read == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st;
    uint8_t status = 0;
    uint16_t captured = 0;

    *samples_read = 0;

    st = fpga_la_set_input_mode(la, input_mode);
    if (st != HAL_OK) return st;

    st = fpga_la_set_trigger(la, 0, 0, FPGA_LA_TRIGGER_RISING);
    if (st != HAL_OK) return st;

    st = fpga_la_set_pretrigger_count(la, 0);
    if (st != HAL_OK) return st;

    st = fpga_la_set_sample_rate_div(la, sample_rate_div);
    if (st != HAL_OK) return st;

    st = fpga_la_set_sample_count(la, sample_count);
    if (st != HAL_OK) return st;

    st = fpga_la_clear(la);
    if (st != HAL_OK) return st;

    st = fpga_la_start(la);
    if (st != HAL_OK) return st;

    st = fpga_la_wait_done(la, 1000000, &status);
    if (st != HAL_OK) return st;

    st = fpga_la_get_captured_count(la, &captured);
    if (st != HAL_OK) return st;

    if (captured > buffer_size) {
        captured = buffer_size;
    }

    st = fpga_la_read_samples(la, buffer, captured);
    if (st != HAL_OK) return st;

    *samples_read = captured;
    return HAL_OK;
}

HAL_StatusTypeDef fpga_la_capture_triggered(
    fpga_la_t *la,
    uint16_t sample_rate_div,
    uint16_t sample_count,
    uint16_t pretrigger_count,
    uint8_t trigger_channel,
    fpga_la_trigger_edge_t trigger_edge,
    uint8_t *buffer,
    uint16_t buffer_size,
    uint16_t *samples_read
)
{
    if (buffer == NULL || samples_read == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st;
    uint8_t status = 0;
    uint16_t captured = 0;

    *samples_read = 0;

    st = fpga_la_set_input_mode(la, FPGA_LA_INPUT_EXTERNAL);
    if (st != HAL_OK) return st;

    st = fpga_la_set_sample_rate_div(la, sample_rate_div);
    if (st != HAL_OK) return st;

    st = fpga_la_set_sample_count(la, sample_count);
    if (st != HAL_OK) return st;

    st = fpga_la_set_pretrigger_count(la, pretrigger_count);
    if (st != HAL_OK) return st;

    st = fpga_la_set_trigger(
        la,
        1,
        trigger_channel,
        trigger_edge
    );
    if (st != HAL_OK) return st;

    st = fpga_la_clear(la);
    if (st != HAL_OK) return st;

    st = fpga_la_start(la);
    if (st != HAL_OK) return st;

    /*
     * With a fast input signal, DONE can happen before STM32 polls ARMED.
     * Therefore wait for ARMED or DONE first.
     */
    st = fpga_la_wait_armed_or_done(la, 1000000, &status);
    if (st != HAL_OK) return st;

    if ((status & FPGA_STATUS_DONE) == 0) {
        st = fpga_la_wait_done(la, 1000000, &status);
        if (st != HAL_OK) return st;
    }

    st = fpga_la_get_captured_count(la, &captured);
    if (st != HAL_OK) return st;

    if (captured > buffer_size) {
        captured = buffer_size;
    }

    st = fpga_la_read_samples(la, buffer, captured);
    if (st != HAL_OK) return st;

    *samples_read = captured;
    return HAL_OK;
}

HAL_StatusTypeDef fpga_la_capture(
    fpga_la_t *la,
    const fpga_la_capture_config_t *cfg,
    uint8_t *buffer,
    uint16_t buffer_size,
    uint16_t *samples_read
)
{
    if (la == NULL || cfg == NULL || buffer == NULL || samples_read == NULL) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st;
    uint8_t status = 0;
    uint16_t captured = 0;

    *samples_read = 0;

    /*
     * Clamp pretrigger_count on STM32 side too.
     * FPGA also protects itself, but this keeps config sane.
     */
    uint16_t pretrigger_count = cfg->pretrigger_count;

    if (pretrigger_count > cfg->sample_count) {
        pretrigger_count = cfg->sample_count;
    }

    st = fpga_la_set_input_mode(la, cfg->input_mode);
    if (st != HAL_OK) return st;

    st = fpga_la_set_sample_rate_div(la, cfg->sample_rate_div);
    if (st != HAL_OK) return st;

    st = fpga_la_set_sample_count(la, cfg->sample_count);
    if (st != HAL_OK) return st;

    st = fpga_la_set_pretrigger_count(la, pretrigger_count);
    if (st != HAL_OK) return st;

    st = fpga_la_set_trigger(
        la,
        cfg->trigger_enable,
        cfg->trigger_channel,
        cfg->trigger_edge
    );
    if (st != HAL_OK) return st;

    st = fpga_la_clear(la);
    if (st != HAL_OK) return st;

    st = fpga_la_start(la);
    if (st != HAL_OK) return st;

    if (cfg->trigger_enable) {
        /*
         * Trigger can complete before STM32 polls ARMED.
         */
        st = fpga_la_wait_armed_or_done(la, 1000000, &status);
        if (st != HAL_OK) return st;

        if ((status & FPGA_STATUS_DONE) == 0) {
            st = fpga_la_wait_done(la, 1000000, &status);
            if (st != HAL_OK) return st;
        }
    } else {
        st = fpga_la_wait_done(la, 1000000, &status);
        if (st != HAL_OK) return st;
    }

    st = fpga_la_get_captured_count(la, &captured);
    if (st != HAL_OK) return st;

    if (captured > buffer_size) {
        captured = buffer_size;
    }

    st = fpga_la_read_samples(la, buffer, captured);
    if (st != HAL_OK) return st;

    *samples_read = captured;
    return HAL_OK;
}

