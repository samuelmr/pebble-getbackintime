#include <pebble.h>

#define MAX_PLACE_COUNT 20
#define MAX_HINT_COUNT 5

static Window *window;
static TextLayer *dist_layer;
// static TextLayer *unit_layer;
static TextLayer *hint_layer;
static TextLayer *track_layer;
static TextLayer *target_layer;
static TextLayer *track_label_layer;
static TextLayer *speed_layer;
static TextLayer *speed_label_layer;
static TextLayer *acc_layer;
static TextLayer *acc_label_layer;
static Layer *head_layer;
static Layer *info_layer;
char *hints[MAX_HINT_COUNT];
int current_hint = 0;
int32_t distance = 0;
int16_t heading = 0;
int16_t pheading = -1;
int16_t speed = 0;
int16_t accuracy = 0;
int16_t orientation = 0;
static int16_t history_count = 0;
// update screen only when heading changes at least <sensitivity> degrees
// this is overridden by settings
int sensitivity = 1;
static const uint32_t CMD_KEY = 1;
static const uint32_t HEAD_KEY = 2;
static const uint32_t DIST_KEY = 3;
static const uint32_t UNITS_KEY = 4;
static const uint32_t SENS_KEY = 5;
static const uint32_t ID_KEY = 6;
static const uint32_t SPEED_KEY = 7;
static const uint32_t ACCURACY_KEY = 8;
static const uint32_t PHONEHEAD_KEY = 9;
static const uint32_t COUNT_KEY = 10;
static const uint32_t INDEX_KEY = 11;
static const uint32_t TITLE_KEY = 12;
static const uint32_t SUBTITLE_KEY = 13;
static const char *set_cmd = "set";
static const char *quit_cmd = "quit";
static GPath *head_path;
static GPoint needle_axis;
static GRect hint_layer_size;
static const double YARD_LENGTH = 0.9144;
static const double YARDS_IN_MILE = 1760;
AppTimer *current_timer;
static MenuLayer *menu_layer;
ClickConfigProvider previous_ccp;

GColor approaching;
GColor receding;
GColor bg;
int max_radius;

static void click_config_provider(void *context);

typedef struct{
  uint32_t id;
  char title[30];
  char subtitle[30];
} Place;

Place places[MAX_PLACE_COUNT];

const GPathInfo HEAD_PATH_POINTS = {
  7,
  (GPoint []) {
    {0, -40},
    {30, 10},
    {10,  10},
    {10,  40},
    {-10,  40},
    {-10,  10},
    {-30,  10},
  }
};

GColor get_bar_color(int val) {
#ifdef PBL_COLOR
  if (val < 3) {
    return(GColorRed);
  }
  else if (val < 6) {
    return(GColorOrange);
  }
  else if (val < 9) {
    return(GColorChromeYellow);
  }
  else if (val < 12) {
    return(GColorYellow);
  }
  else if (val < 15) {
    return(GColorIcterine);
  }
  else if (val < 18) {
    return(GColorPastelYellow);
  }
  else if (val < 21) {
    return(GColorSpringBud);
  }
  else if (val < 24) {
    return(GColorBrightGreen);
  }
  else if (val < 27) {
    return(GColorGreen);
  }
  return(GColorIslamicGreen);
#else
  return(GColorWhite);
#endif
}

static void reset_dist_bg(void *data) {
  text_layer_set_background_color(dist_layer, GColorClear);
}

static void show_hint(int index) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Showing hint: %d", index);  
  if (hints[index]) {
    text_layer_set_text(hint_layer, hints[index]);
    current_hint = index;
  }
}

static void prev_hint_handler(ClickRecognizerRef recognizer, void *context) {
  current_hint--;
  if (current_hint < 0) {
    current_hint = MAX_HINT_COUNT - 1;
  }
  if (!hints[current_hint]) {
    prev_hint_handler(recognizer, context);
  }
  else {
    show_hint(current_hint);
  }
}

static void next_hint_handler(ClickRecognizerRef recognizer, void *context) {
  ++current_hint;
  if (current_hint >= MAX_HINT_COUNT) {
    current_hint = 0;
  }
  if (!hints[current_hint]) {
    next_hint_handler(recognizer, context);
  }
  else {
    show_hint(current_hint);
  }
}

void compass_heading_handler(CompassHeadingData heading_data){
  switch (heading_data.compass_status) {
    case CompassStatusDataInvalid:
      hints[2] = "Calibrating compass...";
      // snprintf(valid_buf, sizeof(valid_buf), "%s", "C");
      break;
    case CompassStatusCalibrating:
      hints[2] = "Fine tuning compass...";
      // snprintf(valid_buf, sizeof(valid_buf), "%s", "F");
      break;
    case CompassStatusCalibrated:
      hints[2] = "Compass calibrated";
      if (strcmp(text_layer_get_text(hint_layer), "Fine tuning compass...") == 0) {
        text_layer_set_text(hint_layer, hints[0]);
      }
      // orientation = heading_data.true_heading;
      orientation = (360 - TRIGANGLE_TO_DEG(heading_data.magnetic_heading))%360;
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Magnetic heading: %d°", orientation);
      layer_mark_dirty(head_layer);
      layer_mark_dirty(info_layer);
      return;
  }
  show_hint(2);
  if (pheading >= 0) {
    orientation = pheading%360;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Orientation from phone heading: %d°", orientation);
  }
  layer_mark_dirty(head_layer);
  layer_mark_dirty(info_layer);
}

static void info_layer_update_callback(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  int goingto = orientation;
  
  static char bearing_text[6] = "---";
  if (pheading > 0) {
    goingto = pheading%360;
    snprintf(bearing_text, sizeof(bearing_text), "%d°", goingto);
  }
  int bearing_diff = abs((int) (goingto - heading));
  if (bearing_diff > 180) {
    bearing_diff = 180 - (bearing_diff % 180);
  }
  int bearing_size = (int) bearing_diff / 6; // 180 degrees = 30 px (full height)
  GRect bearing_ind = GRect(42, bearing_size, 6, 30-bearing_size);
  graphics_context_set_fill_color(ctx, get_bar_color(30-bearing_size));
  graphics_fill_rect(ctx, bearing_ind, 0, GCornerNone);
  text_layer_set_text(track_layer, bearing_text);

  int speed_size = (int) speed * 2;
  if (speed_size > 30) {
    speed_size = 30; // max speed about 54 km/h
  }
  GRect speed_ind = GRect(90, 30-speed_size, 6, speed_size);
  graphics_context_set_fill_color(ctx, get_bar_color(speed_size));
  graphics_fill_rect(ctx, speed_ind, 0, GCornerNone);

  int acc_size = (int) accuracy / 4;
  if (acc_size > 30) {
    acc_size = 30; // accuracy bar range from 0 to 120 m
  }
  GRect acc_ind = GRect(138, acc_size, 6, 30-acc_size);
  graphics_context_set_fill_color(ctx, get_bar_color(30-acc_size));
  graphics_fill_rect(ctx, acc_ind, 0, GCornerNone);
}

static void head_layer_update_callback(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  if (distance <= accuracy) {
    layer_set_hidden(text_layer_get_layer(target_layer), true);
    int radius = distance * 3;
    if (radius > max_radius) {
      radius = max_radius;
    }
    if (radius < 5) {
      radius = 5;
    }
    graphics_fill_circle(ctx, needle_axis, radius);
  }
  else {
    layer_set_hidden(text_layer_get_layer(target_layer), false);
    gpath_rotate_to(head_path, (TRIG_MAX_ANGLE / 360) * (heading - orientation));
    gpath_draw_filled(ctx, head_path);
  }
}

static void send_message(const char *cmd, int32_t id) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter == NULL) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Can not send command %s to phone!", cmd);
    hints[4] = "Phone connection failed!";
    show_hint(4);
    return;
  }
  else {
    hints[4] = NULL;
  }
  dict_write_cstring(iter, CMD_KEY, cmd);
  dict_write_int32(iter, ID_KEY, id);
  dict_write_end(iter);
  app_message_outbox_send();
}

static void reset_handler(ClickRecognizerRef recognizer, void *context) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Reset");
  hints[3] = "Resetting target...";
  show_hint(3);
  text_layer_set_text(dist_layer, "0");
  text_layer_set_text(track_layer, "0°");
  send_message(set_cmd, -1); // current location
}

void hide_menu() {
  layer_set_hidden(menu_layer_get_layer(menu_layer), true);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Hide menu, %d items!", history_count);
  window_set_click_config_provider(window, click_config_provider);
}

void back_button_handler(ClickRecognizerRef recognizer, void *context) {
  if (layer_get_hidden(menu_layer_get_layer(menu_layer)) == false) {
    hide_menu();
  }
  else {
    window_stack_pop(true);
  }
}

void new_ccp(void *context) {
  previous_ccp(context);
  window_single_click_subscribe(BUTTON_ID_BACK, back_button_handler);
}

void force_back_button(Window *window, MenuLayer *menu_layer) {
  previous_ccp = window_get_click_config_provider(window);
  window_set_click_config_provider_with_context(window, new_ccp, menu_layer);
}

void show_menu() {
  menu_layer_reload_data(menu_layer);
  layer_mark_dirty(menu_layer_get_layer(menu_layer)); // unnecessary?
  layer_set_hidden(menu_layer_get_layer(menu_layer), false);
  menu_layer_set_click_config_onto_window(menu_layer, window);
  force_back_button(window, menu_layer);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Show menu, %d items!", history_count);
}

static void menu_show_handler(ClickRecognizerRef recognizer, void *context) {
  if (layer_get_hidden(menu_layer_get_layer(menu_layer)) == true) {
    show_menu();
  }
  else {
    hide_menu();
  }
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Message accepted by phone!");
  hints[3] = "Target set";
  hints[4] = NULL;
  show_hint(3);
  vibes_short_pulse();
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Message rejected by phone: %d", reason);
  if (reason == APP_MSG_SEND_TIMEOUT) {
    hints[4] = "Phone connection timeout!";
  }
  else {
    hints[4] = "Phone connection failed!";
  }
  show_hint(4);
}

void in_received_handler(DictionaryIterator *iter, void *context) {
  hints[4] = NULL;
  Tuple *count_tuple = dict_find(iter, COUNT_KEY);
  if (count_tuple) {
    history_count = count_tuple->value->int8;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Found %d places in history", history_count);
    if (history_count > MAX_PLACE_COUNT) {
      history_count = MAX_PLACE_COUNT;
    }
    Tuple *index_tuple = dict_find(iter, INDEX_KEY);
    int place_index = index_tuple->value->int8;
    Place *place = &places[place_index];
    Tuple *id_tuple = dict_find(iter, ID_KEY);
    place->id = id_tuple->value->uint32;
    Tuple *title_tuple = dict_find(iter, TITLE_KEY);
    strcpy(place->title, title_tuple->value->cstring);
    Tuple *subtitle_tuple = dict_find(iter, SUBTITLE_KEY);
    strcpy(place->subtitle, subtitle_tuple->value->cstring);
    layer_mark_dirty(menu_layer_get_layer(menu_layer));
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Found places %lu (%d): %s/%s", place->id, place_index, place->title, place->subtitle);
  }

  Tuple *id_tuple = dict_find(iter, ID_KEY);
  if (id_tuple) {
    // what's the best way to find out if launch_get_args is supported?
#ifdef PBL_PLATFORM_BASALT
    int32_t idval = id_tuple->value->int32;
    if ((idval < 0) && (launch_reason() == APP_LAUNCH_TIMELINE_ACTION)) {
      uint32_t id = launch_get_args();
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Launched from timeline, pin ID: %lu", id);
      send_message(set_cmd, (int32_t) id);
      hints[3] = "Getting target information...";
      show_hint(3);
    }
#endif
  }
  static char units[9];
  static char *unit = "--";

  Tuple *head_tuple = dict_find(iter, HEAD_KEY);
  if (head_tuple) {
    hints[3] = "Target set";
    heading = head_tuple->value->int16;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated heading to %ld", heading);
    static char target_text[6];
    snprintf(target_text, sizeof(target_text), "%d°", heading);
    text_layer_set_text(target_layer, target_text);
    layer_mark_dirty(head_layer);
  }
  Tuple *phead_tuple = dict_find(iter, PHONEHEAD_KEY);
  if (phead_tuple) {
    pheading = phead_tuple->value->int16;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated phone heading to %ld", pheading);
    layer_mark_dirty(head_layer);
    layer_mark_dirty(info_layer);
  }
  Tuple *acc_tuple = dict_find(iter, ACCURACY_KEY);
  if (acc_tuple) {
    accuracy = acc_tuple->value->int16;
    static char acc_text[6];
    snprintf(acc_text, sizeof(acc_text), "%d m", (int) accuracy);
    text_layer_set_text(acc_layer, acc_text);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated accuracy to %ld", accuracy);
  }
  Tuple *speed_tuple = dict_find(iter, SPEED_KEY);
  if (speed_tuple) {
    speed = speed_tuple->value->int16;
    static char speed_text[6];
    snprintf(speed_text, sizeof(speed_text), "%d", (int) speed);
    text_layer_set_text(speed_layer, speed_text);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated speed to %ld", speed);
  }
  Tuple *units_tuple = dict_find(iter, UNITS_KEY);
  if (units_tuple) {
    strcpy(units, units_tuple->value->cstring);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Units: %s", units);
  }
  Tuple *dist_tuple = dict_find(iter, DIST_KEY);
  if (dist_tuple) {
    if (current_timer) {
      app_timer_cancel(current_timer);
    }
    if (dist_tuple->value->int32 > distance) {
      text_layer_set_background_color(dist_layer, receding);
      current_timer = app_timer_register(4000, reset_dist_bg, NULL);
    }
    else if (dist_tuple->value->int32 < distance) {
      text_layer_set_background_color(dist_layer, approaching);
      current_timer = app_timer_register(4000, reset_dist_bg, NULL);
    }
    else {
      reset_dist_bg(NULL);
    }
    distance = dist_tuple->value->int32;
  }
  if (strcmp(units, "imperial") == 0) {
    unit = "yd";
    distance = distance / YARD_LENGTH;
  }
  else {
    unit = "m";
  }
  if (distance > 2900) {
    if (strcmp(units, "imperial") == 0) {
      distance = (int) (distance / YARDS_IN_MILE);
      unit = "mi";
    }
    else {
      distance = (int) (distance / 1000);
      unit = "km";
    }
  }
  Tuple *sens_tuple = dict_find(iter, SENS_KEY);
  if (sens_tuple) {
    sensitivity = sens_tuple->value->int8;
    compass_service_set_heading_filter(TRIG_MAX_ANGLE*sensitivity/360);
    compass_service_unsubscribe();
    compass_service_subscribe(&compass_heading_handler);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Sensitivity: %d", sensitivity);
  }
  static char dist_text[9];
  snprintf(dist_text, sizeof(dist_text), "%d %s", (int) distance, unit);
  text_layer_set_text(dist_layer, dist_text);
  show_hint(0);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Distance updated: %d %s", (int) distance, text_layer_get_text(unit_layer));
}

void in_dropped_handler(AppMessageResult reason, void *context) {
  // incoming message dropped
  APP_LOG(APP_LOG_LEVEL_WARNING, "Could not handle message from phone: %d, %d", reason, APP_MSG_INVALID_ARGS);
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return history_count;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu header height: %d", MENU_CELL_BASIC_HEADER_HEIGHT);
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, "Place history");
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  Place *place = &places[cell_index->row];
  menu_cell_basic_draw(ctx, cell_layer, place->title, place->subtitle, NULL);
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  Place *place = &places[cell_index->row];
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Getting info for history place %lu", place->id);
  send_message(set_cmd, place->id);
  hide_menu();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_show_handler);
  window_single_click_subscribe(BUTTON_ID_UP, prev_hint_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, reset_handler, NULL);
  // window_long_click_subscribe(BUTTON_ID_UP, 0, reset_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, next_hint_handler);
  // window_long_click_subscribe(BUTTON_ID_DOWN, 0, reset_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_button_handler);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  info_layer = layer_create(GRect(0, 0, bounds.size.w, 32));
  layer_set_update_proc(info_layer, info_layer_update_callback);
  layer_add_child(window_layer, info_layer);
  // layer_mark_dirty(head_layer);

  head_layer = layer_create(bounds);
  layer_set_update_proc(head_layer, head_layer_update_callback);
  head_path = gpath_create(&HEAD_PATH_POINTS);
  needle_axis = GPoint(bounds.size.w/2, 80);
  gpath_move_to(head_path, needle_axis);
  layer_add_child(window_layer, head_layer);
  max_radius = (bounds.size.h - 81)/2;
  
  dist_layer = text_layer_create(GRect(0, bounds.size.h-49, bounds.size.w, 35));
  text_layer_set_font(dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(dist_layer, GColorBlack);
  text_layer_set_background_color(dist_layer, GColorClear);
  text_layer_set_text_alignment(dist_layer, GTextAlignmentCenter);
  text_layer_set_text(dist_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(dist_layer));
  
  target_layer = text_layer_create(GRect(0, 32, 48, 35));
  text_layer_set_font(target_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(target_layer, GColorBlack);
  text_layer_set_background_color(target_layer, GColorClear);
  text_layer_set_text_alignment(target_layer, GTextAlignmentCenter);
  text_layer_set_text(target_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(target_layer));
  
  hint_layer_size = GRect(0, bounds.size.h-15, bounds.size.w, 15);
  hint_layer = text_layer_create(hint_layer_size);
  text_layer_set_font(hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(hint_layer, GColorWhite);
  text_layer_set_background_color(hint_layer, GColorBlack);
  text_layer_set_text_alignment(hint_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(hint_layer));
  show_hint(0);

  track_label_layer = text_layer_create(GRect(0, 0, 42, 18));
  text_layer_set_font(track_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(track_label_layer, GColorWhite);
  text_layer_set_background_color(track_label_layer, GColorClear);
  text_layer_set_text_alignment(track_label_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(track_label_layer));
  text_layer_set_text(track_label_layer, "bearing");

  track_layer = text_layer_create(GRect(0, 14, 42, 14));
  text_layer_set_font(track_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_color(track_layer, GColorWhite);
  text_layer_set_background_color(track_layer, GColorClear);
  text_layer_set_text_alignment(track_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(track_layer));
  text_layer_set_text(track_layer, "~");

  speed_label_layer = text_layer_create(GRect(48, 0, 42, 18));
  text_layer_set_font(speed_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(speed_label_layer, GColorWhite);
  text_layer_set_background_color(speed_label_layer, GColorClear);
  text_layer_set_text_alignment(speed_label_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(speed_label_layer));
  text_layer_set_text(speed_label_layer, "speed");

  speed_layer = text_layer_create(GRect(48, 14, 42, 14));
  text_layer_set_font(speed_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_color(speed_layer, GColorWhite);
  text_layer_set_background_color(speed_layer, GColorClear);
  text_layer_set_text_alignment(speed_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(speed_layer));
  text_layer_set_text(speed_layer, "~");
  
  acc_label_layer = text_layer_create(GRect(96, 0, 42, 18));
  text_layer_set_font(acc_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(acc_label_layer, GColorWhite);
  text_layer_set_background_color(acc_label_layer, GColorClear);
  text_layer_set_text_alignment(acc_label_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(acc_label_layer));
  text_layer_set_text(acc_label_layer, "gps acc.");

  acc_layer = text_layer_create(GRect(96, 14, 42, 14));
  text_layer_set_font(acc_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_color(acc_layer, GColorWhite);
  text_layer_set_background_color(acc_layer, GColorClear);
  text_layer_set_text_alignment(acc_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(acc_layer));
  text_layer_set_text(acc_layer, "~");

  menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback
  });
  
  layer_set_hidden(menu_layer_get_layer(menu_layer), true);
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer));
}

static void window_unload(Window *window) {
  layer_destroy(head_layer);
  // text_layer_destroy(unit_layer);
  text_layer_destroy(dist_layer);
  text_layer_destroy(target_layer);
  text_layer_destroy(track_layer);
  text_layer_destroy(track_label_layer);
  text_layer_destroy(speed_layer);
  text_layer_destroy(speed_label_layer);
  text_layer_destroy(acc_layer);
  text_layer_destroy(acc_label_layer);
  if (hint_layer) {
    text_layer_destroy(hint_layer);
  }
  menu_layer_destroy(menu_layer);
}

static void init(void) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Launch reason: %d", launch_reason());
  hints[0] = "SELECT for history menu";
  hints[1] = "Long SELECT to set target";
  #ifdef PBL_COLOR
    approaching = GColorMintGreen;
    receding = GColorSunsetOrange;
  #else
    approaching = GColorWhite;
    receding = GColorWhite;
  #endif
  compass_service_set_heading_filter(TRIG_MAX_ANGLE*sensitivity/360);
  compass_service_subscribe(&compass_heading_handler);
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_outbox_sent(out_sent_handler);
  app_message_register_outbox_failed(out_failed_handler);
  const uint32_t inbound_size = APP_MESSAGE_INBOX_SIZE_MINIMUM;
  const uint32_t outbound_size = APP_MESSAGE_OUTBOX_SIZE_MINIMUM;
  app_message_open(inbound_size, outbound_size);
  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
#ifndef PBL_SDK_3
  window_set_fullscreen(window, true);
#endif
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);
}

static void deinit(void) {
  compass_service_unsubscribe();
  send_message(quit_cmd, -1);
  window_destroy(window);
}

int main(void) {
  init();

  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
