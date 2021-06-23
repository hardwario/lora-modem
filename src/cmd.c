#include "cmd.h"
#include "lrw.h"
#include "system.h"
#include "radio.h"
#include "config.h"
#include "sx1276.h"
#include "gpio.h"
#include "log.h"
#include "rtc.h"

static bool _cmd_param_parse_is_enable(atci_param_t *param, bool *enable);

static struct
{
    uint8_t port;
} _at;

static void cmd_mode_get(void)
{
    atci_print("+OK=1");
}

static void cmd_deveui_get(void)
{
    uint8_t *data = lrw_deveui_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 8);
}

static void cmd_dutycycle_get(void)
{
    atci_printf("+OK=%d", lrw_duty_cycle_get());
}

static void cmd_dutycycle_set(atci_param_t *param)
{
    bool enable;
    if (_cmd_param_parse_is_enable(param, &enable))
    {
        lrw_duty_cycle_set(enable);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_dr_get(void)
{
    atci_printf("+OK=%d", lrw_tx_datarate_get());
}

static void cmd_dr_set(atci_param_t *param)
{
    uint32_t value;
    if (atci_param_get_uint(param, &value) && (value <= 15))
    {
        lrw_tx_datarate_set(value);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_rfq_get()
{
    atci_printf("+OK=%d,%d", lrw_rssi_get(), lrw_snr_get());
}

static void cmd_deveui_set(atci_param_t *param)
{
    uint8_t deveui[8];
    if (atci_param_get_buffer_from_hex(param, deveui, 8) == 8)
    {
        lrw_deveui_set(deveui);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_devaddr_get(void)
{
    uint32_t devaddr = lrw_devaddr_get();
    atci_printf("+OK=%08X", devaddr);
}

static void cmd_devaddr_set(atci_param_t *param)
{
    uint32_t devaddr;
    if (atci_param_get_buffer_from_hex(param, &devaddr, 4) == 4)
    {
        lrw_devaddr_set(devaddr);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_class_get(void)
{
    atci_printf("+OK=%d", lrw_class_get());
}

static void cmd_class_set(atci_param_t *param)
{
    uint32_t value;

    if (atci_param_get_uint(param, &value) && lrw_class_change(value))
    {
        lrw_save_config();
        atci_print("+OK");
        return;
    }
    atci_print("+ERR=-2");
}

static void cmd_band_get(void)
{
    atci_printf("+OK=%d", lrw_region_get());
}

static void cmd_band_set(atci_param_t *param)
{
    uint32_t value;

    if (atci_param_get_uint(param, &value) && lrw_region_set(value))
    {
        lrw_save_config();
        atci_print("+OK");
        return;
    }
    atci_print("+ERR=-2");
}

static void cmd_appui_get(void)
{
    uint8_t *data = lrw_appeui_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 8);
}

static void cmd_appui_set(atci_param_t *param)
{
    uint8_t appeui[8];
    if (atci_param_get_buffer_from_hex(param, appeui, 8) == 8)
    {
        lrw_appeui_set(appeui);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_appkey_get(void)
{
    uint8_t *data = lrw_appkey_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 16);
}

static void cmd_appkey_set(atci_param_t *param)
{
    uint8_t appkey[16];
    if (atci_param_get_buffer_from_hex(param, appkey, 16) == 16)
    {
        lrw_appkey_set(appkey);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_nwk_get(void)
{
    atci_printf("+OK=%d", lrw_public_network_get());
}

static void cmd_nwk_set(atci_param_t *param)
{
    bool enable;
    if (_cmd_param_parse_is_enable(param, &enable))
    {
        lrw_public_network_set(enable);
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_join(atci_param_t *param)
{
    (void)param;

    if (lrw_otaa_get())
    {
        if (lrw_join())
        {
            atci_print("+OK");
        }
        else
        {
            atci_print("+ERR=-18");
        }
    }
    else
    {
        atci_print("+ERR=-14");
    }
}

static void cmd_putx_data(atci_param_t *param)
{
    if (lrw_send(_at.port, param->txt, param->length, LRW_UNCONFIRMED_MSG))
    {
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-18"); // TODO
    }
}

static void cmd_putx(atci_param_t *param)
{
    uint32_t value;

    if (!atci_param_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    if (!atci_param_is_comma(param))
    {
        atci_print("+ERR=-2");
        return;
    }

    _at.port = value;

    if (!atci_param_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    atci_set_read_next_data(value, cmd_putx_data);
}

static void cmd_pctx_data(atci_param_t *param)
{
    if (lrw_send(_at.port, param->txt, param->length, LRW_CONFIRMED_MSG))
    {
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-18"); // TODO
    }
}

static void cmd_pctx(atci_param_t *param)
{
    uint32_t value;

    if (!atci_param_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    if (!atci_param_is_comma(param))
    {
        atci_print("+ERR=-2");
        return;
    }

    _at.port = value;

    if (!atci_param_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    atci_set_read_next_data(value, cmd_pctx_data);
}

static void cmd_chmask_get(void)
{
    lrw_channel_list_t list = lrw_get_channel_list();
    atci_print("+OK=");
    atci_print_buffer_as_hex(list.chmask, list.chmask_length * sizeof(uint16_t));
}

static void cmd_chmask_set(atci_param_t *param)
{
    uint16_t chmask[LRW_CHMASK_LENGTH];
    memset(chmask, 0, sizeof(chmask));

    size_t length = atci_param_get_buffer_from_hex(param, chmask, sizeof(chmask));

    if (length == 0)
    {
        atci_print("+ERR=-2");
        return;
    }

    if (lrw_chmask_set(chmask))
    {
        lrw_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-2");
    }
}

static void cmd_rep_get(void)
{
    atci_printf("+OK=%d", lrw_unconfirmed_message_repeats_get());
}

static void cmd_rep_set(atci_param_t *param)
{
    uint32_t value;
    if (atci_param_get_uint(param, &value) && lrw_unconfirmed_message_repeats_set(value))
    {
        lrw_save_config();
        atci_print("+OK");
        return;
    }
    atci_print("+ERR=-2");
}

static void cmd_facnew(atci_param_t *param)
{
    (void)param;
    atci_print("+OK\r\n\r\n");
    config_reset();
    config_save();
    cmd_event(0,1);
    rtc_delay_ms(40);
    system_reset();
}

static void cmd_channels_get(void)
{
    lrw_channel_list_t list = lrw_get_channel_list();

    // log_debug("%d %d", list.length, list.chmask_length);
    // log_dump(list.chmask, list.chmask_length * 2, "masks");
    // log_dump(list.chmask_default, list.chmask_length * 2, "default_mask");

    for (uint8_t i = 0; i < list.length; i++)
    {
        if (list.channels[i].Frequency == 0)
            continue;

        uint8_t is_enable = (i / 16) < list.chmask_length ? (list.chmask[i / 16] >> (i % 16)) & 0x01 : 0;

        atci_printf("$CHANNELS: %d,%d,%d,%d,%d,%d\r\n",
                    is_enable,
                    list.channels[i].Frequency,
                    list.channels[i].Rx1Frequency,
                    list.channels[i].DrRange.Fields.Min,
                    list.channels[i].DrRange.Fields.Max,
                    list.channels[i].Band);
    }
    atci_print("+OK");
}

static void cmd_reboot(atci_param_t *param)
{
    (void)param;
    system_reset();
}

static void cmd_dbg(atci_param_t *param)
{
    (void)param;
    // RF_IDLE = 0,   //!< The radio is idle
    // RF_RX_RUNNING, //!< The radio is in reception state
    // RF_TX_RUNNING, //!< The radio is in transmission state
    // RF_CAD,        //!< The radio is doing channel activity detection
    atci_printf("$DBG: \"stop_mode_mask\",%d\r\n", system_get_stop_mode_mask());
    atci_printf("$DBG: \"radio_state\",%d\r\n", Radio.GetStatus());
    atci_print("OK");
}

static const atci_command_t _cmd_commands[] = {
    {"+CLASS", NULL, cmd_class_set, cmd_class_get, NULL, "Class mode"},
    {"+BAND", NULL, cmd_band_set, cmd_band_get, NULL, "Radio band"},
    {"+NWK", NULL, cmd_nwk_set, cmd_nwk_get, NULL, "Public network"},
    {"+MODE", NULL, NULL, cmd_mode_get, NULL, "Activation mode 1:OTTA 0:ABP"},
    {"+DUTYCYCLE", NULL, cmd_dutycycle_set, cmd_dutycycle_get, NULL, "Dutycycle"},
    {"+DR", NULL, cmd_dr_set, cmd_dr_get, NULL, "Data rate"},
    {"+RFQ", NULL, NULL, cmd_rfq_get, NULL, "RF parameter of last received message"},
    {"+DEVEUI", NULL, cmd_deveui_set, cmd_deveui_get, NULL, "Device identifier"},
    {"+APPEUI", NULL, cmd_appui_set, cmd_appui_get, NULL, "Application identifier"},
    {"+APPKEY", NULL, cmd_appkey_set, cmd_appkey_get, NULL, "Application key"},
    {"+DEVADDR", NULL, cmd_devaddr_set, cmd_devaddr_get, NULL, "Device address"},
    {"+JOIN", cmd_join, NULL, NULL, NULL, "Send OTAA Join packet"},
    {"+PUTX", cmd_putx, NULL, NULL, NULL, "Send string frame with port"},
    {"+PCTX", cmd_pctx, NULL, NULL, NULL, "Send string frame with port"},
    {"+CHMASK", NULL, cmd_chmask_set, cmd_chmask_get, NULL, "Channels mask"},
    {"+REP", NULL, cmd_rep_set, cmd_rep_get, NULL, "Unconfirmed message repeats [1..15]"},
    {"+FACNEW", cmd_facnew, NULL, NULL, NULL, "Restore modem to factory"},
    {"$CHANNELS", NULL, NULL, cmd_channels_get, NULL, ""},
    {"+REBOOT", cmd_reboot, NULL, NULL, NULL, "Reboot"},
    {"$DBG", cmd_dbg, NULL, NULL, NULL, ""},
    ATCI_COMMAND_CLAC,
    ATCI_COMMAND_HELP};

void cmd_init()
{
    memset(&_at, 0, sizeof(_at));

    atci_init(_cmd_commands, ATCI_COMMANDS_LENGTH(_cmd_commands));
}

void cmd_event(const uint8_t type, const uint8_t no)
{
    atci_printf("+EVENT=%d,%d\r\n\r\n", type, no);
}

static bool _cmd_param_parse_is_enable(atci_param_t *param, bool *enable)
{
    if (param->length == 1)
    {
        if (param->txt[0] == '0')
        {
            *enable = false;
            return true;
        }
        else if (param->txt[0] == '1')
        {
            *enable = true;
            return true;
        }
    }
    return false;
}
