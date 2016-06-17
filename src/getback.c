#include <pebble.h>

#define MAX_PLACE_COUNT 20
#define MAX_HINT_COUNT 6

static Window *window;
static Window *menu_window;
static TextLayer *dist_layer;
static TextLayer *hint_layer;
static TextLayer *track_layer;
static TextLayer *target_layer;
static TextLayer *target2_layer;
static TextLayer *track_label_layer;
static TextLayer *speed_layer;
static TextLayer *speed_label_layer;
static TextLayer *acc_layer;
static TextLayer *acc_label_layer;
static TextLayer *calib_hint_layer;
static Layer *head_layer;
static Layer *info_layer;
static char *hints[MAX_HINT_COUNT];
static char target_text[6];
static char *target2_text = "---";
static int current_hint = 0;
static int32_t distance = 0;
static int16_t heading = 0;
static int16_t pheading = -1;
static int16_t speed = 0;
static int16_t accuracy = 0;
static int16_t orientation = 0;
static int16_t history_count = 0;
// update screen only when heading changes at least <sensitivity> degrees
// this is overridden by settings
static int sensitivity = 1;
static const bool animated = true;
static bool initialized = false;
static time_t location_updated;
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
static const int YARDS_IN_MILE = 1760;
static const int NAUTICAL_MILE = 1852;
/* if distance is greater than this, use km/m instead of m/yd */
static const int UNIT_TRESHOLD = 2999;
/* if distance is smaller than this, show one decimal precision */
static const int FRACTION_TRESHOLD = 100;
static AppTimer *current_timer;
static MenuLayer *menu_layer;

static GColor approaching;
static GColor receding;
// static GColor bg;
static int max_radius;

enum compass_states {
  CALIBRATING = 0,
  FINETUNING = 1,
  CALIBRATED = 2
};
static int compass_state = CALIBRATING;

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
  if (!initialized) {
    text_layer_set_text(hint_layer, "Loading data...");
    return;
  }
  if(!bluetooth_connection_service_peek()) {
    text_layer_set_text(hint_layer, "Phone connection lost!");
    return;
  }
  if (index == (MAX_HINT_COUNT - 1)) {
    int seconds = time(NULL) - location_updated;
    snprintf(hints[index], 28, "Updated %d seconds ago", seconds);
  }
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
      compass_state = CALIBRATING;
      hints[2] = "Calibrating compass...";
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Calibrating compass");
      layer_set_hidden(head_layer, true);
      layer_set_hidden(text_layer_get_layer(calib_hint_layer), false);
      break; // could as well return...
    case CompassStatusCalibrating:
      compass_state = FINETUNING;
      hints[2] = "Fine tuning compass...";
      show_hint(2);
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Fine tuning compass");
      layer_set_hidden(head_layer, false);
      layer_set_hidden(text_layer_get_layer(calib_hint_layer), true);
      break;
    case CompassStatusCalibrated:
      compass_state = CALIBRATED;
      hints[2] = "Compass calibrated";
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Compass calibrated");
      layer_set_hidden(head_layer, false);
      layer_set_hidden(text_layer_get_layer(calib_hint_layer), true);
      if (strcmp(text_layer_get_text(hint_layer), "Fine tuning compass...") == 0) {
        text_layer_set_text(hint_layer, hints[0]);
      }
      break;
    default:
      APP_LOG(APP_LOG_LEVEL_WARNING, "Unknown compass state %d!", heading_data.compass_status);
      return;
  }
  if (compass_state != CALIBRATING) {
    // orientation = heading_data.true_heading;
    // orientation = (360 - TRIGANGLE_TO_DEG(heading_data.true_heading))%360;
    orientation = 360 - (((int)(heading_data.magnetic_heading * 360 / TRIG_MAX_ANGLE) % 360 + 360) % 360);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Magnetic heading: %d (deg %ld), orientation %d°",(int) heading_data.true_heading, TRIGANGLE_TO_DEG(heading_data.true_heading), orientation);
    layer_mark_dirty(head_layer);
    layer_mark_dirty(info_layer);
  }

/* this might be too confusing... perhaps make it optional some day?
  if (pheading >= 0) {
    orientation = pheading%360;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Orientation from phone heading: %d° (%d)", orientation, pheading);
  }
  layer_mark_dirty(head_layer);
  layer_mark_dirty(info_layer);
*/
}

static void info_layer_update_callback(Layer *layer, GContext *ctx) {
  if(!bluetooth_connection_service_peek()) {
    text_layer_set_text(track_layer, "!");
    text_layer_set_text(speed_layer, "!");
    text_layer_set_text(acc_layer, "!");
    text_layer_set_text(dist_layer, "No phone!");
  }

#ifdef PBL_RECT
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int goingto = orientation;
  static char bearing_text[6] = "~";
  if (pheading > 0) {
    goingto = pheading%360;
    snprintf(bearing_text, sizeof(bearing_text), "%d°", goingto);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Phone heading exists, moving to %d", goingto);
  }
  else {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "No phone heading, bearing_text: %s, going to %d", bearing_text, goingto);
  }
  int bearing_diff = abs((int) (goingto - heading));
  if (bearing_diff > 180) {
    bearing_diff = 180 - (bearing_diff % 180);
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Difference between target heading (%d°) and current heading (%d°): %d°", heading, goingto, bearing_diff);
  int bearing_size = (int) bearing_diff / 6; // 180 degrees = 30 px (full height)
  GRect bearing_ind = GRect(42, bearing_size, 6, 30-bearing_size);
  graphics_context_set_fill_color(ctx, get_bar_color(30-bearing_size));
  graphics_fill_rect(ctx, bearing_ind, 0, GCornerNone);
  text_layer_set_text(track_layer, bearing_text);

  int speed_size = (int) speed * 2;
  if (speed_size > 30) {
    speed_size = 30; // max speed about 54 km/h
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Speed: %d", speed);
  GRect speed_ind = GRect(90, 30-speed_size, 6, speed_size);
  graphics_context_set_fill_color(ctx, get_bar_color(speed_size));
  graphics_fill_rect(ctx, speed_ind, 0, GCornerNone);

  int acc_size = (int) accuracy / 4;
  if (acc_size > 30) {
    acc_size = 30; // accuracy bar range from 0 to 120 m
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Accuracy: %d", accuracy);
  GRect acc_ind = GRect(138, acc_size, 6, 30-acc_size);
  graphics_context_set_fill_color(ctx, get_bar_color(30-acc_size));
  graphics_fill_rect(ctx, acc_ind, 0, GCornerNone);
#endif

}

static void head_layer_update_callback(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
#ifdef PBL_SDK_3
  graphics_context_set_stroke_width(ctx, 4);
#endif
  if (distance <= accuracy) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Distance (%ld) less than accuracy (%d) - drawing circle", distance, accuracy);
    layer_set_hidden(text_layer_get_layer(target_layer), true);
    layer_set_hidden(text_layer_get_layer(target2_layer), true);
    int radius = distance * 2;
    if (radius > max_radius) {
      radius = max_radius;
    }
    if (radius < 5) {
      radius = 5;
    }
    graphics_fill_circle(ctx, needle_axis, radius);
  }
  else if (compass_state == CALIBRATING) {
    layer_set_hidden(text_layer_get_layer(target_layer), true);
    layer_set_hidden(text_layer_get_layer(target2_layer), true);
    int radius = distance * 2;
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
    layer_set_hidden(text_layer_get_layer(target2_layer), false);
    int compass_heading = (TRIG_MAX_ANGLE / 360) * (heading - orientation);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Distance (%ld) more than accuracy (%d) - drawing arrow towards %d (%d)", distance, accuracy, (heading - orientation), compass_heading);
    gpath_rotate_to(head_path, compass_heading);
    AccelData accel = (AccelData) { .x = 0, .y = 0, .z = 0 };
    accel_service_peek(&accel);
    bool leveled = ((accel.y > -150) && (accel.y < 150));
    if ((compass_state == CALIBRATED) && leveled) {
      gpath_draw_filled(ctx, head_path);
    }
    else {
      gpath_draw_outline(ctx, head_path);
    }
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Distance: %d, orientation %d, heading %d, phone heading %d, compass heading %d", (int) distance, orientation, heading, pheading, (heading - orientation));
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
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Reset");
  hints[3] = "Resetting target...";
  show_hint(3);
  text_layer_set_text(dist_layer, "0");
  text_layer_set_text(track_layer, "~");
  send_message(set_cmd, -1); // current location
}

static void menu_show_handler(ClickRecognizerRef recognizer, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Show menu, %d items!", history_count);
  window_stack_push(menu_window, animated);
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
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Found %d places in history", history_count);
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
    if (menu_layer) {
      layer_mark_dirty(menu_layer_get_layer(menu_layer));
    }
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Found place %ld (%d): %s/%s", place->id, place_index, place->title, place->subtitle);
    hints[0] = "SELECT for history menu";
  }

  Tuple *id_tuple = dict_find(iter, ID_KEY);
  if (id_tuple) {
// what's the best way to find out if launch_get_args is supported?
#ifdef PBL_PLATFORM_BASALT
    int32_t idval = id_tuple->value->int32;
    if ((idval < 0) && (launch_reason() == APP_LAUNCH_TIMELINE_ACTION)) {
      uint32_t id = launch_get_args();
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Launched from timeline, pin ID: %ld", id);
      send_message(set_cmd, (int32_t) id);
      hints[3] = "Getting target information...";
      show_hint(3);
    }
#endif
    initialized = true;
  }
  static char units[9];
  static char *unit = "--";

  Tuple *head_tuple = dict_find(iter, HEAD_KEY);
  if (head_tuple) {
    location_updated = time(NULL);
    hints[3] = "Target set";
    heading = head_tuple->value->int16;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated heading to %d", heading);
    snprintf(target_text, sizeof(target_text), "%d°", heading);
    if (heading < 0) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Negative heading %d", heading);
      target2_text = "N";
    }
    if (heading < 22.5) {
      target2_text = "N";
    }
    else if (heading < 67.5) {
      target2_text = "NE";
    }
    else if (heading < 112.5) {
      target2_text = "E";
    }
    else if (heading < 157.5) {
      target2_text = "SE";
    }
    else if (heading < 202.5) {
      target2_text = "S";
    }
    else if (heading < 247.5) {
      target2_text = "SW";
    }
    else if (heading < 292.5) {
      target2_text = "W";
    }
    else if (heading < 337.5) {
      target2_text = "NW";
    }
    text_layer_set_text(target_layer, target_text);
    text_layer_set_text(target2_layer, target2_text);
    layer_mark_dirty(head_layer);
    layer_mark_dirty(info_layer);
  }
  Tuple *units_tuple = dict_find(iter, UNITS_KEY);
  if (units_tuple) {
    strcpy(units, units_tuple->value->cstring);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Units: %s", units);
  }
  Tuple *phead_tuple = dict_find(iter, PHONEHEAD_KEY);
  if (phead_tuple) {
    pheading = phead_tuple->value->int16;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated phone heading to %d", pheading);
  }
  Tuple *acc_tuple = dict_find(iter, ACCURACY_KEY);
  if (acc_tuple) {
    accuracy = acc_tuple->value->int16;
    int16_t show_acc = accuracy;
    // char *acc_unit = "m";
    if (strcmp(units, "imperial") == 0) {
      show_acc = (int) (accuracy / YARD_LENGTH);
      // acc_unit = "y";
    }
    static char acc_text[6];
    snprintf(acc_text, sizeof(acc_text), "%d", show_acc);
    text_layer_set_text(acc_layer, acc_text);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated accuracy to %d %s (%d)", show_acc, acc_unit, accuracy);
  }
  Tuple *speed_tuple = dict_find(iter, SPEED_KEY);
  if (speed_tuple) {
    speed = speed_tuple->value->int16;
    static char speed_text[6] = "";
    if (speed >= 0) {
      snprintf(speed_text, sizeof(speed_text), "%d", (int) speed);
    }
    text_layer_set_text(speed_layer, speed_text);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated speed to %d", speed);
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
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated distance to %d", (int) distance);
  }
  int32_t dist_times_ten = 10 * distance;
  if (strcmp(units, "imperial") == 0) {
    unit = "yd";
    dist_times_ten = (int) (dist_times_ten / YARD_LENGTH);
  }
  else if (strcmp(units, "nautical") == 0) {
    unit = "nm";
    dist_times_ten = (int) (dist_times_ten / NAUTICAL_MILE);
  }
  else {
    unit = "m";
  }
  static char dist_text[9];
  if (dist_times_ten > (10 * UNIT_TRESHOLD)) {
    if (strcmp(units, "imperial") == 0) {
      /* some precision is lost... */
      dist_times_ten = (int) (dist_times_ten / YARDS_IN_MILE);
      unit = "mi";
    }
    else if (strcmp(units, "nautical") == 0) {
      dist_times_ten = dist_times_ten;
      unit = "nm";
    }
    else {
      dist_times_ten = (int) (dist_times_ten / 1000);
      unit = "km";
    }
  }
  /* decimals of metrs would always be 0 */
  if ((strcmp(unit, "m") != 0) && (dist_times_ten < FRACTION_TRESHOLD)) {
    int whole = (int) (dist_times_ten / 10);
    int fractions = (int) (dist_times_ten % 10);
    snprintf(dist_text, sizeof(dist_text), "%d.%d %s", whole, fractions, unit);
  }
  else {
    snprintf(dist_text, sizeof(dist_text), "%d %s", (int) (dist_times_ten/10), unit);
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Unit: %s", unit);
  Tuple *sens_tuple = dict_find(iter, SENS_KEY);
  if (sens_tuple) {
    sensitivity = sens_tuple->value->int8;
    compass_service_unsubscribe();
    compass_service_set_heading_filter(TRIG_MAX_ANGLE*sensitivity/360);
    compass_service_subscribe(&compass_heading_handler);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Sensitivity: %d", sensitivity);
  }

  text_layer_set_text(dist_layer, dist_text);
  show_hint(0);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Distance updated: %s (%d)", text_layer_get_text(dist_layer), (int) distance);
}

void in_dropped_handler(AppMessageResult reason, void *context) {
  // incoming message dropped
  APP_LOG(APP_LOG_LEVEL_WARNING, "Could not handle message from phone: %d", reason);
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // Chalk doesn't do headers well, header replaced with a basic row
  return history_count + PBL_IF_RECT_ELSE(0, 1);
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // Chalk doesn't do headers well, header replaced with a basic row
  return PBL_IF_RECT_ELSE(MENU_CELL_BASIC_HEADER_HEIGHT, 0);
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, "Location history");
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  int index = cell_index->row - PBL_IF_RECT_ELSE(0, 1);
  if (index < 0) {
    // Chalk doesn't do headers well, header replaced with a basic row
    menu_cell_basic_draw(ctx, cell_layer, "Location history", NULL, NULL);
  }
  else {
    Place *place = &places[index];
    menu_cell_basic_draw(ctx, cell_layer, place->title, place->subtitle, NULL);
  }
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  text_layer_set_text(hint_layer, "Loading data...");
  int index = cell_index->row - PBL_IF_RECT_ELSE(0, 1);
  if (index < 0) {
    // Chalk doesn't do headers well, header replaced with a basic row
    MenuIndex menu_index = (MenuIndex) {0, 1};
    menu_layer_set_selected_index(menu_layer, menu_index, MenuRowAlignTop, false);
    return;
  }
  Place *place = &places[index];
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "SELECT pushed, getting info for history place %lu", place->id);
  send_message(set_cmd, place->id);
  window_stack_pop(animated);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_show_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, reset_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, prev_hint_handler);
  // window_long_click_subscribe(BUTTON_ID_UP, 0, reset_handler, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, next_hint_handler);
  // window_long_click_subscribe(BUTTON_ID_DOWN, 0, reset_handler, NULL);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  info_layer = layer_create(GRect(0, 0, bounds.size.w, 32));
  layer_set_update_proc(info_layer, info_layer_update_callback);
  layer_add_child(window_layer, info_layer);

  head_layer = layer_create(bounds);
  layer_set_update_proc(head_layer, head_layer_update_callback);
  head_path = gpath_create(&HEAD_PATH_POINTS);
  needle_axis = GPoint(bounds.size.w/2, PBL_IF_RECT_ELSE(78, bounds.size.h/2));
  gpath_move_to(head_path, needle_axis);
  layer_add_child(window_layer, head_layer);
  max_radius = (bounds.size.h - 81)/2;

  dist_layer = text_layer_create(GRect(0, bounds.size.h-PBL_IF_RECT_ELSE(49, 45), bounds.size.w, 50));
  text_layer_set_font(dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(dist_layer, GColorBlack);
  text_layer_set_background_color(dist_layer, GColorClear);
  text_layer_set_text_alignment(dist_layer, GTextAlignmentCenter);
  text_layer_set_text(dist_layer, "Initializing");
  layer_add_child(window_layer, text_layer_get_layer(dist_layer));

  target_layer = text_layer_create(GRect(PBL_IF_RECT_ELSE(0, 14), 32, 48, 35));
  text_layer_set_font(target_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(target_layer, GColorBlack);
  text_layer_set_background_color(target_layer, GColorClear);
  text_layer_set_text_alignment(target_layer, GTextAlignmentCenter);
  text_layer_set_text(target_layer, "Please");
  layer_add_child(window_layer, text_layer_get_layer(target_layer));

  target2_layer = text_layer_create(GRect(bounds.size.w-PBL_IF_RECT_ELSE(48, 62), 32, 48, 35));
  text_layer_set_font(target2_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(target2_layer, GColorBlack);
  text_layer_set_background_color(target2_layer, GColorClear);
  text_layer_set_text_alignment(target2_layer, GTextAlignmentCenter);
  text_layer_set_text(target2_layer, "wait");
  layer_add_child(window_layer, text_layer_get_layer(target2_layer));

  hint_layer_size = GRect(0, bounds.size.h-PBL_IF_RECT_ELSE(15, 0), bounds.size.w, PBL_IF_RECT_ELSE(15, 0));
  hint_layer = text_layer_create(hint_layer_size);
  text_layer_set_font(hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(hint_layer, GColorWhite);
  text_layer_set_background_color(hint_layer, GColorBlack);
  text_layer_set_text_alignment(hint_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(hint_layer));
  show_hint(1);

  track_label_layer = text_layer_create(GRect(0, 0, 42, PBL_IF_RECT_ELSE(18, 0)));
  text_layer_set_font(track_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(track_label_layer, GColorWhite);
  text_layer_set_background_color(track_label_layer, GColorClear);
  text_layer_set_text_alignment(track_label_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(track_label_layer));
  text_layer_set_text(track_label_layer, "bearing");

  track_layer = text_layer_create(GRect(0, 14, 42, PBL_IF_RECT_ELSE(14, 0)));
  text_layer_set_font(track_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_color(track_layer, GColorWhite);
  text_layer_set_background_color(track_layer, GColorClear);
  text_layer_set_text_alignment(track_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(track_layer));
  text_layer_set_text(track_layer, "~");

  speed_label_layer = text_layer_create(GRect(48, 0, 42, PBL_IF_RECT_ELSE(18, 0)));
  text_layer_set_font(speed_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(speed_label_layer, GColorWhite);
  text_layer_set_background_color(speed_label_layer, GColorClear);
  text_layer_set_text_alignment(speed_label_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(speed_label_layer));
  text_layer_set_text(speed_label_layer, "speed");

  speed_layer = text_layer_create(GRect(48, 14, 42, PBL_IF_RECT_ELSE(14, 0)));
  text_layer_set_font(speed_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_color(speed_layer, GColorWhite);
  text_layer_set_background_color(speed_layer, GColorClear);
  text_layer_set_text_alignment(speed_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(speed_layer));
  text_layer_set_text(speed_layer, "~");

  acc_label_layer = text_layer_create(GRect(96, 0, 42, PBL_IF_RECT_ELSE(18, 0)));
  text_layer_set_font(acc_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(acc_label_layer, GColorWhite);
  text_layer_set_background_color(acc_label_layer, GColorClear);
  text_layer_set_text_alignment(acc_label_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(acc_label_layer));
  text_layer_set_text(acc_label_layer, "gps acc.");

  acc_layer = text_layer_create(GRect(96, 14, 42, PBL_IF_RECT_ELSE(14, 0)));
  text_layer_set_font(acc_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_color(acc_layer, GColorWhite);
  text_layer_set_background_color(acc_layer, GColorClear);
  text_layer_set_text_alignment(acc_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(acc_layer));
  text_layer_set_text(acc_layer, "~");

  calib_hint_layer = text_layer_create(GRect(PBL_IF_RECT_ELSE(8, 0), 40, bounds.size.w-PBL_IF_RECT_ELSE(16, 0), 70));
  text_layer_set_font(calib_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(calib_hint_layer, GColorWhite);
  text_layer_set_background_color(calib_hint_layer, GColorBlack);
  text_layer_set_text_alignment(calib_hint_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(calib_hint_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(calib_hint_layer));
  text_layer_set_text(calib_hint_layer, "Calibrating compass.\nDraw 8 with your wrist.");
}

static void window_unload(Window *window) {
  layer_destroy(head_layer);
  text_layer_destroy(dist_layer);
  text_layer_destroy(target_layer);
  text_layer_destroy(target2_layer);
  text_layer_destroy(track_layer);
  text_layer_destroy(track_label_layer);
  text_layer_destroy(speed_layer);
  text_layer_destroy(speed_label_layer);
  text_layer_destroy(acc_layer);
  text_layer_destroy(acc_label_layer);
  text_layer_destroy(calib_hint_layer);
  if (hint_layer) {
    text_layer_destroy(hint_layer);
  }
}

static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback
  });
  menu_layer_set_click_config_onto_window(menu_layer, menu_window);
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer));
}

static void menu_window_unload(Window *window) {
  menu_layer_destroy(menu_layer);
}


static void init(void) {
  initialized = false;
  hints[0] = "Loading history...";
  hints[1] = "Long SELECT to set target";
  hints[MAX_HINT_COUNT - 1] = "Loading data...";
#ifdef PBL_COLOR
  approaching = GColorMintGreen;
  receding = GColorSunsetOrange;
#else
  approaching = GColorWhite;
  receding = GColorWhite;
#endif
  Place *place = &places[0];
  place->id = -1;
  strcpy(place->title, "Add new target");
  strcpy(place->subtitle, "Set current location");
  history_count = 1;

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
  window_stack_push(window, animated);

  menu_window = window_create();
  window_set_window_handlers(menu_window, (WindowHandlers) {
    .load = menu_window_load,
    .unload = menu_window_unload,
  });

}

static void deinit(void) {
  compass_service_unsubscribe();
  send_message(quit_cmd, -1);
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
