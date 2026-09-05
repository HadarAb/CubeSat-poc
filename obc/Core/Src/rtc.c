/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
#include "rtc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_MAGIC_TIME_VALID) {
   /* Clock already running from a previous boot - do NOT re-init the calendar. */
   goto skip_calendar_init;
  }

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  skip_calendar_init:
  ;

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
// Save the reset reason in DR3 and increase the persistent boot counter in DR1.
void RTC_record_boot(uint32_t reset_flags)
{
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, reset_flags & RTC_RESET_FLAGS_MASK);

	const uint32_t current_boot_count = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, current_boot_count + 1u);
}

uint32_t RTC_get_boot_count(void)
{
	return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
}

uint32_t RTC_get_reset_flags(void)
{
	return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
}

// DR2 remembers the last epoch received from the ground station.
void RTC_set_last_epoch(uint32_t epoch)
{
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, epoch);
}

static uint8_t RTC_is_leap_year(uint32_t year)
{
	return ((year % 4u) == 0u) && (((year % 100u) != 0u) || ((year % 400u) == 0u));
}

static uint32_t RTC_days_in_year(uint32_t year)
{
	if (RTC_is_leap_year(year)) {
		return 366u;
	}

	return 365u;
}

static uint8_t RTC_days_in_month(uint32_t year, uint32_t month)
{
	static const uint8_t days_per_month[12] = {
		31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u
	};
	if ((month == 0u) || (month > 12u)) {
		return 0u;
	}

	if ((month == 2u) && RTC_is_leap_year(year)) {
		return 29u;
	}

	return days_per_month[month - 1u];
}

uint8_t RTC_set_epoch(uint32_t epoch)
{
	const uint32_t minimum_epoch = 946684800u;
	const uint32_t maximum_epoch = 4102444799u;
	if ((epoch < minimum_epoch) || (epoch > maximum_epoch)) {
		return 0u;
	}

	// Convert whole days since 1970 into the RTC year, month and day fields.
	const uint32_t epoch_days = epoch / 86400u;
	uint32_t remaining_days = epoch_days;
	uint32_t year = 1970u;
	uint32_t days_in_year = RTC_days_in_year(year);
	while (remaining_days >= days_in_year) {
		remaining_days -= days_in_year;
		++year;
		days_in_year = RTC_days_in_year(year);
	}

	uint32_t month = 1u;
	while (remaining_days >= RTC_days_in_month(year, month)) {
		remaining_days -= RTC_days_in_month(year, month);
		++month;
	}

	const uint32_t seconds_today = epoch % 86400u;
	RTC_TimeTypeDef time = {0};
	RTC_DateTypeDef date = {0};
	time.Hours = (uint8_t)(seconds_today / 3600u);
	time.Minutes = (uint8_t)((seconds_today % 3600u) / 60u);
	time.Seconds = (uint8_t)(seconds_today % 60u);
	time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
	time.StoreOperation = RTC_STOREOPERATION_RESET;
	date.WeekDay = (uint8_t)(((epoch_days + 3u) % 7u) + 1u);
	date.Month = (uint8_t)month;
	date.Date = (uint8_t)(remaining_days + 1u);
	date.Year = (uint8_t)(year - 2000u);

	// Mark time invalid until both hardware fields were written successfully.
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0u);
	if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK) {
		return 0u;
	}

	if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
		return 0u;
	}

	RTC_set_last_epoch(epoch);
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_MAGIC_TIME_VALID);
	return 1u;
}

uint8_t RTC_time_is_valid(void)
{
	return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_MAGIC_TIME_VALID;
}

uint32_t RTC_get_epoch(void)
{
	RTC_TimeTypeDef sTime = {0};
	RTC_DateTypeDef sDate = {0};

	// HAL requires GetTime before GetDate to unlock the shadow registers.
	if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
		return 0u;
	}

	if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
		return 0u;
	}

	const uint32_t year = 2000u + sDate.Year;
	const uint8_t days_in_current_month = RTC_days_in_month(year, sDate.Month);
	if ((days_in_current_month == 0u) || (sDate.Date == 0u) ||
			(sDate.Date > days_in_current_month)) {
		return 0u;
	}

	// Count days since 1970 without mktime(), which depends on the PC timezone.
	uint32_t days = 0u;
	for (uint32_t current_year = 1970u; current_year < year; ++current_year) {
		days += RTC_days_in_year(current_year);
	}

	for (uint32_t current_month = 1u; current_month < sDate.Month; ++current_month) {
		days += RTC_days_in_month(year, current_month);
	}

	days += sDate.Date - 1u;
	const uint32_t hours_in_seconds = (uint32_t)sTime.Hours * 3600u;
	const uint32_t minutes_in_seconds = (uint32_t)sTime.Minutes * 60u;
	const uint32_t seconds_today = hours_in_seconds + minutes_in_seconds + sTime.Seconds;
	return (days * 86400u) + seconds_today;
}

/* USER CODE END 1 */
