#include "esp_err.h"
#include "esp_log.h"

#include "display.h"
#include "ui.h"

void app_main(void)
{
    display_init();

    if (example_lvgl_lock(-1))
    {
        create_ui();
        example_lvgl_unlock();
    }
}