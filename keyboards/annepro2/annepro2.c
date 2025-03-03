#include "ch.h"
#include "hal.h"
#include "annepro2.h"
#include "annepro2_ble.h"
#include "spi_master.h"
#include "qmk_ap2_led.h"
#include "protocol.h"

static const SerialConfig ledUartInitConfig = {
    .speed = 115200,
};

/*
 * Some people have serial issues between main chip and led chip.
 * This code allows them to easily reduce the speed to 9600 for testing
 */
static const SerialConfig ledUartRuntimeConfig = {
    .speed = 115200,
};

static const SerialConfig bleUartConfig = {
    .speed = 115200,
};

static uint8_t ledMcuWakeup[11] = {0x7b, 0x10, 0x43, 0x10, 0x03, 0x00, 0x00, 0x7d, 0x02, 0x01, 0x02};

ble_capslock_t BLECapsLock = {._dummy = {0}, .caps_lock = false};

void OVERRIDE bootloader_jump(void) {
    // Send msg to shine to boot into IAP
    annepro2SetIAP();

    // wait for shine to boot into IAP
    wait_ms(15);

    // Load ble into IAP
    annepro2_ble_bootload();
    wait_ms(15);

    // Magic key to set keyboard to IAP
    *((uint32_t *)0x20001ffc) = 0x0000fab2;

    // Load the main MCU into IAP
    __disable_irq();
    NVIC_SystemReset();
}

void OVERRIDE keyboard_pre_init_kb(void) {
#if HAL_USE_SPI == TRUE
    spi_init();
#endif
    // Start LED UART
    sdStart(&SD0, &ledUartInitConfig);
    /* Let the LED chip settle a bit before switching the mode.
     * That helped at least one person. */
    wait_ms(ANNEPRO2_LED_MCU_WAKEUP_DELAY);
    sdWrite(&SD0, ledMcuWakeup, 11);

    // wait to receive response from wakeup
    wait_ms(15);

    protoInit(&proto, ledCommandCallback);

    // loop to clear out receive buffer from shine wakeup
    while (!sdGetWouldBlock(&SD0)) sdGet(&SD0);

    sdStart(&SD0, &ledUartRuntimeConfig);
}

void OVERRIDE keyboard_post_init_kb(void) {
    // Start BLE UART
    sdStart(&SD1, &bleUartConfig);
    annepro2_ble_startup();

    // Give the send uart thread some time to
    // send out the queue before we read back
    wait_ms(15);

    // loop to clear out receive buffer from ble wakeup
    while (!sdGetWouldBlock(&SD1)) sdGet(&SD1);

    annepro2LedGetStatus();

    keyboard_post_init_user();
}

void OVERRIDE matrix_init_kb(void) { matrix_init_user(); }

void matrix_scan_kb() {
    // if there's stuff on the ble serial buffer
    // read it into the capslock struct
    while (!sdGetWouldBlock(&SD1)) {
        sdReadTimeout(&SD1, (uint8_t *)&BLECapsLock, sizeof(ble_capslock_t), 10);

        // if it's capslock from ble, darken led
        if (BLECapsLock.caps_lock) {
            // annepro2LedClearMask(MATRIX_COLS * 2);
        } else {
            // annepro2LedSetMask(MATRIX_COLS * 2);
        }
    }

    /* While there's data from LED keyboard sent - read it. */
    while (!sdGetWouldBlock(&SD0)) {
        uint8_t byte = sdGet(&SD0);
        protoConsume(&proto, byte);
    }

    matrix_scan_user();
}

/*!
 * @returns false   processing for this keycode has been completed.
 */
bool OVERRIDE process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        if (annepro2LedStatus.matrixEnabled && annepro2LedStatus.isReactive) {
            annepro2LedForwardKeypress(record->event.key.row, record->event.key.col);
        }

        const annepro2Led_t blue = {
            .p.blue  = 0xff,
            .p.red   = 0x00,
            .p.green = 0x00,
            .p.alpha = 0xff,
        };

        switch (keycode) {
            case KC_AP2_BT1:
                annepro2_ble_broadcast(0);
                annepro2LedBlink(record->event.key.row, record->event.key.col, blue, 8, 50);
                return false;

            case KC_AP2_BT2:
                annepro2_ble_broadcast(1);
                annepro2LedBlink(record->event.key.row, record->event.key.col, blue, 8, 50);
                return false;

            case KC_AP2_BT3:
                annepro2_ble_broadcast(2);
                annepro2LedBlink(record->event.key.row, record->event.key.col, blue, 8, 50);
                return false;

            case KC_AP2_BT4:
                annepro2_ble_broadcast(3);
                annepro2LedBlink(record->event.key.row, record->event.key.col, blue, 8, 50);
                return false;

            case KC_AP2_USB:
                annepro2_ble_disconnect();
                return false;

            case KC_AP2_BT_UNPAIR:
                annepro2_ble_unpair();
                return false;

            case KC_AP_LED_OFF:
                annepro2LedDisable();
                break;

            case KC_AP_LED_ON:
                if (annepro2LedStatus.matrixEnabled) {
                    annepro2LedNextProfile();
                } else {
                    annepro2LedEnable();
                }
                annepro2LedResetForegroundColor();
                break;

            case KC_AP_LED_NEXT_PROFILE:
                annepro2LedNextProfile();
                annepro2LedResetForegroundColor();
                break;

            case KC_AP_LED_PREV_PROFILE:
                annepro2LedPrevProfile();
                annepro2LedResetForegroundColor();
                break;

            case KC_AP_LED_NEXT_INTENSITY:
                annepro2LedNextIntensity();
                annepro2LedResetForegroundColor();
                return false;

            case KC_AP_LED_SPEED:
                annepro2LedNextAnimationSpeed();
                annepro2LedResetForegroundColor();
                return false;

            default:
                break;
        }
    }
    return process_record_user(keycode, record);
}
