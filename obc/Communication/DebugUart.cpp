#include "DebugUart.hpp"

namespace
{
constexpr uint32_t DebugBaudRate = 115200u;

void WriteCharacter(char character)
{
    while ((USART2->ISR & USART_ISR_TXE) == 0u)
    {
    }

    USART2->TDR = static_cast<uint8_t>(character);
}

void WriteText(const char* text)
{
    if (text == nullptr)
    {
        return;
    }

    while (*text != '\0')
    {
        WriteCharacter(*text);
        ++text;
    }
}

void WriteUnsigned(uint32_t value)
{
    char digits[10];
    uint8_t count = 0u;

    do
    {
        digits[count] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
        ++count;
    } while ((value != 0u) && (count < sizeof(digits)));

    while (count > 0u)
    {
        --count;
        WriteCharacter(digits[count]);
    }
}

void WriteSignedTenths(int16_t value)
{
    int32_t signed_value = value;

    if (signed_value < 0)
    {
        WriteCharacter('-');
        signed_value = -signed_value;
    }

    WriteUnsigned(static_cast<uint32_t>(signed_value / 10));
    WriteCharacter('.');
    WriteCharacter(static_cast<char>(
        '0' + (signed_value % 10)));
}

void WriteUnsignedTenths(uint16_t value)
{
    WriteUnsigned(value / 10u);
    WriteCharacter('.');
    WriteCharacter(static_cast<char>('0' + (value % 10u)));
}

void WriteHexNibble(uint8_t value)
{
    value &= 0x0Fu;
    WriteCharacter(static_cast<char>(
        (value < 10u) ? ('0' + value) : ('A' + value - 10u)));
}

void WriteHex8(uint8_t value)
{
    WriteHexNibble(static_cast<uint8_t>(value >> 4u));
    WriteHexNibble(value);
}

void WriteHex32(uint32_t value)
{
    for (int8_t shift = 28; shift >= 0; shift -= 4)
    {
        WriteHexNibble(static_cast<uint8_t>(value >> shift));
    }
}

const char* HalStatusText(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK:
            return "OK";
        case HAL_ERROR:
            return "ERROR";
        case HAL_BUSY:
            return "BUSY";
        case HAL_TIMEOUT:
            return "TIMEOUT";
        default:
            return "UNKNOWN";
    }
}
}

void DebugUart_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    USART2->CR1 = 0u;
    USART2->CR2 = 0u;
    USART2->CR3 = 0u;
    USART2->BRR =
        (HAL_RCC_GetPCLK1Freq() + (DebugBaudRate / 2u))
        / DebugBaudRate;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;

    WriteText("\r\nCubeSat OBC debug console ready (115200 8N1)\r\n");
}

void DebugUart_ReportWhoAmI(
    HAL_StatusTypeDef i2c_status,
    uint8_t node_id)
{
    WriteText("WHOAMI I2C=");
    WriteText(HalStatusText(i2c_status));

    if (i2c_status == HAL_OK)
    {
        WriteText(" node=0x");
        WriteHex8(node_id);

        if (node_id == PAYLOAD_NODE_ID)
        {
            WriteText(" expected");
        }
        else
        {
            WriteText(" WRONG_ID");
        }
    }

    WriteText("\r\n");
}

void DebugUart_ReportPayload(
    HAL_StatusTypeDef i2c_status,
    const PayloadData_t* payload_data,
    uint8_t crc_valid,
    uint32_t calculated_crc)
{
    WriteText("DATA I2C=");
    WriteText(HalStatusText(i2c_status));

    if ((i2c_status != HAL_OK) || (payload_data == nullptr))
    {
        WriteText("\r\n");
        return;
    }

    WriteText(" ts=");
    WriteUnsigned(payload_data->timestamp_ms);
    WriteText("ms temp=");
    WriteSignedTenths(payload_data->temperature_c_x10);
    WriteText("C humidity=");
    WriteUnsignedTenths(payload_data->humidity_pct_x10);
    WriteText("% radiation=");
    WriteUnsigned(payload_data->radiation_cps);
    WriteText("cps battery=");
    WriteUnsigned(payload_data->battery_pct);
    WriteText("% node=0x");
    WriteHex8(payload_data->node_id);
    WriteText(" flags=0x");
    WriteHex8(payload_data->flags);
    WriteText(" CRC=");

    if (crc_valid != 0u)
    {
        WriteText("OK");
    }
    else
    {
        WriteText("FAIL received=0x");
        WriteHex32(payload_data->crc32);
        WriteText(" calculated=0x");
        WriteHex32(calculated_crc);
    }

    WriteText("\r\n");
}
