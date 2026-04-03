/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Baby Monitor - FreeRTOS sensor tasks
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MLX90614_ADDR   (0x5A << 1)
#define MAX30102_ADDR   (0x57 << 1)
#define MPU6050_ADDR    (0x68 << 1)
#define TEMP_LOW        36.0f
#define TEMP_HIGH       37.5f
#define AMB_TEMP_LOW    20.0f
#define AMB_TEMP_HIGH   22.2f
#define NO_MOTION_WARN  (40 * 60 * 1000)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* USER CODE BEGIN PV */
osThreadId_t tempTaskHandle;
osThreadId_t hrTaskHandle;
osThreadId_t motionTaskHandle;
osThreadId_t uartTxTaskHandle;
osThreadId_t spo2TaskHandle;

const osThreadAttr_t tempTask_attr   = { .name = "TempTask",   .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t hrTask_attr     = { .name = "HRTask",     .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t motionTask_attr = { .name = "MotionTask", .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t uartTxTask_attr = { .name = "UARTTxTask", .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t spo2Task_attr   = { .name = "SpO2Task",   .stack_size = 256*4, .priority = osPriorityNormal };

// Shared sensor data
volatile float    g_temp     = 0.0f;
volatile float    g_amb_temp = 0.0f;
volatile uint32_t g_bpm      = 0;
volatile float    g_motion   = 0.0f;
volatile uint8_t  g_finger   = 0;
volatile float    g_spo2     = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void Task_Temperature(void *argument);
void Task_HeartRate(void *argument);
void Task_Motion(void *argument);
void Task_UART_TX(void *argument);
void Task_SpO2(void *argument);
void UART_Print(const char *msg);
float MLX90614_ReadTemp(void);
float MLX90614_ReadAmbientTemp(void);
void MAX30102_Init(void);
uint32_t MAX30102_ReadIR(void);
uint32_t MAX30102_ReadRed(void);
void MPU6050_Init(void);
float MPU6050_GetMotionMagnitude(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void UART_Print(const char *msg) {
  HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

// Body object temperature (register 0x07)
float MLX90614_ReadTemp(void) {
  uint8_t buf[3] = {0};
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
    &hi2c1, MLX90614_ADDR, 0x07, I2C_MEMADD_SIZE_8BIT, buf, 3, 100);
  if (status != HAL_OK) return -999.0f;
  uint16_t raw = ((uint16_t)buf[1] << 8) | buf[0];
  return raw * 0.02f - 273.15f;
}

// Ambient temperature (register 0x06)
float MLX90614_ReadAmbientTemp(void) {
  uint8_t buf[3] = {0};
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
    &hi2c1, MLX90614_ADDR, 0x06, I2C_MEMADD_SIZE_8BIT, buf, 3, 100);
  if (status != HAL_OK) return -999.0f;
  uint16_t raw = ((uint16_t)buf[1] << 8) | buf[0];
  return raw * 0.02f - 273.15f;
}

void MAX30102_WriteReg(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  HAL_I2C_Master_Transmit(&hi2c1, MAX30102_ADDR, buf, 2, HAL_MAX_DELAY);
}

void MAX30102_Init(void) {
  MAX30102_WriteReg(0x09, 0x40);
  HAL_Delay(100);
  MAX30102_WriteReg(0x04, 0x00);
  MAX30102_WriteReg(0x05, 0x00);
  MAX30102_WriteReg(0x06, 0x00);
  MAX30102_WriteReg(0x09, 0x03);
  MAX30102_WriteReg(0x0A, 0x27);
  MAX30102_WriteReg(0x0C, 0x3F);
  MAX30102_WriteReg(0x0D, 0x3F);
}

uint32_t MAX30102_ReadIR(void) {
  uint8_t buf[6] = {0};
  HAL_I2C_Mem_Read(&hi2c1, MAX30102_ADDR, 0x07, I2C_MEMADD_SIZE_8BIT, buf, 6, 100);
  uint32_t ir = ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5];
  return ir & 0x3FFFF;
}


uint32_t MAX30102_ReadRed(void) {
  uint8_t buf[6] = {0};
  HAL_I2C_Mem_Read(&hi2c1, MAX30102_ADDR, 0x07, I2C_MEMADD_SIZE_8BIT, buf, 6, 100);
  uint32_t red = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
  return red & 0x3FFFF;
}


void MPU6050_Init(void) {
  uint8_t buf[2] = {0x6B, 0x00};
  HAL_I2C_Master_Transmit(&hi2c1, MPU6050_ADDR, buf, 2, HAL_MAX_DELAY);
}

float MPU6050_GetMotionMagnitude(void) {
  uint8_t buf[6] = {0};
  HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, 0x3B, I2C_MEMADD_SIZE_8BIT, buf, 6, 100);
  int16_t ax = (buf[0] << 8) | buf[1];
  int16_t ay = (buf[2] << 8) | buf[3];
  int16_t az = (buf[4] << 8) | buf[5];
  float ax_g = ax / 16384.0f;
  float ay_g = ay / 16384.0f;
  float az_g = az / 16384.0f;
  return (ax_g * ax_g + ay_g * ay_g + az_g * az_g);
}
/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */
  MAX30102_Init();
  MPU6050_Init();
  UART_Print("Baby Monitor Starting...\r\n");
  /* USER CODE END 2 */

  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* USER CODE END RTOS_QUEUES */

  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  tempTaskHandle   = osThreadNew(Task_Temperature, NULL, &tempTask_attr);
  hrTaskHandle     = osThreadNew(Task_HeartRate,   NULL, &hrTask_attr);
  motionTaskHandle = osThreadNew(Task_Motion,      NULL, &motionTask_attr);
  uartTxTaskHandle = osThreadNew(Task_UART_TX,     NULL, &uartTxTask_attr);
  spo2TaskHandle   = osThreadNew(Task_SpO2,        NULL, &spo2Task_attr);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* USER CODE END RTOS_EVENTS */

  osKernelStart();

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

// ── Temperature Task ──────────────────────────────────────
void Task_Temperature(void *argument) {
  char msg[64];
  for (;;) {
    float temp = MLX90614_ReadTemp();
    float amb  = MLX90614_ReadAmbientTemp();

    if (temp < -900.0f) {
      UART_Print("[TEMP] I2C Error\r\n");
    } else {
      g_temp = temp;
      snprintf(msg, sizeof(msg), "[TEMP] Body: %.2f C\r\n", temp);
      UART_Print(msg);
      if (temp < TEMP_LOW)
        UART_Print("[ALERT] Body temp too low!\r\n");
      else if (temp > TEMP_HIGH)
        UART_Print("[ALERT] Body temp too high! Fan needed!\r\n");
    }

    if (amb > -900.0f) {
      g_amb_temp = amb;
      snprintf(msg, sizeof(msg), "[AMB] Ambient: %.2f C\r\n", amb);
      UART_Print(msg);
      if (amb < AMB_TEMP_LOW)
        UART_Print("[ALERT] Ambient too cold! Turn on heater!\r\n");
      else if (amb > AMB_TEMP_HIGH)
        UART_Print("[ALERT] Ambient too hot! Turn on fan!\r\n");
    }

    osDelay(2000);
  }
}

// ── Heart Rate Task ───────────────────────────────────────
void Task_HeartRate(void *argument) {
  char msg[64];
  uint32_t prev_ir = 0;
  uint8_t rising = 0;
  uint32_t last_beat_time = 0;
  uint32_t beat_intervals[4] = {0};
  uint8_t beat_idx = 0;
  uint8_t beat_count = 0;
  float filtered = 0;
  float alpha = 0.95f;
  uint32_t lastPrint = 0;

  for (;;) {
    uint32_t ir = MAX30102_ReadIR();

    if (ir < 5000 && filtered == 0) {

    	// Add right after g_finger = 1;
    		UART_Print("[HR] No finger\r\n");
    		g_finger = 0;
      g_bpm = 0;
      filtered = 0;
      prev_ir = 0;
      rising = 0;
      last_beat_time = 0;
      beat_count = 0;
      beat_idx = 0;
      osDelay(100);
      continue;
    }

    g_finger = 1;
    filtered = alpha * (filtered + (float)ir - (float)prev_ir);
    prev_ir = ir;

    if (!rising && filtered > 200.0f) {
      rising = 1;
      uint32_t now = osKernelGetTickCount();
      if (last_beat_time > 0) {
        uint32_t interval = now - last_beat_time;
        if (interval > 300 && interval < 2000) {
          beat_intervals[beat_idx % 4] = interval;
          beat_idx++;
          beat_count++;
          if (beat_count >= 4) {
            uint32_t avg = (beat_intervals[0] + beat_intervals[1] +
                           beat_intervals[2] + beat_intervals[3]) / 4;
            g_bpm = 60000 / avg;
          }
        }
      }
      last_beat_time = now;
    } else if (filtered < -200.0f) {
      rising = 0;
    }

    uint32_t now2 = osKernelGetTickCount();
    if (now2 - lastPrint >= 2000) {
      if (g_finger && g_bpm > 0) {
        snprintf(msg, sizeof(msg), "[HR] BPM: %lu\r\n", g_bpm);
        UART_Print(msg);
        if (g_bpm < 120 || g_bpm > 180)
          UART_Print("[ALERT] Heart rate out of range!\r\n");
      }
      lastPrint = now2;
    }

    osDelay(10);
  }
}

// ── SpO2 Task ─────────────────────────────────────────────
void Task_SpO2(void *argument) {
  char msg[64];
  for (;;) {
    if (g_finger) {
      uint32_t red = MAX30102_ReadRed();
      uint32_t ir  = MAX30102_ReadIR();
      if (ir > 0) {
        float ratio = (float)red / (float)ir;
        float spo2 = 100.0f - 5.0f * ratio;
        if (spo2 > 100.0f) spo2 = 100.0f;
        if (spo2 <   0.0f) spo2 =   0.0f;
        g_spo2 = spo2;
        snprintf(msg, sizeof(msg), "[SPO2] %.1f%%\r\n", spo2);
        UART_Print(msg);
        if (spo2 < 95.0f)
          UART_Print("[ALERT] Low oxygen saturation!\r\n");
      }
    }
    osDelay(2000);
  }
}

// ── Motion Task ───────────────────────────────────────────
void Task_Motion(void *argument) {
  char msg[64];
  uint32_t lastMotionTime = osKernelGetTickCount();
  for (;;) {
    float mag = MPU6050_GetMotionMagnitude();
    g_motion = mag;
    snprintf(msg, sizeof(msg), "[MOTION] Mag: %.3f\r\n", mag);
    UART_Print(msg);
    if (mag > 1.05f)
      lastMotionTime = osKernelGetTickCount();
    uint32_t elapsed = osKernelGetTickCount() - lastMotionTime;
    if (elapsed > NO_MOTION_WARN)
      UART_Print("[ALERT] No movement for 40+ min!\r\n");
    else if (elapsed > (20 * 60 * 1000))
      UART_Print("[WARN] No movement for 20+ min!\r\n");
    osDelay(500);
  }
}

// ── UART TX Task (sends data to ESP32 via USART1) ─────────
void Task_UART_TX(void *argument) {
  char msg[128];
  for (;;) {
    snprintf(msg, sizeof(msg),
      "TEMP:%.2f,BPM:%lu,MOT:%.3f,FNG:%d,AMB:%.2f,SPO2:%.1f\r\n",
      g_temp, g_bpm, g_motion, g_finger, g_amb_temp, g_spo2);
    HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    osDelay(1000);
  }
}
/* USER CODE END 4 */

void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    Error_Handler();

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    Error_Handler();
}

static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
    Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
    Error_Handler();
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
