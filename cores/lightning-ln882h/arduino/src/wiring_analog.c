/* Copyright (c) Etienne Le Cousin 2025-01-19. */

#include <ArduinoPrivate.h>

#ifndef PIN_ADC1
#define PIN_ADC1 -1
#endif
#ifndef PIN_ADC2
#define PIN_ADC2 -1
#endif
#ifndef PIN_ADC3
#define PIN_ADC3 -1
#endif
#ifndef PIN_ADC4
#define PIN_ADC4 -1
#endif
#ifndef PIN_ADC5
#define PIN_ADC5 -1
#endif
#ifndef PIN_ADC6
#define PIN_ADC6 -1
#endif
#ifndef PIN_ADC7
#define PIN_ADC7 -1
#endif

const uint32_t adc_channels[] = {
	-1,
	PIN_ADC1,
	PIN_ADC2,
	PIN_ADC3,
	PIN_ADC4,
	PIN_ADC5,
	PIN_ADC6,
	PIN_ADC7,
};

static adc_ch_t pinToAdcCh(uint32_t gpio) {
	for (uint8_t i = 0; i < ADC_CH_NUM; i++) {
		if (adc_channels[i] == gpio)
			return (adc_ch_t)(1 << i);
	}
	return ADC_CH0;
}

uint16_t analogReadVoltage(pin_size_t pinNumber) {
	uint16_t ret = 0;
	pinCheckGetInfo(pinNumber, PIN_ADC, 0);

	hal_gpio_pin_mode_set(GPIO_GET_BASE(pin->gpio), GPIO_GET_PIN(pin->gpio), GPIO_MODE_ANALOG);

	adc_ch_t ch = pinToAdcCh(pin->gpio);
	adc_init_t_def adc_init;

	memset(&adc_init, 0, sizeof(adc_init_t_def));
	adc_init.adc_ch				 = ch;
	adc_init.adc_conv_mode		 = ADC_CONV_MODE_CONTINUE;
	adc_init.adc_presc			 = 80;
	adc_init.adc_ov_smp_ratio	 = ADC_OVER_SAMPLING_RATIO_X8;
	adc_init.adc_ov_smp_ratio_en = ADC_OVER_SAMPLING_EN_STATUS_BIT0;
	hal_adc_init(ADC_BASE, &adc_init);

	hal_adc_en(ADC_BASE, HAL_ENABLE);

	hal_adc_start_conv(ADC_BASE);

	while (hal_adc_get_conv_status(ADC_BASE, ch) == 0)
		;

	ret = hal_adc_get_data(ADC_BASE, ch);

	hal_adc_clr_conv_status(ADC_BASE, ch);

	return (uint16_t)(3700UL * ret / 4095);
}

uint16_t analogReadMaxVoltage(pin_size_t pinNumber) {
	return 3300;
}

// PWM: the "advanced timers" of the LN882H, as in the SDK's PWM driver
// (project/mcu_peripheral_driver_demo/PWM/bsp/ln_drv_pwm.c). Six timers with
// two channels each; a pin gets the next free channel on its first
// analogWrite() and keeps it. Both channels of a timer share its frequency.

#define PWM_CHANNELS		  12
#define PWM_TIMERS			  (PWM_CHANNELS / 2)
#define PWM_SLOW_CLOCK_HZ	  1000000
#define PWM_FAST_THRESHOLD_HZ 10000

static uint8_t pwmChannelsUsed = 0;
static uint32_t pwmTimerPeriod[PWM_TIMERS]; // period (us) a timer is set up for, 0 = not yet

static uint32_t pwmTimerBase(uint8_t channel) {
	return ADV_TIMER_0_BASE + 0x20 * (channel / 2);
}

// Sets the frequency of a timer. Below 10 kHz it counts at 1 MHz, above at the
// APB clock (as the SDK driver does); the load value is 16 bit.
static void pwmSetupTimer(uint8_t channel, uint32_t periodUs) {
	uint32_t base = pwmTimerBase(channel);
	uint32_t hz	  = 1000000 / periodUs;

	adv_tim_init_t_def init;
	memset(&init, 0, sizeof(init));
	if (hz >= PWM_FAST_THRESHOLD_HZ) {
		init.adv_tim_clk_div	= 0;
		init.adv_tim_load_value = hal_clock_get_apb0_clk() / hz - 2;
	} else {
		init.adv_tim_clk_div	= hal_clock_get_apb0_clk() / PWM_SLOW_CLOCK_HZ - 1;
		init.adv_tim_load_value = periodUs > 0xFFFF ? 0xFFFF : periodUs - 2;
	}
	init.adv_tim_dead_en	= ADV_TIMER_DEAD_DIS;
	init.adv_tim_cnt_mode	= ADV_TIMER_CNT_MODE_INC;
	init.adv_tim_cha_inv_en = ADV_TIMER_CHA_INV_EN;
	init.adv_tim_chb_inv_en = ADV_TIMER_CHB_INV_EN;

	// The other channel of the timer keeps its compare value (and so roughly its duty cycle).
	init.adv_tim_cmp_a_value = hal_adv_tim_get_comp_a(base);
	init.adv_tim_cmp_b_value = hal_adv_tim_get_comp_b(base);
	hal_adv_tim_init(base, &init);
	pwmTimerPeriod[channel / 2] = periodUs;
}

static void pwmEnableChannel(uint8_t channel, hal_en_t enable) {
	if (channel % 2 == 0)
		hal_adv_tim_a_en(pwmTimerBase(channel), enable);
	else
		hal_adv_tim_b_en(pwmTimerBase(channel), enable);
}

void analogWrite(pin_size_t pinNumber, int value) {
	// Every pin with an alternate function can be a PWM output.
	pinCheckGetData(pinNumber, PIN_NONE, );
	uint32_t gpioBase	= GPIO_GET_BASE(pin->gpio);
	gpio_pin_t gpioPin	= GPIO_GET_PIN(pin->gpio);
	uint32_t periodUs	= _analogWritePeriod > 0 ? _analogWritePeriod : 1;

	// GPIO can't be used together with PWM
	pinRemoveMode(pin, PIN_GPIO | PIN_IRQ);

	if (!data->pwmChannel) {
		if (pwmChannelsUsed >= PWM_CHANNELS)
			return;
		data->pwmChannel = ++pwmChannelsUsed;
	}
	uint8_t channel = data->pwmChannel - 1;
	uint32_t base	= pwmTimerBase(channel);

	if (!pinEnabled(pin, PIN_PWM)) {
		pinEnable(pin, PIN_PWM);
		hal_gpio_pin_afio_select(gpioBase, gpioPin, (afio_function_t)(ADV_TIMER_PWM0 + channel));
		if (channel % 2 == 0)
			hal_adv_tim_set_comp_a(base, 0);
		else
			hal_adv_tim_set_comp_b(base, 0);
	}
	if (pwmTimerPeriod[channel / 2] != periodUs)
		pwmSetupTimer(channel, periodUs);

	uint32_t maxValue = (1UL << _analogWriteResolution) - 1;
	if (value < 0)
		value = 0;
	if ((uint32_t)value > maxValue)
		value = maxValue;
	uint32_t steps = hal_adv_tim_get_load_value(base) + 2;
	uint32_t comp  = (uint32_t)((uint64_t)steps * (uint32_t)value / maxValue);

	// Fully off or on: a steady level from the GPIO, the timer can't give that.
	if (comp == 0 || comp >= steps) {
		hal_gpio_pin_afio_en(gpioBase, gpioPin, HAL_DISABLE);
		if (comp == 0)
			hal_gpio_pin_reset(gpioBase, gpioPin);
		else
			hal_gpio_pin_set(gpioBase, gpioPin);
		hal_gpio_pin_direction_set(gpioBase, gpioPin, GPIO_OUTPUT);
		pwmEnableChannel(channel, HAL_DISABLE);
		return;
	}

	if (channel % 2 == 0)
		hal_adv_tim_set_comp_a(base, comp);
	else
		hal_adv_tim_set_comp_b(base, comp);
	hal_gpio_pin_direction_set(gpioBase, gpioPin, GPIO_INPUT);
	hal_gpio_pin_afio_en(gpioBase, gpioPin, HAL_ENABLE);
	pwmEnableChannel(channel, HAL_ENABLE);
}
