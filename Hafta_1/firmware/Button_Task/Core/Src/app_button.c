#include "main.h"
#include "app_button.h"
#include "app_config.h"
#include "app_rtos.h"
#include "app_ring_buffer.h"
#include "app_timestamp.h"

static volatile uint32_t s_last_edge_ms;
static volatile bool     s_edge_seen;
static volatile uint16_t s_event_counter;

void Button_Init(void)
{
    s_last_edge_ms  = 0u;
    s_edge_seen     = false;
    s_event_counter = 0u;
}

void Button_ResetForScenario(void)
{
    s_event_counter = 0u;
}

void Button_HandleEXTI(void)
{
    uint32_t t_entry = Timestamp_Now();   /* ISR girisi: kabul edilirse t0 bu */
    uint32_t now_ms  = HAL_GetTick();

    bool quiet = !s_edge_seen || (uint32_t)(now_ms - s_last_edge_ms) >= APP_DEBOUNCE_MS;
    s_last_edge_ms = now_ms;
    s_edge_seen    = true;

    bool pressed = HAL_GPIO_ReadPin(USER_BUTTON_PORT, USER_BUTTON_PIN) == USER_BUTTON_PRESSED_STATE;
    if (!quiet || !pressed || !g_running) {
        return;
    }

    uint16_t event_id    = (uint16_t)(s_event_counter + 1u);
    s_event_counter      = event_id;
    uint8_t  scenario_id = g_cfg.scenario_id;
    uint32_t t0_abs_ms   = now_ms - g_run_start_ms;

    ButtonEvent_t evt = {
        .t0          = t_entry,
        .t0_abs_ms   = t0_abs_ms,
        .event_id    = event_id,
        .ring_index  = RingBuffer_Alloc(event_id, scenario_id, t_entry, t0_abs_ms),
        .scenario_id = scenario_id,
    };

    /* ISR context: timeout 0 zorunlu. ButtonQueue (8) dolu ise DROP. */
    if (osMessageQueuePut(ButtonQueueHandle, &evt, 0u, 0u) != osOK) {
        RingBuffer_CloseDropIsr(evt.ring_index, event_id);
    }
}
