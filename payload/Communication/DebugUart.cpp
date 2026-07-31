#include "DebugUart.hpp"

#include "stm32l4xx_hal.h"

#include <cstdarg>
#include <cstdio>

namespace
{
constexpr uint32_t DebugBaudRate = 115200u;
constexpr size_t DebugFormatBufferSize = 192u;

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
    HAL_GPIO_Init(GPIOA,&gpio);

    USART2->CR1 = 0u;
    USART2->CR2 = 0u;
    USART2->CR3 = 0u;
    USART2->BRR =
        (HAL_RCC_GetPCLK1Freq() + (DebugBaudRate / 2u))
        / DebugBaudRate;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;

    WriteText("\r\nCubeSat Payload debug console ready (115200 8N1)\r\n");
}

void DebugUart_Print(const char* text)
{
    WriteText(text);
}

void DebugUart_Printf(const char* format, ...)
{
    if (format == nullptr)
    {
        return;
    }

    char buffer[DebugFormatBufferSize];
    va_list arguments;
    va_start(arguments,format);
    const int written = vsnprintf(buffer,sizeof(buffer),format,arguments);
    va_end(arguments);

    if (written > 0)
    {
        buffer[sizeof(buffer) - 1u] = '\0';
        WriteText(buffer);
    }
}
