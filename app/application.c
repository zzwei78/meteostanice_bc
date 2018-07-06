#include <application.h>
#include <bcl.h>
#include <bc_usb_cdc.h>

#define REPORT_INTERVAL     (30 * 60 * 1000)        // Data is sent every 30 minutes to the cloud.
#define MEASURE_INTERVAL     (5 * 60 * 1000)        // Data is measured every 5 minutes.

#define TESTING_REPORT_INTERVAL  (30 * 1000)
#define TESTING_MEASURE_INTERVAL (10 * 1000)
#define FIRST_REPORT              (5 * 1000)

#define APPLICATION_TASK_ID                0
#define SENSOR_DATA_STREAM_SAMPLES         8

bc_led_t led;
bc_button_t button;
bc_tag_humidity_t humidity_tag;
bc_module_sigfox_t sigfox_module;
bc_scheduler_task_id_t send_sigfox_frame_task_id;

BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_humidity, SENSOR_DATA_STREAM_SAMPLES)
bc_data_stream_t stream_humidity;

BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_temperature, SENSOR_DATA_STREAM_SAMPLES)
bc_data_stream_t stream_temperature;

float average_humidity;
float average_temperature;

uint16_t average_humidity_raw;
uint16_t average_temperature_raw;

int battery_charge_level = 0;
int battery_state = 0;

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_led_set_mode(&led, BC_LED_MODE_TOGGLE);

        char buffer[100];
        sprintf(buffer, "Button pressed!\r\n");
        bc_usb_cdc_write(buffer, strlen(buffer));
    }
}

void battery_module_event_handler(bc_module_battery_event_t event, void *event_param) {
	(void) event_param;

	if(event == BC_MODULE_BATTERY_EVENT_LEVEL_LOW) {
		battery_state = 1;
	}

	else if(event == BC_MODULE_BATTERY_EVENT_LEVEL_CRITICAL) {
		battery_state = 2;
	}

	else if(event == BC_MODULE_BATTERY_EVENT_ERROR) {
		battery_state = 3;
	}
}

void sigfox_module_event_handler(bc_module_sigfox_t *self, bc_module_sigfox_event_t event, void *event_param) {
    (void) self;
    (void) event_param;

    if(event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_START) {
        bc_led_set_mode(&led, BC_LED_MODE_ON);
    }

    else if(event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_DONE) {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }

    else if(event == BC_MODULE_SIGFOX_EVENT_ERROR) {
        bc_led_set_mode(&led, BC_LED_MODE_BLINK);
    }
}

void humidity_tag_event_handler(bc_tag_humidity_t *self, bc_tag_humidity_event_t event, void *event_param) {
    (void) event;
    bc_led_pulse(&led, 1000);
    
    float humidity_percentage;
    float temperature_celsius;

    if(bc_tag_humidity_get_humidity_percentage(&humidity_tag, &humidity_percentage)) {
        bc_data_stream_feed(&stream_humidity, &humidity_percentage);
    }
    else {
        bc_data_stream_reset(&stream_humidity);
    }
    if(bc_tag_humidity_get_temperature_celsius(&humidity_tag, &temperature_celsius)) {
        bc_data_stream_feed(&stream_temperature, &temperature_celsius);
    }
    else {
        bc_data_stream_reset(&stream_temperature);
    }
}

void send_sigfox_frame(void *param) {
	(void) param;

    if(!bc_module_sigfox_is_ready(&sigfox_module))
    {
    	char buffer[100];

    	sprintf(buffer, "Sigfox module is not ready.\r\n");

    	bc_usb_cdc_write(buffer, strlen(buffer));

        bc_scheduler_plan_current_now();
        return;
    }

    bc_module_battery_measure();

    bc_module_battery_get_charge_level(&battery_charge_level);

   	char buffer[100];

    sprintf(buffer, "Sigfox module starts transmission!\r\n");

    bc_usb_cdc_write(buffer, strlen(buffer));

    uint8_t sigfoxBuf[7];

    sigfoxBuf[0] = average_temperature_raw >> 8;
    sigfoxBuf[1] = average_temperature_raw;
    sigfoxBuf[2] = average_humidity_raw >> 8;
    sigfoxBuf[3] = average_humidity_raw;
    sigfoxBuf[4] = battery_charge_level >> 8;
    sigfoxBuf[5] = battery_charge_level;
    sigfoxBuf[6] = battery_state;

    char printBuf[100];

    for(uint16_t i = 0; i < sizeof(sigfoxBuf); i++) {
    	sprintf(&printBuf[i * 2], "%x\r\n", sigfoxBuf[i]);
    }

	bc_usb_cdc_write(printBuf, strlen(printBuf));

	bc_module_sigfox_send_rf_frame(&sigfox_module, sigfoxBuf, sizeof(sigfoxBuf));
}

void application_init(void) {
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    bc_usb_cdc_init();

    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_module_event_handler, NULL);
    bc_module_battery_measure();

    bc_module_sigfox_init(&sigfox_module, BC_MODULE_SIGFOX_REVISION_R2);
    bc_module_sigfox_set_event_handler(&sigfox_module, sigfox_module_event_handler, NULL);

    bc_data_stream_init(&stream_humidity, 1, &stream_buffer_humidity);
    bc_data_stream_init(&stream_temperature, 1, &stream_buffer_temperature);
    
    bc_tag_humidity_init(&humidity_tag, BC_TAG_HUMIDITY_REVISION_R3, BC_I2C_I2C0, BC_TAG_HUMIDITY_I2C_ADDRESS_DEFAULT);
    bc_tag_humidity_set_update_interval(&humidity_tag, TESTING_MEASURE_INTERVAL);
    bc_tag_humidity_set_event_handler(&humidity_tag, humidity_tag_event_handler, NULL);
    bc_tag_humidity_measure(&humidity_tag);

    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN,0);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    send_sigfox_frame_task_id = bc_scheduler_register(send_sigfox_frame, NULL, BC_TICK_INFINITY);

    bc_scheduler_plan_from_now(APPLICATION_TASK_ID, FIRST_REPORT);

    bc_led_pulse(&led, 2000);
}

void application_task(void *param) {
    (void) param;

    char buffer[100];

    if(bc_data_stream_get_average(&stream_humidity, &average_humidity)) {
        sprintf(buffer, "Average humidity is: %f\r\n", average_humidity);
        bc_usb_cdc_write(buffer, strlen(buffer));

        if(average_humidity >= 100.f) {
        	average_humidity = 100.f;
        }

        average_humidity_raw = average_humidity / 100.f * 65536.f;

        sprintf(buffer, "Average raw humidity is: %d\r\n", average_humidity_raw);
        bc_usb_cdc_write(buffer, strlen(buffer));

        if(bc_data_stream_get_average(&stream_temperature, &average_temperature)) {
            sprintf(buffer, "Average temperature is: %f\r\n", average_temperature);
            bc_usb_cdc_write(buffer, strlen(buffer));

            average_temperature_raw = (average_temperature + 40.f) / 165.f * 65536.f;

            sprintf(buffer, "Average raw temperature is: %d\r\n", average_temperature_raw);
            bc_usb_cdc_write(buffer, strlen(buffer));

            bc_scheduler_plan_now(send_sigfox_frame_task_id);
    	}
    }
    bc_scheduler_plan_current_relative(TESTING_REPORT_INTERVAL);
}
