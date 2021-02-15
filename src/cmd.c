#include "cmd.h"
#include "lora.h"

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
    uint8_t *data = lora_config_deveui_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 8);
}

static void cmd_appui_get(void)
{
    uint8_t *data = lora_config_joineui_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 8);
}

static void cmd_appui_set(atci_param_t *param)
{
    uint8_t joineui[8];
    if (atci_get_buffer_from_hex(param, joineui, 8) == 8)
    {
        lora_config_joineui_set(joineui);
        atci_print("+OK");
    }
    else
    {
        atci_print("+ERR=-3");
    }
}

static void cmd_appkey_get(void)
{
    uint8_t *data = lora_config_appkey_get();
    atci_print("+OK=");
    atci_print_buffer_as_hex(data, 16);
}

static void cmd_appkey_set(atci_param_t *param)
{
    uint8_t appkey[16];
    if (atci_get_buffer_from_hex(param, appkey, 16) == 16)
    {
        lora_config_appkey_set(appkey);
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
    LoraErrorStatus status = LORA_Join();
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

    if (LORA_send(&_at.lora_data, LORAWAN_UNCONFIRMED_MSG) == LORA_SUCCESS)
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

    if (!atci_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    if (!atci_is_comma(param))
    {
        atci_print("+ERR=-2");
        return;
    }

    _at.lora_data.Port = value;

    if (!atci_get_uint(param, &value) || value > 255)
    {
        atci_print("+ERR=-2");
        return;
    }

    atci_set_read_next_data(value, cmd_putx_data);
}

static const atci_command_t _cmd_commands[] = {
    {"+MODE", NULL, NULL, cmd_mode_get, NULL, "Activation mode"},
    {"+DEVEUI", NULL, NULL, cmd_deveui_get, NULL, "Device identifier"},
    {"+APPEUI", NULL, cmd_appui_set, cmd_appui_get, NULL, "Application identifier"},
    {"+APPKEY", NULL, cmd_appkey_set, cmd_appkey_get, NULL, "Application key"},
    {"+JOIN", cmd_join, NULL, NULL, NULL, "Send OTAA Join packet"},
    {"+PUTX", cmd_putx, NULL, NULL, NULL, "Send string frame with port"},
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
