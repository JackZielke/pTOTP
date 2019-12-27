// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pebble.h"

#include "generate.h"
#include "persist_error_msg.h"

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

// #define TEST_TOKEN 1

#define P_UTCOFFSET       1
#define P_TOKENS_COUNT    2
#define P_SELECTED_LIST_INDEX    3
#define P_TOKENS_START    10000
#define P_SECRETS_START   20000

#define MAX_NAME_LENGTH   32

static Window *window;

typedef enum PersistenceWritebackFlags {
  PWNone = 0,
  PWUTCOffset = 1,
  PWTokens = 1 << 1,
  PWSecrets = 1 << 2
} PersistenceWritebackFlags;

PersistenceWritebackFlags persist_writeback = PWNone;

typedef enum AMKey {
  AMSetUTCOffset = 0, // Int32 with offset

  AMCreateToken = 1, // UInt8 array with secret
  AMCreateToken_ID = 2, // Short with ID for token (provided by phone)
  AMCreateToken_Name = 3, // Char array with name for token (provided by phone)

  AMDeleteToken = 4, // Short with token ID
  AMClearTokens = 5,

  AMReadTokenList = 6, // Starts token list read
  AMReadTokenList_Result = 7, // Struct with token info, returned in order of the list
  AMReadTokenList_Finished = 8, // Included in the last AMReadTokenList_Result message

  AMUpdateToken = 9, // Struct with token info

  AMSetTokenListOrder = 10, // array of shorts of token IDs

  AMCreateToken_Digits = 11, // Short with length of code (provided by phone)

} AMKey;

typedef struct TokenInfo {
  char name[MAX_NAME_LENGTH + 1];
  short id;
  uint8_t secret_length; // Since persistence is limited to this size anyways.
  uint8_t* secret;
  char code[12];
  short digits;
} TokenInfo;

typedef struct PublicTokenInfo {
  short id;
  char name[MAX_NAME_LENGTH + 1];
} PublicTokenInfo;

typedef struct TokenListNode {
  struct TokenListNode* next;
  TokenInfo* key;
} TokenListNode;

TokenListNode* token_list = NULL;
bool key_list_is_dirty = false;

Layer *bar_layer;

TextLayer *no_tokens_layer;

MenuLayer *code_list_layer;

int utc_offset;

int token_list_retrieve_index = 0;

int startup_selected_list_index = 0;

static void persist_do_writeback(void);

void token_list_add(TokenInfo* key) {
  TokenListNode* node = malloc(sizeof(TokenListNode));
  node->next = NULL;
  node->key = key;

  if (!token_list) {
    token_list = node;
  } else {
    TokenListNode* tail = token_list;
    while (tail->next != NULL) {
      tail = tail->next;
    }
    tail->next = node;
  }
  key_list_is_dirty = true;
}

TokenInfo* token_by_list_index(int index) {
  TokenListNode* node = token_list;
  for (int i = 0; i < index; ++i)
  {
    node = node->next;
  }
  return node->key;
}

TokenInfo* token_by_id(short id) {
  TokenListNode* node = token_list;
  while (node) {
    if (node->key->id == id) {
      return node->key;
    }
    node = node->next;
  }
  return NULL;
}

short token_list_length(void){
  short size = 0;
  TokenListNode* node = token_list;
  while (node) {
    node = node->next;
    size++;
  }
  return size;
}

void token_list_clear(void){
  TokenListNode* temp;
  while (token_list) {
    temp = token_list;
    token_list = temp->next;
    free(temp->key->secret);
    free(temp->key); // Since it'd be a pain to do this otherwise.
    free(temp);
  }
  key_list_is_dirty = true;
}

bool token_list_delete(TokenInfo* key){
  TokenListNode* node = token_list;
  TokenListNode* last = NULL;
  while (node && node->key != key) {
    last = node;
    node = node->next;
  }
  if (node) {
    if (last) {
      last->next = node->next;
    } else {
      token_list = node->next;
    }
    free(node);
    key_list_is_dirty = true;
    return true;
  }
  return false;
}

void token_list_supplant(TokenListNode* newList) {
  TokenListNode* last = NULL;
  // Free the existing list nodes (but not their TokenInfos)
  while (token_list) {
    last = token_list;
    token_list = last->next;
    free(last);
  }
  token_list = newList;
  key_list_is_dirty = true;
}

void tokeninfo2publicinfo(TokenInfo* key, PublicTokenInfo* public) {
  public->id = key->id;
  strncpy(public->name, key->name, MAX_NAME_LENGTH);
  public->name[MAX_NAME_LENGTH] = 0;
}

void publicinfo2tokeninfo(PublicTokenInfo* public, TokenInfo* key) {
  key->id = public->id;
  strncpy(key->name, public->name, MAX_NAME_LENGTH);
  key->name[MAX_NAME_LENGTH] = 0;
}

void code2char(unsigned int code, char* out, int length) {
  out[length] = 0;
  for(int x=0; x<length; x++) {
    out[length-1-x] = '0' + (code % 10);
    code /= 10;
  }
}

void code2charspace(unsigned int code, char* out, int length) {
  out[length+1] = 0;
  for(int x=0; x<=length; x++) {
    if (x == length/2) {
      out[length-x] = ' ';
      x++;
    }
    out[length-x] = '0' + (code % 10);
    code /= 10;
  }
}

void show_no_tokens_message(bool show) {
  layer_set_hidden((Layer*)code_list_layer, show);
  layer_set_hidden(bar_layer, show);
  layer_set_hidden((Layer*)no_tokens_layer, !show);
}

void refresh_all(void){
  static unsigned int lastQuantizedTimeGenerated = 0;

#ifdef PBL_SDK_3
  unsigned long utcTime = time(NULL);
#else
  unsigned long utcTime = time(NULL) - utc_offset;
#endif
  unsigned int quantized_time = utcTime/30;

  if (quantized_time == lastQuantizedTimeGenerated && !key_list_is_dirty) {
    return;
  }

  key_list_is_dirty = false;

  bool hasKeys = false;

  lastQuantizedTimeGenerated = quantized_time;

  TokenListNode* keyNode = token_list;
  while (keyNode) {
    unsigned int code = generateCode(keyNode->key->secret, keyNode->key->secret_length, quantized_time);
    if (keyNode->key->digits > 6) {
      code2charspace(code, (char*)&keyNode->key->code, keyNode->key->digits);
    } else {
      code2char(code, (char*)&keyNode->key->code, keyNode->key->digits);
    }
    keyNode = keyNode->next;
    hasKeys = true;
  }

  if (hasKeys) {
    menu_layer_reload_data(code_list_layer);
  }
  show_no_tokens_message(!hasKeys);
}

static void wrap_angle(int *angle) {
  while (*angle < 0) {
    *angle += TRIG_MAX_ANGLE;
  }
  while (*angle > TRIG_MAX_ANGLE) {
    *angle -= TRIG_MAX_ANGLE;
  }
}

void bar_layer_update(Layer *l, GContext* ctx) {
#ifdef PBL_PLATFORM_BASALT
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif

  static const unsigned short MAX_SLICE_TIME = 0xffff;
  time_t now_sec;
  uint16_t now_msec;
  time_ms(&now_sec, &now_msec);
  unsigned int slice =  (now_sec % 30) * (MAX_SLICE_TIME / 30) + ((now_msec * MAX_SLICE_TIME) / 30000);

  #ifdef PBL_PLATFORM_CHALK
  int32_t start_angle, end_angle;
  if (now_sec % 60 < 30) {
    start_angle = slice * TRIG_MAX_ANGLE / MAX_SLICE_TIME;
    end_angle = TRIG_MAX_ANGLE;
  } else {
    start_angle = 0;
    end_angle = slice * TRIG_MAX_ANGLE / MAX_SLICE_TIME;
  }

  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_radial(ctx, layer_get_bounds(l), GOvalScaleModeFitCircle, 8, end_angle == TRIG_MAX_ANGLE ? 0 : end_angle, start_angle == 0 ? TRIG_MAX_ANGLE : start_angle);
  graphics_context_set_fill_color(ctx, GColorCobaltBlue);
  graphics_fill_radial(ctx, layer_get_bounds(l), GOvalScaleModeFitCircle, 8, start_angle, end_angle);
  #else
  graphics_fill_rect(ctx, GRect(0, 0, ((MAX_SLICE_TIME-slice) * 144) / MAX_SLICE_TIME, 5), 0, GCornerNone);
  #endif
}

void draw_code_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context){
  GRect bounds = layer_get_bounds(cell_layer);
  GColor fg = GColorBlack;
  GColor active_fg = GColorWhite;
  GFont code_font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
  #ifdef PBL_COLOR
  GColor active_bg = GColorCobaltBlue;
  #else
  GColor active_bg = GColorBlack;
  #endif

  graphics_context_set_fill_color(ctx, active_bg);
  if (menu_cell_layer_is_highlighted(cell_layer)) {
    graphics_context_set_text_color(ctx, active_fg);
    graphics_fill_rect(ctx, bounds, 0, 0);
  } else {
    graphics_context_set_text_color(ctx, fg);
  }

  TokenInfo* key = token_by_list_index(cell_index->row);
  graphics_draw_text(ctx, (char*)key->name, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 36, bounds.size.w, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  if (key->digits > 8) {
    code_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  } else if (key->digits > 6) {
    code_font = fonts_get_system_font(FONT_KEY_DROID_SERIF_28_BOLD);
  }
  graphics_draw_text(ctx, (char*)key->code, code_font, GRect(0, 0, bounds.size.w, 100), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

}

uint16_t num_code_rows(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context){
  if (section_index) return 0;
  return token_list_length();
}

int16_t get_cell_height(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  #ifdef PBL_PLATFORM_CHALK
  return 180/3;
  #else
  return 55;
  #endif
}

#ifdef PBL_PLATFORM_CHALK

void draw_no_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *callback_context){}

uint16_t num_faux_sections(MenuLayer *menu_layer, void *callback_context) {
  return 2;
}

int16_t get_faux_section_height(MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
  return 180/3;
}

#endif

void token_list_retrieve_iter() {
  DictionaryIterator *iter;
  if (token_list_retrieve_index == token_list_length()) {
    if (token_list_retrieve_index == 0){
      // We have to send the AMReadTokenList_Finished message by its own, otherwise the configuration screen will block forever waiting for tokens that will never arrive.
      app_message_outbox_begin(&iter);
      dict_write_tuplet(iter, &TupletInteger(AMReadTokenList_Finished, 1));
      app_message_outbox_send();
    }
    return;
  }

  TokenInfo* key = token_by_list_index(token_list_retrieve_index);
  PublicTokenInfo* public = malloc(sizeof(PublicTokenInfo));
  app_message_outbox_begin(&iter);

  tokeninfo2publicinfo(key, public);
  Tuplet record = TupletBytes(AMReadTokenList_Result, (uint8_t*)public, sizeof(PublicTokenInfo));
  dict_write_tuplet(iter, &record);

  if (token_list_retrieve_index + 1 == token_list_length()) {
    dict_write_tuplet(iter, &TupletInteger(AMReadTokenList_Finished, 1));
  }

  app_message_outbox_send();

  token_list_retrieve_index++;
  free(public);
}

void in_received_handler(DictionaryIterator *received, void *context) {
  static bool delta = false;
  Tuple *utcoffset_tuple = dict_find(received, AMSetUTCOffset);
  if (utcoffset_tuple) {
    if (utc_offset != utcoffset_tuple->value->int32){
      delta = true;
      persist_writeback |= PWUTCOffset;
    }

    utc_offset = utcoffset_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Set TZ offset %d", utc_offset);
   }

  if (dict_find(received, AMClearTokens)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Clear tokens");
    token_list_clear();

    persist_writeback |= PWTokens;
    delta = true;
  }

  Tuple *delete_token = dict_find(received, AMDeleteToken);
  if (delete_token) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Delete token %d", delete_token->value->int8);
    TokenInfo* key = token_by_id(delete_token->value->int8);
    persist_delete(P_SECRETS_START + key->id); // Ensure the secret gets deleted.
    token_list_delete(key);
    free(key->secret);
    free(key);

    persist_writeback |= PWTokens;
    delta = true;
  }

  Tuple *update_token = dict_find(received, AMUpdateToken);
  if (update_token) {
    PublicTokenInfo* public = (PublicTokenInfo*)&update_token->value->data;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Update token %d", public->id);
    publicinfo2tokeninfo(public, token_by_id(public->id));

    persist_writeback |= PWTokens;
    delta = true;
  }

  Tuple *create_token = dict_find(received, AMCreateToken);
  if (create_token) {
    uint8_t* secret = create_token->value->data;
    TokenInfo* newKey = malloc(sizeof(TokenInfo));
  newKey->secret_length = secret[0]; // First byte is secret length
  newKey->secret = malloc(newKey->secret_length);
    memcpy(newKey->secret, secret + 1, newKey->secret_length); // While the rest is the key itself
    newKey->id = dict_find(received, AMCreateToken_ID)->value->int32;
    strncpy((char*)&newKey->name, dict_find(received, AMCreateToken_Name)->value->cstring, MAX_NAME_LENGTH);
    newKey->name[MAX_NAME_LENGTH] = 0;
    newKey->digits = dict_find(received, AMCreateToken_Digits)->value->int32;

    token_list_add(newKey);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Create token %d", newKey->id);

  persist_writeback |= PWTokens | PWSecrets;
    delta = true;
  }

  if (dict_find(received, AMReadTokenList)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Listing tokens");
    token_list_retrieve_index = 0;
    token_list_retrieve_iter();
  }

  Tuple *reorder_list = dict_find(received, AMSetTokenListOrder);
  if (reorder_list) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Reordering tokens");
    TokenListNode* newList = NULL;
    TokenListNode* node = NULL;
    TokenListNode* last = NULL;

    // Build a new list using the existing TokenInfos
    int ct = token_list_length();
    for (int i = 0; i < ct; ++i)
    {
      node = malloc(sizeof(TokenListNode));
      if (last) {
        last->next = node;
      } else {
        newList = node;
      }
      node->next = NULL;
      node->key = token_by_id(reorder_list->value->data[i]);
      last = node;
    }

    // Free the existing list and replace.
    token_list_supplant(newList);

    persist_writeback |= PWTokens;
    delta = true;
  }

  if (delta){
    refresh_all();
    persist_do_writeback();
  }
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
  if (dict_find(sent, AMReadTokenList_Result)) {
    token_list_retrieve_iter();
  }
}

// Animations on Chalk (only)
// We also drive code refreshes from here (rather than a tick handler) so they line up
void bar_animation_tick(void* unused) {
  static uint32_t current_code_gen;
  uint32_t code_gen = time(NULL) / 30;

  if (code_gen != current_code_gen) {
    refresh_all();
    current_code_gen = code_gen;
  }

  layer_mark_dirty(bar_layer);

  app_timer_register(1000/29, bar_animation_tick, NULL);
}

void handle_tick(struct tm* tick_time, TimeUnits units_changed) {
  refresh_all();
  layer_mark_dirty(bar_layer);
}

void handle_init() {

  app_message_register_inbox_received(in_received_handler);
  app_message_register_outbox_sent(out_sent_handler);

  const uint32_t inbound_size = 1024;
  const uint32_t outbound_size = 1024;
  app_message_open(inbound_size, outbound_size);

  // Load persisted data
  utc_offset = persist_exists(P_UTCOFFSET) ? persist_read_int(P_UTCOFFSET) : 0;
  if (persist_exists(P_TOKENS_COUNT)) {
    int ct = persist_read_int(P_TOKENS_COUNT);
    APP_LOG(APP_LOG_LEVEL_INFO, "Starting with %d tokens & secrets", ct);
    for (int i = 0; i < ct; ++i) {
      TokenInfo* key = malloc(sizeof(TokenInfo));
      persist_read_data(P_TOKENS_START + i, key, sizeof(TokenInfo));
    key->secret = malloc(key->secret_length);
    persist_read_data(P_SECRETS_START + key->id, key->secret, key->secret_length);
      token_list_add(key);
    }
  }
#ifdef TEST_TOKEN
  token_list_clear();
  TokenInfo* key = malloc(sizeof(TokenInfo));
  strcpy(key->name, "TEST TOKEN!");
  key->id = 0;
  void* secret = malloc(10);
  memset(secret, 65, 10);
  key->secret = secret;
  key->secret_length = 10;
  key->digits = 6;
  token_list_add(key);
  
  key = malloc(sizeof(TokenInfo));
  strcpy(key->name, "TEST TOKEN 2!");
  key->id = 1;
  secret = malloc(10);
  memset(secret, 66, 10);
  key->secret = secret;
  key->secret_length = 10;
  key->digits = 8;
  token_list_add(key);
#endif

  window = window_create();
  window_stack_push(window, true /* Animated */);

  Layer* rootLayer = window_get_root_layer(window);
  GRect rootLayerRect = layer_get_bounds(rootLayer);

#ifdef PBL_PLATFORM_CHALK
  // It's not a bar on Chalk...
  const GRect bar_layer_rect = rootLayerRect;
#else
  const GRect bar_layer_rect = GRect(0, rootLayerRect.size.h-5, rootLayerRect.size.w, 5);
#endif

  bar_layer = layer_create(bar_layer_rect);
  layer_set_update_proc(bar_layer, bar_layer_update);

#ifdef PBL_PLATFORM_CHALK
  const GRect no_tokens_rect = GRect(0, rootLayerRect.size.h/2-17, rootLayerRect.size.w, 30*2);
#else
  const GRect no_tokens_rect = GRect(0, rootLayerRect.size.h/2-35, rootLayerRect.size.w, 30*2);
#endif
  no_tokens_layer = text_layer_create(no_tokens_rect);

  text_layer_set_text(no_tokens_layer, "No Tokens");
  text_layer_set_font(no_tokens_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_color(no_tokens_layer, GColorBlack);
  text_layer_set_text_alignment(no_tokens_layer, GTextAlignmentCenter);

  #ifdef PBL_PLATFORM_CHALK
    const GRect code_list_rect = rootLayerRect;
  #else
    const GRect code_list_rect = GRect(0,0,rootLayerRect.size.w, rootLayerRect.size.h - 4);
  #endif
  code_list_layer = menu_layer_create(code_list_rect);

  MenuLayerCallbacks menuCallbacks = {
#ifdef PBL_PLATFORM_CHALK
    .get_num_sections = num_faux_sections,
    .get_header_height = get_faux_section_height,
    .draw_header = draw_no_header,
#endif
    .draw_row = draw_code_row,
    .get_num_rows = num_code_rows,
    .get_cell_height = get_cell_height
  };

  menu_layer_set_callbacks(code_list_layer, NULL, menuCallbacks);


  menu_layer_set_click_config_onto_window(code_list_layer, window);


  layer_add_child(rootLayer, (Layer*)code_list_layer);
  layer_add_child(rootLayer, bar_layer);
  layer_add_child(rootLayer, (Layer*)no_tokens_layer);

  // Start draining their batteries
  #ifdef PBL_PLATFORM_CHALK
  bar_animation_tick(NULL);
  #else
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  #endif

  // Ideally we'd set this before we register the callbacks, so we wouldn't catch the change event should it be called.
  if (persist_exists(P_SELECTED_LIST_INDEX)) {
    MenuIndex index = {.row = persist_read_int(P_SELECTED_LIST_INDEX), .section=0};
    startup_selected_list_index = index.row;
    menu_layer_set_selected_index(code_list_layer, index, MenuRowAlignCenter, false);
  }


  refresh_all();
}

static void persist_do_writeback(void) {
  bool writeback_ok = true;
  int writeback_status = S_SUCCESS;
  if ((persist_writeback & PWUTCOffset) == PWUTCOffset) {
    // Write back persistent things
    writeback_status = min(0, persist_write_int(P_UTCOFFSET, utc_offset));
    writeback_ok &= writeback_status == S_SUCCESS;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Wrote UTC offset, status %d", writeback_status);
  }

  if (startup_selected_list_index != menu_layer_get_selected_index(code_list_layer).row && writeback_ok) {
    writeback_status = min(0, persist_write_int(P_SELECTED_LIST_INDEX, menu_layer_get_selected_index(code_list_layer).row));
    writeback_ok &= writeback_status == S_SUCCESS;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Wrote list index, status %d", writeback_status);
  }

  if ((persist_writeback & PWTokens) == PWTokens && writeback_ok) {
    writeback_status = min(0, persist_write_int(P_TOKENS_COUNT, token_list_length()));
    writeback_ok &= writeback_status == S_SUCCESS;
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Wrote token count, status %d", writeback_status);

    TokenListNode* node = token_list;
    short idx = 0;
    while (node && writeback_ok) {
      writeback_status = (persist_write_data(P_TOKENS_START + idx, node->key, sizeof(TokenInfo)) == sizeof(TokenInfo)) ? S_SUCCESS : -64;
      writeback_ok &= writeback_status == S_SUCCESS;
      idx++;
      node = node->next;
    }

    // APP_LOG(APP_LOG_LEVEL_INFO, "Wrote %d tokens, status %d", idx, writeback_status);
  }

  // This is stored in a seperate storage area keyed by ID because a) it's easier to have truly variable-length secrets this way, and b) it's easier to ensure secrets are deleted (as opposed to relying on them getting overwritten)
  if ((persist_writeback & PWSecrets) == PWSecrets && writeback_ok) {
    TokenListNode* node = token_list;
    while (node && writeback_ok) {
      writeback_status = (persist_write_data(P_SECRETS_START + node->key->id, node->key->secret, node->key->secret_length) == node->key->secret_length) ? S_SUCCESS : -63;
      writeback_ok &= writeback_status == S_SUCCESS;
      node = node->next;
    }

    // APP_LOG(APP_LOG_LEVEL_INFO, "Wrote secrets, status %d", writeback_status);
  }

  if (!writeback_ok) {
    persist_error_push(writeback_status);
  }
  persist_writeback = PWNone;
}

void handle_deinit() {
  persist_do_writeback();

  token_list_clear();
  menu_layer_destroy(code_list_layer);
  layer_destroy(bar_layer);
  text_layer_destroy(no_tokens_layer);
  window_destroy(window);
}

int main() {
  handle_init();
  app_event_loop();
  handle_deinit();
}
