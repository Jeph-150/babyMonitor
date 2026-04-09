/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Baby Monitor - FreeRTOS sensor tasks
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PD */
#define MLX90614_ADDR   (0x5A << 1)
#define MAX30102_ADDR   (0x57 << 1)
#define MPU6050_ADDR    (0x68 << 1)
#define TEMP_LOW        36.0f
#define TEMP_HIGH       37.5f
#define AMB_TEMP_LOW    20.0f
#define AMB_TEMP_HIGH   22.2f
#define NO_MOTION_WARN  (40 * 60 * 1000)

#define SOUND_THRESHOLD   500
#define SOUND_LOUD_MS     1500
#define SOUND_QUIET_MS    3000
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
ADC_HandleTypeDef hadc1;

osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* USER CODE BEGIN PV */
osThreadId_t maxTaskHandle;
osThreadId_t motionTaskHandle;
osThreadId_t uartTxTaskHandle;
osThreadId_t soundTaskHandle;
osThreadId_t mlxTaskHandle;

const osThreadAttr_t maxTask_attr    = { .name = "MAXTask",    .stack_size = 512*4, .priority = osPriorityNormal };
const osThreadAttr_t motionTask_attr = { .name = "MotionTask", .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t uartTxTask_attr = { .name = "UARTTxTask", .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t soundTask_attr  = { .name = "SoundTask",  .stack_size = 256*4, .priority = osPriorityNormal };
const osThreadAttr_t mlxTask_attr    = { .name = "MLXTask",    .stack_size = 256*4, .priority = osPriorityNormal };

volatile float    g_temp     = 0.0f;
volatile float    g_amb_temp = 0.0f;
volatile uint32_t g_bpm      = 0;
volatile float    g_motion   = 0.0f;
volatile uint8_t  g_finger   = 0;
volatile float    g_spo2     = 0.0f;
volatile uint32_t g_sound    = 0;
volatile float    g_max_temp = 0.0f;
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void Task_MAX30102(void *argument);
void Task_MLX90614(void *argument);
void Task_Motion(void *argument);
void Task_UART_TX(void *argument);
void Task_Sound(void *argument);

void UART_Print(const char *msg);
float MLX90614_ReadTemp(void);
float MLX90614_ReadAmbientTemp(void);
void MAX30102_Init(void);
uint32_t MAX30102_ReadIR(void);
uint32_t MAX30102_ReadRed(void);
float MAX30102_ReadTemp(void);
void MPU6050_Init(void);
float MPU6050_GetMotionMagnitude(void);
uint32_t HW484_ReadSound(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
void UART_Print(const char *msg) {
  HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

uint32_t HW484_ReadSound(void) {
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
    uint32_t value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return value;
  }
  HAL_ADC_Stop(&hadc1);
  return 0;
}

float MLX90614_ReadTemp(void) {
  uint8_t buf[3] = {0};
  HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
    &hi2c1, MLX90614_ADDR, 0x07, I2C_MEMADD_SIZE_8BIT, buf, 3, 100);
  if (status != HAL_OK) return -999.0f;
  uint16_t raw = ((uint16_t)buf[1] << 8) | buf[0];
  return raw * 0.02f - 273.15f;
}

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

float MAX30102_ReadTemp(void) {
  MAX30102_WriteReg(0x21, 0x01);
  HAL_Delay(100);
  uint8_t t_int  = 0;
  uint8_t t_frac = 0;
  HAL_I2C_Mem_Read(&hi2c1, MAX30102_ADDR, 0x1F, I2C_MEMADD_SIZE_8BIT, &t_int,  1, 100);
  HAL_I2C_Mem_Read(&hi2c1, MAX30102_ADDR, 0x20, I2C_MEMADD_SIZE_8BIT, &t_frac, 1, 100);
  return (float)((int8_t)t_int) + ((float)t_frac * 0.0625f);
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
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();

  /* USER CODE BEGIN 2 */
  MAX30102_Init();
  MPU6050_Init();
  UART_Print("Baby Monitor Starting...\r\n");
  /* USER CODE END 2 */

  osKernelInitialize();

  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  maxTaskHandle    = osThreadNew(Task_MAX30102, NULL, &maxTask_attr);
  mlxTaskHandle    = osThreadNew(Task_MLX90614, NULL, &mlxTask_attr);
  motionTaskHandle = osThreadNew(Task_Motion,   NULL, &motionTask_attr);
  uartTxTaskHandle = osThreadNew(Task_UART_TX,  NULL, &uartTxTask_attr);
  soundTaskHandle  = osThreadNew(Task_Sound,    NULL, &soundTask_attr);
  /* USER CODE END RTOS_THREADS */

  osKernelStart();

  while (1) {}
}

/* USER CODE BEGIN 4 */

// ── MAX30102 Task — owns the sensor completely ────────────
// Handles HR, SpO2, and die temp all in one task to avoid I2C contention
void Task_MAX30102(void *argument) {
  char msg[64];
  uint32_t prev_ir     = 0;
  uint8_t  rising      = 0;
  uint32_t last_beat_time = 0;
  uint32_t beat_intervals[4] = {0};
  uint8_t  beat_idx    = 0;
  uint8_t  beat_count  = 0;
  float    filtered    = 0;
  float    alpha       = 0.95f;
  uint32_t lastPrint   = 0;
  uint32_t lastSPO2    = 0;
  uint32_t lastTEMP    = 0;

  osDelay(200);

  for (;;) {
    uint32_t now = osKernelGetTickCount();

    // ── IR read for finger detection + HR ──
    uint32_t ir = MAX30102_ReadIR();
    osDelay(15);

    if (ir < 50000) {
      g_finger = 0;
      g_bpm    = 0;
      filtered = 0;
      prev_ir  = 0;
      rising   = 0;
      last_beat_time = 0;
      beat_count     = 0;
      beat_idx       = 0;
      if ((now - lastPrint) >= 2000) {
        UART_Print("[FNG] 0\r\n");
        lastPrint = now;
      }
      osDelay(200);
      continue;
    }

    g_finger  = 1;
    filtered  = alpha * (filtered + (float)ir - (float)prev_ir);
    prev_ir   = ir;

    if (!rising && filtered > 100.0f) {
      rising = 1;
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
    } else if (filtered < -100.0f) {
      rising = 0;
    }

    // ── Print HR every 2s ──
    if ((now - lastPrint) >= 2000) {
      UART_Print("[FNG] 1\r\n");
      if (g_bpm > 0) {
        snprintf(msg, sizeof(msg), "[HR] BPM: %lu\r\n", g_bpm);
        UART_Print(msg);
      }
      lastPrint = now;
    }

    // ── SpO2 every 4s ──
    if ((now - lastSPO2) >= 4000) {
      osDelay(15);
      uint32_t red = MAX30102_ReadRed();
      osDelay(15);
      uint32_t ir2 = MAX30102_ReadIR();
      if (ir2 > 50000) {
        float ratio = (float)red / (float)ir2;
        float spo2  = 100.0f - 5.0f * ratio;
        if (spo2 > 100.0f) spo2 = 100.0f;
        if (spo2 <   0.0f) spo2 =   0.0f;
        g_spo2 = spo2;
        snprintf(msg, sizeof(msg), "[SPO2] %.1f%%\r\n", spo2);
        UART_Print(msg);
      }
      lastSPO2 = now;
    }

    // ── Die temp every 10s ──
    if ((now - lastTEMP) >= 10000) {
      osDelay(15);
      float temp = MAX30102_ReadTemp();
      g_max_temp = temp;
      snprintf(msg, sizeof(msg), "[MAX_TEMP] %.2f C\r\n", temp);
      UART_Print(msg);
      lastTEMP = now;
    }

    osDelay(20);
  }
}

// ── MLX90614 Task — IR body + ambient temp ───────────────
void Task_MLX90614(void *argument) {
  char msg[64];
  osDelay(500);  // stagger from MAX task

  for (;;) {
    float body = MLX90614_ReadTemp();
    float amb  = MLX90614_ReadAmbientTemp();

    if (body > -900.0f) {
      g_temp = body;
      snprintf(msg, sizeof(msg), "[TEMP] Body: %.2f C\r\n", body);
      UART_Print(msg);
    }

    if (amb > -900.0f) {
      g_amb_temp = amb;
      snprintf(msg, sizeof(msg), "[AMB] %.2f C\r\n", amb);
      UART_Print(msg);
    }

    osDelay(4000);  // 4s — temp is slow moving
  }
}

// ── Motion Task — MPU6050 ─────────────────────────────────
void Task_Motion(void *argument) {
  char msg[64];
  uint32_t lastMotionTime = osKernelGetTickCount();
  osDelay(300);

  for (;;) {
    float mag = MPU6050_GetMotionMagnitude();
    g_motion  = mag;
    snprintf(msg, sizeof(msg), "[MOT] %.3f\r\n", mag);
    UART_Print(msg);

    if (mag > 1.05f)
      lastMotionTime = osKernelGetTickCount();

    uint32_t elapsed = osKernelGetTickCount() - lastMotionTime;
    if (elapsed > NO_MOTION_WARN)
      UART_Print("[ALERT] No movement 40+ min!\r\n");
    else if (elapsed > (20 * 60 * 1000))
      UART_Print("[WARN] No movement 20+ min!\r\n");

    osDelay(2000);  // 2s — motion doesn't need to be polled fast
  }
}

// ── Sound Task — HW484 ────────────────────────────────────
void Task_Sound(void *argument) {
  char msg[64];

  typedef enum { SOUND_QUIET, SOUND_DETECTING, SOUND_ALERT } SoundState_t;
  SoundState_t state = SOUND_QUIET;

  uint32_t loud_onset  = 0;
  uint32_t quiet_onset = 0;
  uint32_t last_alert  = 0;
  uint32_t last_ok     = 0;

  osDelay(500);

  for (;;) {
    uint32_t adc_val = HW484_ReadSound();
    g_sound = adc_val;

    uint8_t  is_loud = (adc_val >= SOUND_THRESHOLD);
    uint32_t now     = osKernelGetTickCount();

    switch (state) {
      case SOUND_QUIET:
        if (is_loud) {
          loud_onset = now;
          state = SOUND_DETECTING;
        } else if ((now - last_ok) >= 10000) {
          snprintf(msg, sizeof(msg), "[SOUND] OK, ADC:%lu\r\n", adc_val);
          UART_Print(msg);
          last_ok = now;
        }
        break;

      case SOUND_DETECTING:
        if (!is_loud) {
          state = SOUND_QUIET;
        } else if ((now - loud_onset) >= SOUND_LOUD_MS) {
          state = SOUND_ALERT;
          snprintf(msg, sizeof(msg), "[ALERT] Crying! ADC:%lu\r\n", adc_val);
          UART_Print(msg);
          last_alert = now;
        }
        break;

      case SOUND_ALERT:
        if (!is_loud) {
          if (quiet_onset == 0) quiet_onset = now;
          else if ((now - quiet_onset) >= SOUND_QUIET_MS) {
            state       = SOUND_QUIET;
            quiet_onset = 0;
            UART_Print("[SOUND] Quiet again\r\n");
            last_ok = now;
          }
        } else {
          quiet_onset = 0;
          if ((now - last_alert) >= 5000) {
            snprintf(msg, sizeof(msg), "[ALERT] Still crying! ADC:%lu\r\n", adc_val);
            UART_Print(msg);
            last_alert = now;
          }
        }
        break;
    }

    osDelay(50);
  }
}

// ── UART TX Task — sends to ESP32 via USART1 ─────────────
void Task_UART_TX(void *argument) {
  char msg[128];
  for (;;) {
    snprintf(msg, sizeof(msg),
      "TEMP:%.2f,BPM:%lu,MOT:%.3f,FNG:%d,AMB:%.2f,SPO2:%.1f,SND:%lu\r\n",
      g_temp, g_bpm, g_motion, g_finger, g_amb_temp, g_spo2, g_sound);
    HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    osDelay(1000);
  }
}
/* USER CODE END 4 */

void StartDefaultTask(void *argument) {
  for(;;) { osDelay(1); }
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {}
}

void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM            = 16;
  RCC_OscInitStruct.PLL.PLLN            = 336;
  RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ            = 2;
  RCC_OscInitStruct.PLL.PLLR            = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

static void MX_I2C1_Init(void) {
  hi2c1.Instance             = I2C1;
  hi2c1.Init.ClockSpeed      = 100000;
  hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1     = 0;
  hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2     = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_USART1_UART_Init(void) {
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void) {
  huart2.Instance          = USART2;
  huart2.Init.BaudRate     = 115200;
  huart2.Init.WordLength   = UART_WORDLENGTH_8B;
  huart2.Init.StopBits     = UART_STOPBITS_1;
  huart2.Init.Parity       = UART_PARITY_NONE;
  huart2.Init.Mode         = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_ADC1_Init(void) {
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode          = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  sConfig.Channel      = ADC_CHANNEL_1;
  sConfig.Rank         = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin  = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin   = LD2_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
