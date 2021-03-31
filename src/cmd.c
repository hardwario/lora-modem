#include "cmd.h"
#include "lora.h"
#include "system.h"

static struct
{
    lora_AppData_t lora_data;
    uint8_t buffer[256];
} _at;

static void cmd_mode_get(void)
{
    atci_print("+OK=1");
}

static void cmd_deveui_get(void)
{
    uint8_t *data = lora_deveui_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 8);
}

static void cmd_deveui_set(atci_param_t *param)
{
    uint8_t deveui[8];
    if (atci_param_get_buffer_from_hex(param, deveui, 8) == 8)
    {
        lora_deveui_set(deveui);
        lora_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_devaddr_get(void)
{
    uint32_t devaddr = lora_devaddr_get();
    atci_printf("+OK=%08X", devaddr);
}

static void cmd_devaddr_set(atci_param_t *param)
{
    uint32_t devaddr;
    if (atci_param_get_buffer_from_hex(param, &devaddr, 4) == 4)
    {
        lora_devaddr_set(devaddr);
        lora_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_class_get(void)
{
    atci_printf("+OK=%d", lora_class_get());
}

static void cmd_band_get(void)
{
    atci_printf("+OK=%d", lora_region_get());
}

static void cmd_appui_get(void)
{
    uint8_t *data = lora_appeui_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 8);
}

static void cmd_appui_set(atci_param_t *param)
{
    uint8_t appeui[8];
    if (atci_param_get_buffer_from_hex(param, appeui, 8) == 8)
    {
        lora_appeui_set(appeui);
        lora_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_appkey_get(void)
{
    uint8_t *data = lora_appkey_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 16);
}

static void cmd_appkey_set(atci_param_t *param)
{
    uint8_t appkey[16];
    if (atci_param_get_buffer_from_hex(param, appkey, 16) == 16)
    {
        lora_appkey_set(appkey);
        lora_save_config();
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_nwk_get(void)
{
    atci_printf("+OK=%d", lora_public_network_get());
}

static void cmd_nwk_set(atci_param_t *param)
{
    bool ok = false;
    if (param->length == 1)
    {
        if (param->txt[0] == '0')
        {
            lora_public_network_set(false);
            ok = true;
        }
        else if (param->txt[0] == '1')
        {
            lora_public_network_set(true);
            ok = true;
        }
    }
    if (ok) lora_save_config();

    atci_print(ok ? "+OK" : "+ERR=-3");
}

static void cmd_join(atci_param_t *param)
{
    (void)param;
    LoraErrorStatus status = lora_join();
    if (status == LORA_SUCCESS)
    {
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-14"); // TODO
    }
}

static void cmd_putx_data(atci_param_t *param)
{
    _at.lora_data.BuffSize = param->length;

    memcpy(_at.buffer, param->txt, param->length);

    if (lora_send(&_at.lora_data, LORA_UNCONFIRMED_MSG) == LORA_SUCCESS)
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

    _at.lora_data.Port = value;

    if (!atci_param_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    atci_set_read_next_data(value, cmd_putx_data);
}

static void cmd_pctx_data(atci_param_t *param)
{
    _at.lora_data.BuffSize = param->length;

    memcpy(_at.buffer, param->txt, param->length);

    if (lora_send(&_at.lora_data, LORA_CONFIRMED_MSG) == LORA_SUCCESS)
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

    _at.lora_data.Port = value;

    if (!atci_param_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    atci_set_read_next_data(value, cmd_pctx_data);
}

static void cmd_reboot(atci_param_t *param)
{
    (void) param;
    system_reset();
}

static const atci_command_t _cmd_commands[] = {
    {"+CLASS", NULL, NULL, cmd_class_get, NULL, "Class mode"},
    {"+BAND", NULL, NULL, cmd_band_get, NULL, "Radio band"},
    {"+NWK", NULL, cmd_nwk_set, cmd_nwk_get, NULL, "Public network"},
    {"+MODE", NULL, NULL, cmd_mode_get, NULL, "Activation mode"},
    {"+DEVEUI", NULL, cmd_deveui_set, cmd_deveui_get, NULL, "Device identifier"},
    {"+APPEUI", NULL, cmd_appui_set, cmd_appui_get, NULL, "Application identifier"},
    {"+APPKEY", NULL, cmd_appkey_set, cmd_appkey_get, NULL, "Application key"},
    {"+DEVADDR", NULL, cmd_devaddr_set, cmd_devaddr_get, NULL, "Device address"},
    {"+JOIN", cmd_join, NULL, NULL, NULL, "Send OTAA Join packet"},
    {"+PUTX", cmd_putx, NULL, NULL, NULL, "Send string frame with port"},
    {"+PCTX", cmd_pctx, NULL, NULL, NULL, "Send string frame with port"},
    {"+REBOOT", cmd_reboot, NULL, NULL, NULL, "Reboot"},
    ATCI_COMMAND_CLAC,
    ATCI_COMMAND_HELP};

void cmd_init()
{
    memset(&_at, 0, sizeof(_at));

    _at.lora_data.Buff = _at.buffer;

    atci_init(_cmd_commands, ATCI_COMMANDS_LENGTH(_cmd_commands));
}

void cmd_event(const uint8_t type, const uint8_t no)
{
    atci_printf("+EVENT=%d,%d\r\n\r\n", type, no);
}
