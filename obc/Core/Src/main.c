/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "../../../common/bus_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_FATFS_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  /* obc — smoke test */
  FATFS fs;
  FIL f;
  UINT bw;

  char *start_msg = "Starting SD test...\r\n";
  HAL_UART_Transmit(&huart2, (uint8_t*)start_msg, strlen(start_msg), HAL_MAX_DELAY);

  FRESULT mount_res = f_mount(&fs, "", 1);

  char debug_msg[60];
  int len = sprintf(debug_msg, "f_mount result: %d\r\n", mount_res);
  HAL_UART_Transmit(&huart2, (uint8_t*)debug_msg, len, HAL_MAX_DELAY);

  if (mount_res == FR_OK) {
	  FRESULT open_res = f_open(&f, "HELLO.TXT", FA_CREATE_ALWAYS | FA_WRITE);
	  if (open_res == FR_OK) {
		  f_write(&f, "cubesat\r\n", 9, &bw);
		  f_close(&f);
		  HAL_UART_Transmit(&huart2, (uint8_t*)"SD OK & Wrote!\r\n", 16, HAL_MAX_DELAY);
	  } else {
		  int err_len = sprintf(debug_msg, "f_open failed with: %d\r\n", open_res);
		  HAL_UART_Transmit(&huart2, (uint8_t*)debug_msg, err_len, HAL_MAX_DELAY);
	  }
	}


  // --------------TEST I2C---------------
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) == GPIO_PIN_RESET ||
        HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET) {

        char *err_bus = "BUS ERROR: SDA or SCL is stuck LOW!\r\n";
        HAL_UART_Transmit(&huart2, (uint8_t*)err_bus, strlen(err_bus), HAL_MAX_DELAY);
    } else {
        char *ok_bus = "I2C Bus voltage is HIGH (OK)\r\n";
        HAL_UART_Transmit(&huart2, (uint8_t*)ok_bus, strlen(ok_bus), HAL_MAX_DELAY);
    }
  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  uint8_t val = 0;

	  // master read request - request 1 byte from slave address 0x04 with a 100ms timeout limit
	  // use the common macro NODE_ADDR_PAYLOAD_8BIT (from ../../../common/bus_config.h)
	  if (HAL_I2C_Master_Receive(&hi2c1, NODE_ADDR_PAYLOAD_8BIT, &val, 1, 100) == HAL_OK) {		  // print the received byte via UART upon successful reception
		  char msg[40];
		  int len = sprintf(msg, "payload says 0x%02X\r\n", val);
		  HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, HAL_MAX_DELAY);

		  // toggle the external RGB RED LED to visually indicate a successful read.
		  // We use RGB_RED_GPIO_Port and RGB_RED_Pin instead of PA5 to avoid hardware conflicts with the SPI clock (SCK).
		  HAL_GPIO_TogglePin(RGB_RED_GPIO_Port, RGB_RED_Pin);
	  } else {
		  char err_msg[60];
		  uint32_t err_code = HAL_I2C_GetError(&hi2c1);
		  int len = sprintf(err_msg, "Timeout/Error! HAL_Err: 0x%08lX, ISR: 0x%08lX\r\n", err_code, I2C1->ISR);
		  HAL_UART_Transmit(&huart2, (uint8_t*)err_msg, len, HAL_MAX_DELAY);
	  }

	  // wait 500 ms before initiating the next request
	  HAL_Delay(500);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
extern UART_HandleTypeDef huart2;

int __io_putchar(int ch) {
	HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
	return ch;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
