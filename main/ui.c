#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "lvgl.h"
#include "esp_log.h"

#include "user_encoder_bsp.h"
#include "user_config.h"

#include "ui.h"
#include "display.h"
#include <time.h>

#define VALUE_MIN 0
#define VALUE_MAX 300
#define MAX_PLAYERS 5

typedef struct {
    uint32_t elapsed_ms;
    bool paused;
} player_timer_t;

typedef struct {
    int num_players;
    int current_player;
    player_timer_t players[MAX_PLAYERS];
    char player_names[MAX_PLAYERS][32];
    uint32_t last_update_time;
    bool game_running;
} game_state_t;

static game_state_t game_state = {
    .num_players = 0,
    .current_player = 0,
    .game_running = false,
    .last_update_time = 0
};

static lv_obj_t *game_screen = NULL;
static lv_obj_t *player_labels[MAX_PLAYERS];
static lv_obj_t *timer_labels[MAX_PLAYERS];
static lv_obj_t *player_boxes[MAX_PLAYERS];
static lv_obj_t *pause_btn_label;
static lv_obj_t *name_textarea = NULL;
static lv_timer_t *game_timer = NULL;
static int current_name_input_player = 0;

static void format_time(uint32_t ms, char *buf, size_t buf_size)
{
    uint32_t hours = ms / 3600000;
    uint32_t minutes = (ms % 3600000) / 60000;
    uint32_t seconds = (ms % 60000) / 1000;
    uint32_t milliseconds = (ms % 1000) / 10;
    
    snprintf(buf, buf_size, "%02d:%02d:%02d.%02d", 
             hours, minutes, seconds, milliseconds);
}

static void show_game_screen(void);

static void return_to_launcher(void)
{
    const esp_partition_t *factory =
        esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_FACTORY,
            NULL);

    if (factory)
    {
        esp_ota_set_boot_partition(factory);
        esp_restart();
    }
}

static void hotspot_event_cb(lv_event_t *e)
{
    return_to_launcher();
}

static void player_select_btn_event_cb(lv_event_t *e);
static void show_player_names_screen(void);

static void player_select_btn_event_cb(lv_event_t *e)
{
    int num_players = (int)(intptr_t)lv_event_get_user_data(e);
    game_state.num_players = num_players;
    game_state.current_player = 0;
    game_state.game_running = false;
    current_name_input_player = 0;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        game_state.players[i].elapsed_ms = 0;
        game_state.players[i].paused = true;
        snprintf(game_state.player_names[i], sizeof(game_state.player_names[i]), 
                 "Player %d", i + 1);
    }
    
    show_player_names_screen();
}

static void show_player_select_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Liczba graczy");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    for (int i = 1; i <= MAX_PLAYERS; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 70, 70);
        lv_obj_align(btn, LV_ALIGN_CENTER, -150 + i * 55, 0);

        lv_obj_set_style_border_width(btn, 3, 0);
        lv_obj_set_style_border_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d", i);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, player_select_btn_event_cb, 
                           LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *exit_btn = lv_btn_create(scr);
    lv_obj_set_size(exit_btn, LV_PCT(100), 40);
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *exit_label = lv_label_create(exit_btn);
    lv_label_set_text(exit_label, "Wyjscie");
    lv_obj_center(exit_label);
    lv_obj_add_event_cb(exit_btn, hotspot_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_scr_load(scr);
}

static void next_player_name_btn_event_cb(lv_event_t *e);
static void prev_player_name_btn_event_cb(lv_event_t *e);

static void start_game_btn_event_cb(lv_event_t *e)
{
    if (name_textarea) {
        const char *text = lv_textarea_get_text(name_textarea);
        if (text && text[0] != '\0') {
            snprintf(game_state.player_names[current_name_input_player], 
                     sizeof(game_state.player_names[0]), "%s", text);
        }
    }
    show_game_screen();
}

static void next_player_name_btn_event_cb(lv_event_t *e)
{
    if (name_textarea) {
        const char *text = lv_textarea_get_text(name_textarea);
        if (text && text[0] != '\0') {
            snprintf(game_state.player_names[current_name_input_player], 
                     sizeof(game_state.player_names[0]), "%s", text);
        }
    }
    
    if (current_name_input_player < game_state.num_players - 1) {
        current_name_input_player++;
        show_player_names_screen();
    }
}

static void prev_player_name_btn_event_cb(lv_event_t *e)
{
    if (name_textarea) {
        const char *text = lv_textarea_get_text(name_textarea);
        if (text && text[0] != '\0') {
            snprintf(game_state.player_names[current_name_input_player], 
                     sizeof(game_state.player_names[0]), "%s", text);
        }
    }
    
    if (current_name_input_player > 0) {
        current_name_input_player--;
        show_player_names_screen();
    }
}

static void show_player_names_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text_fmt(title, "Imie gracza %d/%d", current_name_input_player + 1, game_state.num_players);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    name_textarea = lv_textarea_create(scr);
    lv_textarea_set_max_length(name_textarea, 31);
    lv_obj_set_size(name_textarea, 180, 40);
    lv_obj_align(name_textarea, LV_ALIGN_TOP_MID, 0, 50);
    
    lv_textarea_set_text(name_textarea, game_state.player_names[current_name_input_player]);

    lv_obj_t *prev_btn = lv_btn_create(scr);
    lv_obj_set_size(prev_btn, 75, 35);
    lv_obj_align(prev_btn, LV_ALIGN_TOP_LEFT, 10, 100);
    lv_obj_set_style_bg_color(prev_btn, lv_palette_main(LV_PALETTE_GREY), 0);
    if (current_name_input_player == 0) {
        lv_obj_add_state(prev_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_event_cb(prev_btn, prev_player_name_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, "WSTECZ");
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_12, 0);
    lv_obj_center(prev_label);

    lv_obj_t *next_btn = lv_btn_create(scr);
    lv_obj_set_size(next_btn, 75, 35);
    lv_obj_align(next_btn, LV_ALIGN_TOP_RIGHT, -10, 100);
    lv_obj_set_style_bg_color(next_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    if (current_name_input_player == game_state.num_players - 1) {
        lv_obj_set_style_bg_color(next_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_t *next_label = lv_label_create(next_btn);
        lv_label_set_text(next_label, "START");
        lv_obj_set_style_text_font(next_label, &lv_font_montserrat_12, 0);
        lv_obj_center(next_label);
        lv_obj_add_event_cb(next_btn, start_game_btn_event_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_t *next_label = lv_label_create(next_btn);
        lv_label_set_text(next_label, "DALEJ");
        lv_obj_set_style_text_font(next_label, &lv_font_montserrat_12, 0);
        lv_obj_center(next_label);
        lv_obj_add_event_cb(next_btn, next_player_name_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *keyboard = lv_keyboard_create(scr);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_size(keyboard, LV_PCT(100), LV_PCT(45));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, name_textarea);

    lv_scr_load(scr);
}

static void update_player_displays(void)
{
    static char buf[16];
    
    for (int i = 0; i < game_state.num_players; i++) {
        format_time(game_state.players[i].elapsed_ms, buf, sizeof(buf));
        lv_label_set_text(timer_labels[i], buf);
        
        if (i == game_state.current_player) {
            lv_obj_set_style_border_width(player_boxes[i], 5, 0);
            lv_obj_set_style_border_color(player_boxes[i], 
                                         lv_palette_main(LV_PALETTE_GREEN), 0);
        } else {
            lv_obj_set_style_border_width(player_boxes[i], 2, 0);
            lv_obj_set_style_border_color(player_boxes[i], 
                                         lv_palette_main(LV_PALETTE_GREY), 0);
        }
    }
}

static void game_timer_callback(lv_timer_t *timer)
{
    if (!game_state.game_running) {
        return;
    }
    
    player_timer_t *current = &game_state.players[game_state.current_player];
    if (!current->paused) {
        current->elapsed_ms += 100;
    }
    
    update_player_displays();
}

static void next_player_btn_event_cb(lv_event_t *e)
{
    // Pauzuj wszystkie stopery
    for (int i = 0; i < game_state.num_players; i++) {
        game_state.players[i].paused = true;
    }
    game_state.game_running = false;
    
    game_state.current_player = (game_state.current_player + 1) % game_state.num_players;
    
    // Zaktualizuj label przycisku na START
    lv_label_set_text(pause_btn_label, "START");
    
    update_player_displays();
}

static void player_timer_btn_event_cb(lv_event_t *e)
{
    int player_num = (int)(intptr_t)lv_event_get_user_data(e);
    
    // Pauzuj wszystkie stopery
    for (int i = 0; i < game_state.num_players; i++) {
        game_state.players[i].paused = true;
    }
    game_state.game_running = false;
    
    game_state.current_player = player_num;
    
    // Zaktualizuj label przycisku na START
    lv_label_set_text(pause_btn_label, "START");
    
    update_player_displays();
}

static void pause_play_btn_event_cb(lv_event_t *e)
{
    player_timer_t *current = &game_state.players[game_state.current_player];
    current->paused = !current->paused;
    game_state.game_running = !current->paused;
    
    const char *text = current->paused ? "START" : "PAUSE";
    lv_label_set_text(pause_btn_label, text);
    update_player_displays();
}

static void back_to_menu_btn_event_cb(lv_event_t *e)
{
    if (game_timer) {
        lv_timer_del(game_timer);
        game_timer = NULL;
    }
    show_player_select_screen();
}

static void show_game_screen(void)
{
    game_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(game_screen, lv_color_white(), 0);

    int num_players = game_state.num_players;
    lv_coord_t box_height = 50;
    lv_coord_t spacing = 60;
    lv_coord_t left_offset = 70;
    lv_coord_t right_offset = -30;

    // Lewokolumna - stopery graczy
    for (int i = 0; i < num_players; i++) {
        player_boxes[i] = lv_btn_create(game_screen);
        lv_obj_set_size(player_boxes[i], 110, box_height);
        lv_obj_align(player_boxes[i], LV_ALIGN_LEFT_MID, 
                    left_offset, -110 + (i * spacing));
        lv_obj_add_event_cb(player_boxes[i], player_timer_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        player_labels[i] = lv_label_create(player_boxes[i]);
        lv_label_set_text(player_labels[i], game_state.player_names[i]);
        lv_obj_align(player_labels[i], LV_ALIGN_TOP_LEFT, 5, 2);

        timer_labels[i] = lv_label_create(player_boxes[i]);
        lv_obj_set_style_text_font(timer_labels[i], &lv_font_montserrat_16, 0);
        lv_label_set_text(timer_labels[i], "00:00:00.00");
        lv_obj_align(timer_labels[i], LV_ALIGN_CENTER, 0, 15);
    }

    // Prawakolumna - przyciski sterujące
    lv_obj_t *pause_btn = lv_btn_create(game_screen);
    lv_obj_set_size(pause_btn, 90, 60);
    lv_obj_align(pause_btn, LV_ALIGN_RIGHT_MID, right_offset, -70);
    lv_obj_set_style_bg_color(pause_btn, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_add_event_cb(pause_btn, pause_play_btn_event_cb, LV_EVENT_CLICKED, NULL);

    pause_btn_label = lv_label_create(pause_btn);
    lv_label_set_text(pause_btn_label, "START");
    lv_obj_set_style_text_font(pause_btn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(pause_btn_label);

    lv_obj_t *next_btn = lv_btn_create(game_screen);
    lv_obj_set_size(next_btn, 90, 60);
    lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, right_offset, 0);
    lv_obj_set_style_bg_color(next_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(next_btn, next_player_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, "NEXT");
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_14, 0);
    lv_obj_center(next_label);

    lv_obj_t *menu_btn = lv_btn_create(game_screen);
    lv_obj_set_size(menu_btn, 90, 60);
    lv_obj_align(menu_btn, LV_ALIGN_RIGHT_MID, right_offset, 70);
    lv_obj_set_style_bg_color(menu_btn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_add_event_cb(menu_btn, back_to_menu_btn_event_cb, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *menu_label = lv_label_create(menu_btn);
    lv_label_set_text(menu_label, "MENU");
    lv_obj_set_style_text_font(menu_label, &lv_font_montserrat_14, 0);
    lv_obj_center(menu_label);

    update_player_displays();
    lv_scr_load(game_screen);

    if (game_timer) {
        lv_timer_del(game_timer);
    }
    game_timer = lv_timer_create(game_timer_callback, 100, NULL);
}

static void show_main_screen(void)
{
    show_player_select_screen();
}

static void encoder_task(void *arg)
{
    while (1)
    {
        xEventGroupWaitBits(
            knob_even_,
            BIT0 | BIT1,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        if (example_lvgl_lock(-1))
        {
            example_lvgl_unlock();
        }
    }
}

void create_ui(void)
{
    user_encoder_init();

    xTaskCreate(
        encoder_task,
        "encoder_task",
        4096,
        NULL,
        5,
        NULL);

    show_main_screen();
}