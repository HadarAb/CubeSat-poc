/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ObcController.hpp"
#include "PayloadCollector.hpp"
#include "../../../common/log_record.h"
#include "SdLogger.hpp"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticSemaphore_t osStaticMutexDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osMutexId_t i2c_mtxHandle;
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_PayloadCol */
osThreadId_t Task_PayloadColHandle;
const osThreadAttr_t Task_PayloadCol_attributes = {
  .name = "Task_PayloadCol",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_SD_Logger */
osThreadId_t Task_SD_LoggerHandle;
const osThreadAttr_t Task_SD_Logger_attributes = {
  .name = "Task_SD_Logger",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Task_PowerMgmt */
osThreadId_t Task_PowerMgmtHandle;
const osThreadAttr_t Task_PowerMgmt_attributes = {
  .name = "Task_PowerMgmt",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Task_GroundComm */
osThreadId_t Task_GroundCommHandle;
const osThreadAttr_t Task_GroundComm_attributes = {
  .name = "Task_GroundComm",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for q_telemetry */
osMessageQueueId_t q_telemetryHandle;
const osMessageQueueAttr_t q_telemetry_attributes = {
  .name = "q_telemetry"
};
/* Definitions for mtx_spi */
osMutexId_t mtx_spiHandle;
osStaticMutexDef_t mtx_spiControlBlock;
const osMutexAttr_t mtx_spi_attributes = {
  .name = "mtx_spi",
  .cb_mem = &mtx_spiControlBlock,
  .cb_size = sizeof(mtx_spiControlBlock),
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask_PayloadCol(void *argument);
void StartTask_SD_Logger(void *argument);
void StartTask_PowerMgmt(void *argument);
void StartTask_GroundComm(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

	PayloadCollector_Init();

	const osMutexAttr_t i2c_mtx_attributes = {
	  .name = "i2c_mtx",
	  .attr_bits = osMutexPrioInherit,
	};

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of mtx_spi */
  mtx_spiHandle = osMutexNew(&mtx_spi_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */

  i2c_mtxHandle = osMutexNew(&i2c_mtx_attributes);

  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of q_telemetry */
  q_telemetryHandle = osMessageQueueNew (16, sizeof(LogRecord_t), &q_telemetry_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of Task_PayloadCol */
  Task_PayloadColHandle = osThreadNew(StartTask_PayloadCol, NULL, &Task_PayloadCol_attributes);

  /* creation of Task_SD_Logger */
  Task_SD_LoggerHandle = osThreadNew(StartTask_SD_Logger, NULL, &Task_SD_Logger_attributes);

  /* creation of Task_PowerMgmt */
  Task_PowerMgmtHandle = osThreadNew(StartTask_PowerMgmt, NULL, &Task_PowerMgmt_attributes);

  /* creation of Task_GroundComm */
  Task_GroundCommHandle = osThreadNew(StartTask_GroundComm, NULL, &Task_GroundComm_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask_PayloadCol */
/**
* @brief Function implementing the Task_PayloadCol thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_PayloadCol */
void StartTask_PayloadCol(void *argument)
{
  /* USER CODE BEGIN StartTask_PayloadCol */
  /* Infinite loop */
	payload_collector_run();
  /* USER CODE END StartTask_PayloadCol */
}

/* USER CODE BEGIN Header_StartTask_SD_Logger */
/**
* @brief Function implementing the Task_SD_Logger thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_SD_Logger */
void StartTask_SD_Logger(void *argument)
{
  /* USER CODE BEGIN StartTask_SD_Logger */
	SdLogger_Task(argument);
  /* USER CODE END StartTask_SD_Logger */
}

/* USER CODE BEGIN Header_StartTask_PowerMgmt */
/**
* @brief Function implementing the Task_PowerMgmt thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_PowerMgmt */
void StartTask_PowerMgmt(void *argument)
{
  /* USER CODE BEGIN StartTask_PowerMgmt */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartTask_PowerMgmt */
}

/* USER CODE BEGIN Header_StartTask_GroundComm */
/**
* @brief Function implementing the Task_GroundComm thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_GroundComm */
void StartTask_GroundComm(void *argument)
{
  /* USER CODE BEGIN StartTask_GroundComm */
  /* Drains the USART2 RX ring buffer, answers ground-station requests, and checks
     the automatic-status schedule. Before the RTOS this ran from main(), which
     osKernelStart() made unreachable -- without this call the OBC transmits its boot
     banner and then never answers a command. */
  /* Infinite loop */
  for(;;)
  {
    ObcController_Process();
    osDelay(1);
  }
  /* USER CODE END StartTask_GroundComm */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

