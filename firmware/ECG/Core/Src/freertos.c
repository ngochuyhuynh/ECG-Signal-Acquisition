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
#include "adc.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
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
/* USER CODE BEGIN Variables */
uint16_t ECG_Buffer[256];
uint8_t ECG_Half = 0; // 0  => First half of the buffer is filled, 1 => Second half of the buffer is filled
char tx_buf[100]; // Buffer for UART transmission
/* USER CODE END Variables */
/* Definitions for ECG_Task */
osThreadId_t ECG_TaskHandle;
const osThreadAttr_t ECG_Task_attributes = {
  .name = "ECG_Task",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
float notch_filter(float x);
float hpf_filter(float x);
float lpf_filter(float x);
/* USER CODE END FunctionPrototypes */

void ECG_Task_func(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of ECG_Task */
  ECG_TaskHandle = osThreadNew(ECG_Task_func, NULL, &ECG_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_ECG_Task_func */
/**
  * @brief  Function implementing the ECG_Task thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_ECG_Task_func */
void ECG_Task_func(void *argument)
{
  /* USER CODE BEGIN ECG_Task_func */
  /* Infinite loop */
  uint16_t start_index;
  char tx_buf[10];

  for(;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    start_index = (ECG_Half == 0) ? 0 : 128;

    for(int i = start_index; i < start_index + 128; i++)
    {
      float sample = (float)ECG_Buffer[i]; // Convert ADC value to float for filtering
      float hp = hpf_filter(sample); // Apply the high-pass filter
      float filtered_sample = notch_filter(hp); // Apply the notch filter to the high-pass filtered signal
      float final_sample = lpf_filter(filtered_sample) + 2048.0f; // Apply the low-pass filter to the notch filtered signal
      uint16_t output_value = (uint16_t)fmaxf(0.0f, fminf(4095.0f, final_sample)); // Convert back to uint16_t for transmission
      sprintf(tx_buf, "%u\r\n", output_value); // Format the output value as a string
      HAL_UART_Transmit(&huart2, (uint8_t*)tx_buf, strlen(tx_buf), HAL_MAX_DELAY);
    }
  }
  /* USER CODE END ECG_Task_func */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
  if(hadc->Instance == ADC1)
  {
    ECG_Half = 0; // First half of the buffer is filled
    BaseType_t xHigherPriorityTaskWoken = pdFALSE; // Variable to check if a context switch is needed
    vTaskNotifyGiveFromISR(ECG_TaskHandle, &xHigherPriorityTaskWoken); // Notify the ECG_Task that the first half of the buffer is filled
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // Request a context switch if needed
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  if(hadc->Instance == ADC1)
  {
    ECG_Half = 1; // Second half of the buffer is filled
    BaseType_t xHigherPriorityTaskWoken = pdFALSE; // Variable to check if a context switch is needed
    vTaskNotifyGiveFromISR(ECG_TaskHandle, &xHigherPriorityTaskWoken); // Notify the ECG_Task that the second half of the buffer is filled
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // Request a context switch if needed
  }
}

/*Notch Filter function
Input function for the notch filter : y[n] = b[0]*x[n] - b[1]*x[n-1] + b[2]*x[n-2] - a[1]*y[n-1] - a[2]*y[n-2]*/
float notch_filter(float x)
{
  // Coefficients for a 60 Hz notch filter with a sampling frequency of 1000 Hz, BW = 20 Hz
  static const float b0 = 1.0000f;
  static const float b1 = -1.8596f;
  static const float b2 = 1.0000f;
  static const float a1 = -1.7428f;
  static const float a2 = 0.8783f;

  // Static variables to hold previous input and output values
  static float x1 = 0.0f, x2 = 0.0f; // Previous two input samples
  static float y1 = 0.0f, y2 = 0.0f; // Previous two output samples

  // Compute the output value
  float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

  // Update the previous values
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = y;

  return y;
}
/* Digital HPF
Cut-off Frequency at .5Hz
y[n] = a* (y[n-1] + x[n] - x[n-1])*/
float hpf_filter(float x)
{
  static const float a = 0.9969f; // Coefficient for the high-pass filter
  static float x_prev = 0.0f; // Previous input sample
  static float y_prev = 0.0f; // Previous output sample

  // Compute the output value
  float y = a * (y_prev + x - x_prev);

  // Update the previous values
  x_prev = x;
  y_prev = y;

  return y;
}

/* Digital LPF 
Cut off frequency at 40Hz, fs = 1000hz
y[n] = a * x[n] + (1 - a) * y[n-1]*/
float lpf_filter(float x)
{
  static const float a = 0.2010f; // Coefficient for the low-pass filter
  static float y_prev = 0.0f; // Previous output sample

  // Compute the output value
  float y = a * x + (1 - a) * y_prev;

  // Update the previous value
  y_prev = y;

  return y;
}

/* USER CODE END Application */

