#include "board.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"

static const uint8_t input_gpio[BOARD_NUM_INPUTS] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t output_gpio[BOARD_NUM_OUTPUTS] = {22, 21, 20, 19, 18, 17, 16};

/* AI0=GPIO28, AI1=GPIO27, AI2=GPIO26. ADC channel is gpio - 26, so the
 * channel order is the reverse of the AI numbering. */
static const uint8_t analog_gpio[BOARD_NUM_ANALOG] = {28, 27, 26};

void board_init(void) {
    for (int i = 0; i < BOARD_NUM_INPUTS; i++) {
        gpio_init(input_gpio[i]);
        gpio_set_dir(input_gpio[i], GPIO_IN);
        /* A released pushbutton leaves the pin floating; pull it down so an
         * open contact reads as 0 rather than picking up noise. */
        gpio_pull_down(input_gpio[i]);
    }

    for (int i = 0; i < BOARD_NUM_OUTPUTS; i++) {
        gpio_init(output_gpio[i]);
        gpio_set_dir(output_gpio[i], GPIO_OUT);
        gpio_put(output_gpio[i], 0);
    }

    gpio_init(BOARD_STS0_GPIO);
    gpio_set_dir(BOARD_STS0_GPIO, GPIO_OUT);
    gpio_put(BOARD_STS0_GPIO, 0);
    gpio_init(BOARD_STS1_GPIO);
    gpio_set_dir(BOARD_STS1_GPIO, GPIO_OUT);
    gpio_put(BOARD_STS1_GPIO, 0);

    adc_init();
    for (int i = 0; i < BOARD_NUM_ANALOG; i++) {
        adc_gpio_init(analog_gpio[i]);
    }
}

uint16_t board_read_inputs(void) {
    uint32_t all = gpio_get_all();
    uint16_t bits = 0;
    for (int i = 0; i < BOARD_NUM_INPUTS; i++) {
        if (all & (1u << input_gpio[i])) {
            bits |= (uint16_t)(1u << i);
        }
    }
    return bits;
}

void board_write_outputs(uint16_t bits) {
    for (int i = 0; i < BOARD_NUM_OUTPUTS; i++) {
        gpio_put(output_gpio[i], (bits >> i) & 1u);
    }
}

uint16_t board_read_analog(uint8_t ch) {
    if (ch >= BOARD_NUM_ANALOG) {
        return 0;
    }
    adc_select_input((uint)(analog_gpio[ch] - 26));
    return adc_read();
}

void board_status_led(uint8_t idx, bool on) {
    gpio_put(idx == 0 ? BOARD_STS0_GPIO : BOARD_STS1_GPIO, on);
}
