#include <pebble.h>

typedef struct
{
    int temperature;
    char conditions[16];
} WeatherData;

static uint32_t WEATHER_DATA_KEY = 56;

static WeatherData weather_data;

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_temperature_layer;
static TextLayer *s_conditions_layer;
static TextLayer *s_firmware_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_bluetooth_layer;

static int s_battery_level;

static void main_window_load(Window *window);
static void main_window_unload(Window *window);

static void tick_handler(struct tm *tick_time, TimeUnits units_changed);

static void inbox_received_callback(DictionaryIterator *iterator, void *context);
static void inbox_dropped_callback(AppMessageResult reason, void *context);
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context);
static void outbox_sent_callback(DictionaryIterator *iterator, void *context);

static void update_time();
static void update_date();
static void update_weather();
static void battery_callback(BatteryChargeState state);
static void bluetooth_callback(bool connected);

static void save_weather();
static void load_weather();

static void main_window_load(Window *window)
{
    // Get information about the Window
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // get watch info
    WatchInfoVersion watch_version = watch_info_get_firmware_version();

    // create the base text
    s_firmware_layer = text_layer_create(GRect(0, 0, bounds.size.w, 23));
    text_layer_set_background_color(s_firmware_layer, GColorClear);
    text_layer_set_text_color(s_firmware_layer, GColorWhite);
    text_layer_set_font(s_firmware_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    static char s_firmware_string[8];
    snprintf(s_firmware_string, sizeof(s_firmware_string), "v%u.%u", watch_version.major, watch_version.minor);
    text_layer_set_text(s_firmware_layer, s_firmware_string);
    layer_add_child(window_layer, text_layer_get_layer(s_firmware_layer));

    // Create the TextLayer with specific bounds
    s_time_layer = text_layer_create(GRect(0, 64, bounds.size.w, 50));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

    // Create temperature Layer
    s_temperature_layer = text_layer_create(GRect(0, 22, bounds.size.w, 23));
    text_layer_set_background_color(s_temperature_layer, GColorClear);
    text_layer_set_text_color(s_temperature_layer, GColorWhite);
    text_layer_set_font(s_temperature_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text(s_temperature_layer, "...");
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_temperature_layer));

    // create the conditions layer
    s_conditions_layer = text_layer_create(GRect(0, 22, bounds.size.w, 23));
    text_layer_set_background_color(s_conditions_layer, GColorClear);
    text_layer_set_text_color(s_conditions_layer, GColorWhite);
    text_layer_set_font(s_conditions_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text(s_conditions_layer, "...");
    text_layer_set_text_alignment(s_conditions_layer, GTextAlignmentRight);
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_conditions_layer));

    // battery
    s_battery_layer = text_layer_create(GRect(0, 0, bounds.size.w, 23));
    text_layer_set_background_color(s_battery_layer, GColorClear);
    text_layer_set_text_color(s_battery_layer, GColorWhite);
    text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_battery_layer, GTextAlignmentRight);
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_battery_layer));

    // date layer
    s_date_layer = text_layer_create(GRect(0, 120, bounds.size.w, 23));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_date_layer));

    // bluetooth connection text
    s_bluetooth_layer = text_layer_create(GRect(0, 0, bounds.size.w, 23));
    text_layer_set_background_color(s_bluetooth_layer, GColorClear);
    text_layer_set_text_color(s_bluetooth_layer, GColorWhite);
    text_layer_set_font(s_bluetooth_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_bluetooth_layer, GTextAlignmentCenter);
    layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_bluetooth_layer));
}

static void main_window_unload(Window *window)
{
    // Destroy TextLayer
    text_layer_destroy(s_firmware_layer);
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_temperature_layer);
    text_layer_destroy(s_conditions_layer);
    text_layer_destroy(s_battery_layer);
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_bluetooth_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed)
{
    if ((units_changed & MINUTE_UNIT) != 0)
    {
        // update the time every minute
        update_time();
    }
    if ((units_changed & HOUR_UNIT) != 0)
    {
        // update weather every hour
        update_weather();
    }
    if ((units_changed & DAY_UNIT) != 0)
    {
        // update the date
        update_date();
    }
}

static void update_date()
{
    // Get a tm structure
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    static char s_buffer[16];
    strftime(s_buffer, sizeof(s_buffer), "%a %d %b", tick_time);

    // Display this date on the TextLayer
    text_layer_set_text(s_date_layer, s_buffer);
}

static void update_time()
{
    // Get a tm structure
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    // Write the current hours and minutes into a buffer
    static char s_buffer[8];
    strftime(s_buffer, sizeof(s_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

    // Display this time on the TextLayer
    text_layer_set_text(s_time_layer, s_buffer);
}

static void update_weather()
{
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
}

static void save_weather()
{
    persist_write_data(WEATHER_DATA_KEY, &weather_data, sizeof(WeatherData));
}

static void load_weather()
{
    if (persist_exists(WEATHER_DATA_KEY))
    {
        persist_read_data(WEATHER_DATA_KEY, &weather_data, sizeof(WeatherData));

        static char temperature_buffer[8];
        static char conditions_buffer[16];

        // update UI
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dF", weather_data.temperature);
        snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", weather_data.conditions);

        // display text
        text_layer_set_text(s_temperature_layer, temperature_buffer);
        text_layer_set_text(s_conditions_layer, conditions_buffer);
    }
    else
    {
        update_weather();
    }
}

static void battery_callback(BatteryChargeState state)
{
    // Record the new battery level
    s_battery_level = state.charge_percent;
    static char s_battery_text[8];
    snprintf(s_battery_text, sizeof(s_battery_text), "%d%%", s_battery_level);
    text_layer_set_text(s_battery_layer, s_battery_text);
}

static void bluetooth_callback(bool connected)
{
    text_layer_set_text(s_bluetooth_layer, connected ? "==" : "=/=");
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context)
{
    // Read tuples for data
    Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_Temperature);
    Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_Conditions);

    // If all data is available, use it
    if (temp_tuple && conditions_tuple)
    {
        // Store incoming information
        static char temperature_buffer[8];
        static char conditions_buffer[16];

        // save here
        weather_data.temperature = (int)temp_tuple->value->int32;
        strcpy(weather_data.conditions, conditions_tuple->value->cstring);

        // store values in persistent storage
        save_weather();

        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dF", weather_data.temperature);
        snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", weather_data.conditions);

        // display text
        text_layer_set_text(s_temperature_layer, temperature_buffer);
        text_layer_set_text(s_conditions_layer, conditions_buffer);
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context)
{
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context)
{
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context)
{
    APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

static void init()
{
    s_main_window = window_create();

    window_set_window_handlers(s_main_window, (WindowHandlers){
                                                  .load = main_window_load,
                                                  .unload = main_window_unload});

    window_set_background_color(s_main_window, GColorBlack);

    // push the main windows with animated set to true
    window_stack_push(s_main_window, true);

    // Register with TickTimerService
    tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT | DAY_UNIT, tick_handler);

    // Make sure the time is displayed from the start
    update_time();
    update_date();

    // Register callbacks
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);

    // Open AppMessage
    const int inbox_size = 128;
    const int outbox_size = 128;
    app_message_open(inbox_size, outbox_size);

    // Register for battery level updates
    battery_state_service_subscribe(battery_callback);
    // Ensure battery level is displayed from the start
    battery_callback(battery_state_service_peek());

    // register bluetooth handler
    connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = bluetooth_callback});
    // display correct setting on start
    bluetooth_callback(connection_service_peek_pebble_app_connection());

    // set initial weather data
    weather_data.temperature = 42;
    strcpy(weather_data.conditions, "Moose");

    // load initial weather
    load_weather();
}

static void deinit()
{
    window_destroy(s_main_window);
}

int main(void)
{
    init();
    app_event_loop();
    deinit();
}
