# main/CMakeLists.txt
#
# List every .c file in this component (screens/ and services/).
# Add a line here each time you create a new screen or service file.

idf_component_register(
    SRCS
        "main.c"
        "app_manager.c"

        # ── Services ───────────────────────────────────────────────
        "services/wifi_service.c"
        "services/service_template.c"
        "services/clock_service.c"
        # "services/service_yourname.c"

        # ── Screens ────────────────────────────────────────────────
        "screens/screen_watchface.c"
        "screens/screen_menu.c"
        "screens/screen_wifi.c"
        "screens/screen_settings.c"
        #"screens/screen_heartrate.c"
        # "screens/screen_settings.c"
        # "screens/screen_activity.c"
        # "screens/screen_notifications.c"
        
        # ── Registry ────────────────────────────────────────────────
        "settings_registry.h"
        "ui/ui_kit.c"

        # ── UI ─────────────────────────────────────────────────────
        "ui/ui_kit.c"
    

    INCLUDE_DIRS
        "."          # app_manager.h, screen.h, service.h
        "screens"
        "services"   # wifi_service.h, etc.
        "../components/esp_lvgl_port/include"
        "ui"

        
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        lwip

)
