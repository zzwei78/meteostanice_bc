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

BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_humidity, SENSOR_DATA_STREAM_SAMPLES)
bc_data_stream_t stream_humidity;

BC_DATA_STREAM_FLOAT_BUFFER(stream_buffer_temperature, SENSOR_DATA_STREAM_SAMPLES)
bc_data_stream_t stream_temperature;

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

void sigfox_module_event_handler(bc_module_sigfox_t *self, bc_module_sigfox_event_t event, void *event_param) {
    /*
    (void) self;
    (void) event_param;

    if(event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_START) {
        bc_led_set_mode(&led, BC_LED_MODE_ON);
    }

    else if(event == BC_MODULE_SIGFOX_EVENT_SEND_RF_FRAME_DONE) {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }

    else (event == BC_MODULE_SIGFOX_EVENT_ERROR) {
        bc_led_set_mode(&led, BC_LED_MODE_BLINK);
    }
    */
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

void application_init(void) {
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    bc_usb_cdc_init();

    bc_module_battery_init();

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

    bc_scheduler_plan_from_now(APPLICATION_TASK_ID, FIRST_REPORT);

    bc_led_pulse(&led, 2000);
}

void application_task(void *param) {
    (void) param;

    int battery_charge_level = 0;

    float average_humidity;
    float average_temperature;

    char buffer[100];

    if(bc_data_stream_get_average(&stream_humidity, &average_humidity)) {
        sprintf(buffer, "Average humidity is: %f\r\n", average_humidity);
        bc_usb_cdc_write(buffer, strlen(buffer));
    }
    
    if(bc_data_stream_get_average(&stream_temperature, &average_temperature)) {
        sprintf(buffer, "Average temperature is: %f\r\n", average_temperature);
        bc_usb_cdc_write(buffer, strlen(buffer));
    }

    bc_scheduler_plan_current_relative(TESTING_REPORT_INTERVAL);
}
