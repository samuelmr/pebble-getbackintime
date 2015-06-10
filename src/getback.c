#include <pebble.h>

static Window *window;
static TextLayer *dist_layer;
// static TextLayer *unit_layer;
static TextLayer *hint_layer;
static TextLayer *track_layer;
static TextLayer *track_label_layer;
static TextLayer *speed_layer;
static TextLayer *speed_label_layer;
static TextLayer *acc_layer;
static TextLayer *acc_label_layer;
static Layer *head_layer;
static Layer *info_layer;
static char *default_hint_text = "Long press UP to set target.";
/*
static TextLayer *n_layer;
static TextLayer *e_layer;
static TextLayer *s_layer;
static TextLayer *w_layer;
static TextLayer *calib_layer;
*/
int32_t distance = 0;
int16_t heading = 0;
int16_t pheading = 0;
int16_t speed = 0;
int16_t accuracy = 0;
int16_t orientation = 0;
// update screen only when heading changes at least <sensitivity> degrees
// this is overridden by settings
int sensitivity = 1;
static const uint32_t CMD_KEY = 0x1;
static const uint32_t HEAD_KEY = 0x2;
static const uint32_t DIST_KEY = 0x3;
static const uint32_t UNITS_KEY = 0x4;
static const uint32_t SENS_KEY = 0x5;
static const uint32_t ID_KEY = 0x6;
static const uint32_t SPEED_KEY = 0x7;
static const uint32_t ACCURACY_KEY = 0x8;
static const uint32_t PHONEHEAD_KEY = 0x9;
static const char *set_cmd = "set";
static const char *quit_cmd = "quit";
static GPath *head_path;
static GRect hint_layer_size;
static const double YARD_LENGTH = 0.9144;
static const double YARDS_IN_MILE = 1760;
GPoint center;

GColor approaching;
GColor receding;
GColor bg;

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
void compass_heading_handler(CompassHeadingData heading_data){
  switch (heading_data.compass_status) {
    case CompassStatusDataInvalid:
      text_layer_set_text(hint_layer, "Calibrating compass...");
      // snprintf(valid_buf, sizeof(valid_buf), "%s", "C");
      return;
    case CompassStatusCalibrating:
      text_layer_set_text(hint_layer, "Fine tuning compass...");
      // snprintf(valid_buf, sizeof(valid_buf), "%s", "F");
      break;
    case CompassStatusCalibrated:
      break;
      text_layer_set_text(hint_layer, default_hint_text);
      // snprintf(valid_buf, sizeof(valid_buf), "%s", "");
  }
  static char compassheading[2];
  orientation = heading_data.true_heading;
  snprintf(compassheading, sizeof(compassheading), "%d", orientation);
  text_layer_set_text(track_layer, compassheading);
  layer_mark_dirty(head_layer);
  layer_mark_dirty(info_layer);
/*
  text_layer_set_text(calib_layer, valid_buf);
  int32_t nx = center.x + 63 * sin_lookup(orientation)/TRIG_MAX_RATIO;
  int32_t ny = center.y - 63 * cos_lookup(orientation)/TRIG_MAX_RATIO;
  layer_set_frame((Layer *) n_layer, GRect(nx - 9, ny - 9, 18, 18));
  int32_t ex = center.x + 63 * sin_lookup(orientation + TRIG_MAX_ANGLE/4)/TRIG_MAX_RATIO;
  int32_t ey = center.y - 63 * cos_lookup(orientation + TRIG_MAX_ANGLE/4)/TRIG_MAX_RATIO;
  layer_set_frame((Layer *) e_layer, GRect(ex - 9, ey - 9, 18, 18));
  int32_t sx = center.x + 63 * sin_lookup(orientation + TRIG_MAX_ANGLE/2)/TRIG_MAX_RATIO;
  int32_t sy = center.y - 63 * cos_lookup(orientation + TRIG_MAX_ANGLE/2)/TRIG_MAX_RATIO;
  layer_set_frame((Layer *) s_layer, GRect(sx - 9, sy - 9, 18, 18));
  int32_t wx = center.x + 63 * sin_lookup(orientation + TRIG_MAX_ANGLE*3/4)/TRIG_MAX_RATIO;
  int32_t wy = center.y - 63 * cos_lookup(orientation + TRIG_MAX_ANGLE*3/4)/TRIG_MAX_RATIO;
  layer_set_frame((Layer *) w_layer, GRect(wx - 9, wy - 9, 18, 18));
*/
}

static void info_layer_update_callback(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  // graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  // GRect bearing_box = GRect(1, 1, 42, 30);
  // graphics_draw_rect(ctx, bearing_box);
  APP_LOG(APP_LOG_LEVEL_WARNING, "Heading: %d, orientation: %d", heading, orientation);
  int bearing_diff = abs((int) (TRIGANGLE_TO_DEG(orientation) - heading));
  if (bearing_diff > 180) {
    bearing_diff = 180 - (bearing_diff % 180);
  }
  int bearing_size = (int) bearing_diff / 6; // 180 degrees = 30 px (full height)
  GRect bearing_ind = GRect(42, bearing_size, 6, 30-bearing_size);
  graphics_context_set_fill_color(ctx, get_bar_color(30-bearing_size));
  graphics_fill_rect(ctx, bearing_ind, 0, GCornerNone);

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
  gpath_rotate_to(head_path, (TRIG_MAX_ANGLE / 360) * (heading + TRIGANGLE_TO_DEG(orientation)));
  graphics_context_set_fill_color(ctx, GColorBlack);
//   graphics_fill_circle(ctx, center, 77);
//   graphics_context_set_fill_color(ctx, bg);
  gpath_draw_filled(ctx, head_path);
//  graphics_fill_circle(ctx, center, 49);
//  graphics_context_set_fill_color(ctx, GColorBlack);
}

/*
static void show_hint(void) {
  if (hint_layer) {
    layer_set_hidden(text_layer_get_layer(hint_layer), false);
  }
}

static void hide_hint(void) {
  if (hint_layer) {
    layer_set_hidden(text_layer_get_layer(hint_layer), true);
  }
}
*/

static void send_message(const char *cmd, int32_t id) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter == NULL) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Can not send command %s to phone!", cmd);
    text_layer_set_text(hint_layer, "Phone connection failed!");
    return;
  }
  dict_write_cstring(iter, CMD_KEY, cmd);
  dict_write_int32(iter, ID_KEY, id);
  const uint32_t size = dict_write_end(iter);
  app_message_outbox_send();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Sent command '%s' id '%ld' to phone! (%d bytes)", cmd, id, (int) size);
}

static void reset_handler(ClickRecognizerRef recognizer, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Reset");
  // hide_hint();
  text_layer_set_text(hint_layer, "Resetting target...");
  text_layer_set_text(dist_layer, "0");
  text_layer_set_text(track_layer, "0");
  send_message(set_cmd, -1); // current location
}

static void hint_handler(ClickRecognizerRef recognizer, void *context) {
  text_layer_set_text(hint_layer, default_hint_text);
/*
  if (layer_get_hidden(text_layer_get_layer(hint_layer))) {
    show_hint();
  }
  else {
    hide_hint();
  }  
*/
}  

void out_sent_handler(DictionaryIterator *sent, void *context) {
   // outgoing message was delivered
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message accepted by phone!");
/*
  Layer *window_layer = window_get_root_layer(window);
  hint_layer = text_layer_create(hint_layer_size);
  text_layer_set_font(hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(hint_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(hint_layer));
*/
  text_layer_set_text(hint_layer, "Target set.");
  vibes_short_pulse();
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
   // outgoing message failed
  APP_LOG(APP_LOG_LEVEL_WARNING, "Message rejected by phone: %d", reason);
  if (reason == APP_MSG_SEND_TIMEOUT) {
/*
    Layer *window_layer = window_get_root_layer(window);
    hint_layer = text_layer_create(hint_layer_size);
    text_layer_set_font(hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(hint_layer, GTextAlignmentCenter);
    layer_add_child(window_layer, text_layer_get_layer(hint_layer));
*/
    text_layer_set_text(hint_layer, "Phone connection timeout!");
  }
  else {
    text_layer_set_text(hint_layer, "Phone connection failed!");
  }
}

void in_received_handler(DictionaryIterator *iter, void *context) {
  // incoming message received
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Got message from phone!");
  Tuple *id_tuple = dict_find(iter, ID_KEY);
  if (id_tuple) {
    // what's the best way to find out if launch_get_args is supported?
    #ifdef PBL_PLATFORM_BASALT
      if(launch_reason() == APP_LAUNCH_TIMELINE_ACTION) {
        uint32_t id = launch_get_args();
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Launched from timeline, pin ID: %lu", id);
        send_message(set_cmd, (int32_t) id);
        text_layer_set_text(hint_layer, "Getting target information...");
        // hide_hint();
      }
    #endif
  }
  static char units[9];
  static char *unit = "--";
  Tuple *head_tuple = dict_find(iter, HEAD_KEY);
  if (head_tuple) {
    heading = head_tuple->value->int16;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated heading to %ld", heading);
    layer_mark_dirty(head_layer);
  }
  Tuple *phead_tuple = dict_find(iter, PHONEHEAD_KEY);
  if (phead_tuple) {
    pheading = head_tuple->value->int16;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Updated phone heading to %ld", pheading);
    layer_mark_dirty(head_layer);
  }
  Tuple *units_tuple = dict_find(iter, UNITS_KEY);
  if (units_tuple) {
    strcpy(units, units_tuple->value->cstring);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Units: %s", units);
  }
  Tuple *dist_tuple = dict_find(iter, DIST_KEY);
  if (dist_tuple) {
    // hide_hint();
    if (dist_tuple->value->int32 > distance) {
      text_layer_set_background_color(dist_layer, receding);
    }
    else if (dist_tuple->value->int32 < distance) {
      text_layer_set_background_color(dist_layer, approaching);
    }
    else {
      text_layer_set_background_color(dist_layer, GColorClear);
    }
    distance = dist_tuple->value->int32;
  }
  if (strcmp(units, "imperial") == 0) {
    // text_layer_set_text(unit_layer, "yd");
    unit = "yd";
    distance = distance / YARD_LENGTH;
  }
  else {
    // text_layer_set_text(unit_layer, "m");
    unit = "m";
  }
  if (distance > 2900) {
    if (strcmp(units, "imperial") == 0) {
      distance = (int) (distance / YARDS_IN_MILE);
      // text_layer_set_text(unit_layer, "mi");
      unit = "mi";
    }
    else {
      distance = (int) (distance / 1000);
      // text_layer_set_text(unit_layer, "km");
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
  text_layer_set_text(hint_layer, default_hint_text);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Distance updated: %d %s", (int) distance, text_layer_get_text(unit_layer));
}

void in_dropped_handler(AppMessageResult reason, void *context) {
  // incoming message dropped
  APP_LOG(APP_LOG_LEVEL_WARNING, "Could not handle message from phone: %d", reason);
}
 
static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, hint_handler);
  window_single_click_subscribe(BUTTON_ID_UP, hint_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, hint_handler);
  // window_long_click_subscribe(BUTTON_ID_SELECT, 0, reset_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 0, reset_handler, NULL);
  // window_long_click_subscribe(BUTTON_ID_DOWN, 0, reset_handler, NULL);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  center = grect_center_point(&bounds);

  info_layer = layer_create(GRect(0, 0, bounds.size.w, 32));
  layer_set_update_proc(info_layer, info_layer_update_callback);
  layer_add_child(window_layer, info_layer);
  // layer_mark_dirty(head_layer);

  head_layer = layer_create(bounds);
  layer_set_update_proc(head_layer, head_layer_update_callback);
  head_path = gpath_create(&HEAD_PATH_POINTS);
  GPoint needle_axis = {bounds.size.w/2, 74};
  gpath_move_to(head_path, needle_axis);
  layer_add_child(window_layer, head_layer);

/*
  n_layer = text_layer_create(GRect(center.x-9, center.y-71, 18, 18));
  text_layer_set_background_color(n_layer, GColorClear);
  text_layer_set_text_color(n_layer, GColorWhite);
  text_layer_set_font(n_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(n_layer, GTextAlignmentCenter);
  text_layer_set_text(n_layer, "N");
  layer_add_child(head_layer, (Layer *) n_layer);

  e_layer = text_layer_create(GRect(center.x+53, center.y-10, 18, 18));
  text_layer_set_background_color(e_layer, GColorClear);
  text_layer_set_text_color(e_layer, GColorWhite);
  text_layer_set_font(e_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(e_layer, GTextAlignmentCenter);
  text_layer_set_text(e_layer, "E");
  layer_add_child(head_layer, (Layer *) e_layer);

  s_layer = text_layer_create(GRect(center.x-9, center.y+57, 18, 18));
  text_layer_set_background_color(s_layer, GColorClear);
  text_layer_set_text_color(s_layer, GColorWhite);
  text_layer_set_font(s_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_layer, GTextAlignmentCenter);
  text_layer_set_text(s_layer, "S");
  layer_add_child(head_layer, (Layer *) s_layer);

  w_layer = text_layer_create(GRect(center.x-71, center.y-9, 18, 18));
  text_layer_set_background_color(w_layer, GColorClear);
  text_layer_set_text_color(w_layer, GColorWhite);
  text_layer_set_font(w_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(w_layer, GTextAlignmentCenter);
  text_layer_set_text(w_layer, "W");
  layer_add_child(head_layer, text_layer_get_layer(w_layer));
*/
  
  dist_layer = text_layer_create(GRect(0, bounds.size.h-49, bounds.size.w, 35));
  text_layer_set_background_color(dist_layer, GColorClear);
  text_layer_set_font(dist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(dist_layer, GColorBlack);
  text_layer_set_background_color(dist_layer, GColorClear);
  text_layer_set_text_alignment(dist_layer, GTextAlignmentCenter);
  text_layer_set_text(dist_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(dist_layer));
  
/*
  unit_layer = text_layer_create(GRect(50, 84, 44, 30));
  text_layer_set_background_color(unit_layer, GColorClear);
  text_layer_set_font(unit_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(unit_layer, GTextAlignmentCenter);
  text_layer_set_text(unit_layer, "");
  layer_add_child(window_layer, text_layer_get_layer(unit_layer));

  calib_layer = text_layer_create(GRect(129, 129, 14, 14));
  text_layer_set_background_color(calib_layer, GColorClear);
  text_layer_set_font(calib_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(calib_layer, GTextAlignmentCenter);
  text_layer_set_text(calib_layer, "C");
  layer_add_child(window_layer, text_layer_get_layer(calib_layer));
*/
  
  hint_layer_size = GRect(0, bounds.size.h-15, bounds.size.w, 15);
  hint_layer = text_layer_create(hint_layer_size);
  text_layer_set_font(hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(hint_layer, GColorWhite);
  text_layer_set_background_color(hint_layer, GColorBlack);
  text_layer_set_text_alignment(hint_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(hint_layer));
  text_layer_set_text(hint_layer, default_hint_text);

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
}

static void window_unload(Window *window) {
  layer_destroy(head_layer);
  // text_layer_destroy(unit_layer);
  text_layer_destroy(dist_layer);
  text_layer_destroy(track_layer);
  text_layer_destroy(track_label_layer);
  text_layer_destroy(speed_layer);
  text_layer_destroy(speed_label_layer);
  text_layer_destroy(acc_layer);
  text_layer_destroy(acc_label_layer);
/*
  text_layer_destroy(calib_layer);
  text_layer_destroy(w_layer);
  text_layer_destroy(s_layer);
  text_layer_destroy(e_layer);
  text_layer_destroy(n_layer);
*/
  if (hint_layer) {
    text_layer_destroy(hint_layer);
  }
}

static void init(void) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Launch reason: %d", launch_reason());
  #ifdef PBL_COLOR
    // approaching = GColorMalachite;
    // receding = GColorFolly;
    approaching = GColorMintGreen;
    receding = GColorSunsetOrange;
  #else
    approaching = GColorWhite;
    receding = GColorWhite;
  #endif
  // bg = GColorWhite;
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

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
