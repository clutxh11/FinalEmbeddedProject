/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include "message.pb.h"  // Include your Protocol Buffers header
#include <pb_encode.h>   // Include Nanopb encode header
#include <pb_decode.h>   // Include Nanopb decode header
#include "string.h"
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// PWM pulse width limits for 9g servo motor
#define PWM_MIN 30.0f   // Minimum pulse width (CCR value for 0°)
#define PWM_MAX 75.0f   // Maximum pulse width (CCR value for 180°)

#define THRESHOLD_DISTANCE 4 // Ultra sound triggering distance

#define CAN_MAX_FRAME_SIZE 8 // Maximum bytes per CAN message
#define CAN_FIRST_FRAME_SIZE 7 // First frame bytes
#define MAX_MESSAGE_SIZE 32 // Maximum size of the entire message
#define CAN_ID_STATUS 0x441       // Standard CAN for sending status
#define CAN_ID_DATA 0x442       // Standard CAN for sending data
#define CAN_ID_DISP 0x144       // Standard CAN for sending status to display

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;
DMA_HandleTypeDef hdma_adc2;

CAN_HandleTypeDef hcan1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart2;

osThreadId CommandProcessHandle;
/* USER CODE BEGIN PV */

BaseType_t RetValJCT;
osThreadId Task2Handle;

BaseType_t RetValUST;
osThreadId Task3Handle;

BaseType_t RetValLCT;
osThreadId Task4Handle;

BaseType_t RetValSMT;
osThreadId Task5Handle;

BaseType_t RetValCAN;
osThreadId TaskCANHandle;

BaseType_t RetValCANSTATUS;
osThreadId TaskCanStatusHandle;

SemaphoreHandle_t xLaserSemaphore;
SemaphoreHandle_t xManualModeSemaphore;
SemaphoreHandle_t xScanModeSemaphore;
SemaphoreHandle_t xCanTransmitSemaphore;

uint16_t pulseX = 75;


CAN_TxHeaderTypeDef TxHeader;
CAN_RxHeaderTypeDef RxHeader;

uint8_t TxData[8];
uint8_t RxData[8];

uint32_t TxMailbox[3];

uint8_t fullRxBuffer[MAX_MESSAGE_SIZE] = {1};

uint32_t error;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM1_Init(void);
static void MX_CAN1_Init(void);
static void MX_USART2_UART_Init(void);
void CommandProcessorTask(void const * argument);

/* USER CODE BEGIN PFP */
void JoystickControlTask(void * argument);
void UltrasonicSensorTask(void * argument);
void LaserControlTask(void * argument);
void ScanningModeTask(void * argument);
void CANCommunicationTask(void *argument);
void CANStatusCommTask(void *argument);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
bool lock_motors = false;  // Lock motors in place
bool fire_laser = false;            // Fire laser
bool motor_mode = false;  // True for scanning and False for manual


uint32_t IC_Val1 = 0;
uint32_t IC_Val2 = 0;
uint32_t Difference = 0;
uint8_t Is_First_Captured = 0;  // is the first value captured ?
uint8_t Distance  = 100;

CombinedData received_combined = CombinedData_init_zero;
CombinedData feedback_combined = CombinedData_init_zero;
ServoUltrasonicData feedback_data = ServoUltrasonicData_init_zero;

uint8_t totalBytesReceived = 0;
size_t expectedMessageSize = 0;

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)  // if the interrupt source is channel1
	{
		if (Is_First_Captured==0) // if the first value is not captured
		{
			IC_Val1 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1); // read the first value
			Is_First_Captured = 1;  // set the first captured as true
			// Now change the polarity to falling edge
			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_FALLING);
		}

		else if (Is_First_Captured==1)   // if the first is already captured
		{
			IC_Val2 = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);  // read second value
			__HAL_TIM_SET_COUNTER(htim, 0);  // reset the counter

			if (IC_Val2 > IC_Val1)
			{
				Difference = IC_Val2-IC_Val1;
			}

			else if (IC_Val1 > IC_Val2)
			{
				Difference = (0xffff - IC_Val1) + IC_Val2;
			}

			Distance = Difference * .034/2;
			Is_First_Captured = 0; // set it back to false

			// set polarity to rising edge
			__HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
			__HAL_TIM_DISABLE_IT(&htim1, TIM_IT_CC1);
		}
	}
}

// Callback for CAN receive
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    // Get the received message
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK) {
        Error_Handler();  // Handle reception error
    }

    if (totalBytesReceived == 0) {
        // The first byte of the first frame tells us the total message length
        expectedMessageSize = RxData[0];
        // Copy the remaining 7 bytes to the buffer
        memcpy(&fullRxBuffer[totalBytesReceived], &RxData[1], CAN_FIRST_FRAME_SIZE);
        totalBytesReceived += CAN_FIRST_FRAME_SIZE;
    } else {
        // Copy the full data as usual
        size_t datalength = RxHeader.DLC;
        memcpy(&fullRxBuffer[totalBytesReceived], RxData, datalength);
        totalBytesReceived += datalength;
    }

    // Check if the full message has been received
    if (totalBytesReceived >= expectedMessageSize) {
        // Decode the received message from the complete buffer
        pb_istream_t stream = pb_istream_from_buffer(fullRxBuffer, totalBytesReceived);
        bool status = pb_decode(&stream, CombinedData_fields, &received_combined);

        if (!status) {
//            Error_Handler();  // Handle decoding error
        }

        // Reset for the next message
        totalBytesReceived = 0;
        expectedMessageSize = 0;
    }
}

void SendCANMessage(uint8_t* buffer, size_t message_length, uint32_t ide) {
    size_t bytesSent = 0;
    uint8_t frameIndex = 0; // To keep track of the frames

    while (bytesSent < message_length) {
        uint8_t TxData[8] = {0}; // Initialize with zeros

        if (frameIndex == 0) {
            // First frame: include the message length in the first byte
            TxData[0] = (uint8_t)message_length; // Store the total message length in the first byte
            size_t bytesToSend = (message_length < CAN_FIRST_FRAME_SIZE) ? message_length : CAN_FIRST_FRAME_SIZE;
            memcpy(&TxData[1], &buffer[bytesSent], bytesToSend);
            TxHeader.DLC = bytesToSend + 1; // DLC is the number of bytes in this frame
            bytesSent += bytesToSend;
        } else {
            // Subsequent frames
            size_t bytesToSend = (message_length - bytesSent > CAN_MAX_FRAME_SIZE) ? CAN_MAX_FRAME_SIZE : (message_length - bytesSent);
            memcpy(TxData, &buffer[bytesSent], bytesToSend);
            TxHeader.DLC = bytesToSend; // DLC is the number of bytes in this frame
            bytesSent += bytesToSend;
        }

        TxHeader.IDE = CAN_ID_STD;
        TxHeader.RTR = CAN_RTR_DATA;
        TxHeader.StdId = ide;

        HAL_StatusTypeDef status = HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox[frameIndex]);
        if (status != HAL_OK) {
//            Error_Handler();  // Handle transmission error
        }

        frameIndex++;
    }
}


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
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_TIM1_Init();
  MX_CAN1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1); //start pwm signal motor1
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2); //start pwm signal motor1

  HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_1);

  HAL_CAN_Start(&hcan1);
  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  xLaserSemaphore = xSemaphoreCreateBinary();
  assert_param(xLaserSemaphore != NULL);

  xManualModeSemaphore = xSemaphoreCreateBinary();
  assert_param(xManualModeSemaphore != NULL);

  xScanModeSemaphore = xSemaphoreCreateBinary();
  assert_param(xScanModeSemaphore != NULL);

  xCanTransmitSemaphore = xSemaphoreCreateBinary();
  assert_param(xCanTransmitSemaphore != NULL);
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of CommandProcess */
  osThreadDef(CommandProcess, CommandProcessorTask, osPriorityHigh, 0, 128);
  CommandProcessHandle = osThreadCreate(osThread(CommandProcess), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */

  RetValJCT = xTaskCreate(JoystickControlTask, "JoystickControlTask", 128, NULL, PRIORITY_JOYSTICK_CONTROL, &Task2Handle);
  assert_param(RetValJCT != NULL);

  RetValUST = xTaskCreate(UltrasonicSensorTask, "UltrasonicSensorTask", 128, NULL, PRIORITY_ULTRASONIC_SENSOR, &Task3Handle);
  assert_param(RetValUST != NULL);

  RetValLCT = xTaskCreate(LaserControlTask, "LaserControlTask", 128, NULL, PRIORITY_LASER_CONTROL, &Task4Handle);
  assert_param(RetValLCT != NULL);

  RetValSMT = xTaskCreate(ScanningModeTask, "ScanningModeTask", 128, NULL, PRIORITY_SCANNING_MODE, &Task5Handle);
  assert_param(RetValSMT != NULL);

  RetValCAN = xTaskCreate(CANCommunicationTask, "CANCommTask", 256, NULL, osPriorityNormal, &TaskCANHandle);
  assert_param(RetValCAN != NULL);

  RetValCANSTATUS = xTaskCreate(CANStatusCommTask, "CANStatusCommTask", 256, NULL, osPriorityNormal, &TaskCanStatusHandle);
  assert_param(RetValCANSTATUS != NULL);

  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 15;
  RCC_OscInitStruct.PLL.PLLN = 216;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.ScanConvMode = DISABLE;
  hadc2.Init.ContinuousConvMode = ENABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 18;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_2TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  CAN_FilterTypeDef canfilterconfig;

  canfilterconfig.FilterActivation = CAN_FILTER_ENABLE;
  canfilterconfig.FilterBank = 18;  // Choose a filter bank
  canfilterconfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  canfilterconfig.FilterIdHigh = 0x441 << 5; // Example ID, adjust as needed
  canfilterconfig.FilterIdLow = 0;
  canfilterconfig.FilterMaskIdHigh = 0x441 << 5;
  canfilterconfig.FilterMaskIdLow = 0;
  canfilterconfig.FilterMode = CAN_FILTERMODE_IDMASK;
  canfilterconfig.FilterScale = CAN_FILTERSCALE_32BIT;
  canfilterconfig.SlaveStartFilterBank = 20;

  HAL_CAN_ConfigFilter(&hcan1, &canfilterconfig);

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 180-1;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_IC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  if (HAL_TIM_IC_ConfigChannel(&htim1, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 900-1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 1800-1;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 1000;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, testLED1_Pin|testLED2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Trig_GPIO_Port, Trig_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(laser_GPIO_Port, laser_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : testLED1_Pin testLED2_Pin */
  GPIO_InitStruct.Pin = testLED1_Pin|testLED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : modeMotor_Pin */
  GPIO_InitStruct.Pin = modeMotor_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(modeMotor_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : Trig_Pin */
  GPIO_InitStruct.Pin = Trig_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Trig_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : laserTrig_Pin */
  GPIO_InitStruct.Pin = laserTrig_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(laserTrig_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : laser_Pin */
  GPIO_InitStruct.Pin = laser_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(laser_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

void JoystickControlTask(void * argument) {
	uint16_t readValueX;
	uint16_t readValueY;

	uint16_t pulseX;
	uint16_t pulseY;

    while(1) {
    	if (xSemaphoreTake(xManualModeSemaphore, portMAX_DELAY)) {
			// Read joystick x-axis
			HAL_ADC_Start(&hadc1);
			HAL_ADC_PollForConversion(&hadc1,1);
			readValueX = HAL_ADC_GetValue(&hadc1);
			HAL_ADC_Stop(&hadc1);

			// Read joystick y-axis
			HAL_ADC_Start(&hadc2);
			HAL_ADC_PollForConversion(&hadc2,1);
			readValueY = HAL_ADC_GetValue(&hadc2);
			HAL_ADC_Stop(&hadc2);

			// Perform the calculation to get ccr value for desired pwm
			pulseX = PWM_MIN + (uint16_t)(((float)readValueX / 4095.0) * (PWM_MAX - PWM_MIN));
			htim3.Instance->CCR1 = pulseX;

			pulseY = PWM_MIN + (uint16_t)(((float)readValueY / 4095.0) * (PWM_MAX - PWM_MIN));
			htim4.Instance->CCR1 = pulseY;

			xSemaphoreGive(xManualModeSemaphore);
			vTaskDelay(pdMS_TO_TICKS(100));  // Delay to allow other tasks to run
    	}
    }
}

void UltrasonicSensorTask(void * argument) {

    while(1) {
    	HAL_GPIO_WritePin(Trig_GPIO_Port, Trig_Pin, GPIO_PIN_SET);  // pull the TRIG pin HIGH
    	vTaskDelay(pdMS_TO_TICKS(10));  // wait for 10 ms
		HAL_GPIO_WritePin(Trig_GPIO_Port, Trig_Pin, GPIO_PIN_RESET);  // pull the TRIG pin low

		__HAL_TIM_ENABLE_IT(&htim1, TIM_IT_CC1);

        if (Distance < THRESHOLD_DISTANCE) {
        	// Notify CommandProcessorTask that object is close
        	xTaskNotify(CommandProcessHandle, (1 << 0), eSetBits); // Use bit 0 to indicate UltrasonicSensorTask
        }
        else if (Distance > THRESHOLD_DISTANCE) {
        	// Notify LaserControlTask that object is far
        	xTaskNotify(CommandProcessHandle, (1 << 1), eSetBits); // Use bit 0 to indicate LaserControlTask
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void LaserControlTask(void * argument) {

    while(1) {

        if (xSemaphoreTake(xLaserSemaphore, 0) == pdTRUE) {
            // If semaphore is successfully taken, turn on the laser
            HAL_GPIO_WritePin(laser_GPIO_Port, laser_Pin, GPIO_PIN_SET);
            xSemaphoreGive(xLaserSemaphore);
        } else {
            // If semaphore is not available, turn off the laser
            HAL_GPIO_WritePin(laser_GPIO_Port, laser_Pin, GPIO_PIN_RESET);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ScanningModeTask(void * argument) {
    bool increasing = true;  // Flag to indicate direction of movement

    for (;;) {
            while (1) {
                // Check if laser mode is active and should stop scanning
                if (xSemaphoreTake(xScanModeSemaphore, 0) == pdFALSE) {
                    // Laser mode active, stop scanning
                    break;
                }

                // Update PWM for motor X
                htim3.Instance->CCR1 = pulseX;

                // Move in the current direction
                if (increasing) {
                    pulseX++;
                    if (pulseX > PWM_MAX) {
                        pulseX = PWM_MAX;  // Cap pulseX at 100
                        increasing = false;  // Change direction to decreasing
                    }
                } else {
                    pulseX--;
                    if (pulseX < PWM_MIN) {
                        pulseX = PWM_MIN;  // Cap pulseX at 50
                        increasing = true;  // Change direction to increasing
                    }
                }

                xSemaphoreGive(xScanModeSemaphore);

                // Delay for smooth movement
                vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}


void CANCommunicationTask(void *argument) {
    uint8_t buffer[MAX_MESSAGE_SIZE];  // Adjust the buffer size according to your needs
    size_t message_length;

	for (;;)
	{
		if (xSemaphoreTake(xCanTransmitSemaphore, 0) == pdTRUE) {
			feedback_data.position_1 = ((float)(htim3.Instance->CCR1 - PWM_MIN) / (PWM_MAX - PWM_MIN)) * 180.0f;  //  Calculating motor 1 angle from ccr value
			feedback_data.position_2 = ((float)(htim4.Instance->CCR1 - PWM_MIN) / (PWM_MAX - PWM_MIN)) * 180.0f;  //  Calculating motor 2 angle from ccr value
			feedback_data.distance = (float)(Distance); // UltraSound distance

		    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
		    bool status = pb_encode(&stream, ServoUltrasonicData_fields, &feedback_data);
		    message_length = stream.bytes_written;

		    if (!status) {
		        Error_Handler();  // Handle encoding error
		    }

			/// Send the data
		    SendCANMessage(buffer, message_length, CAN_ID_DISP);
		    xSemaphoreGive(xCanTransmitSemaphore);

			vTaskDelay(pdMS_TO_TICKS(150));
		}
	}
}

void CANStatusCommTask(void *argument) {
    uint8_t buffer[MAX_MESSAGE_SIZE];  // Adjust the buffer size according to your needs
    size_t message_length;

	for (;;)
	{
		feedback_combined.status.lock_motors = lock_motors;
		feedback_combined.status.fire_laser = fire_laser;
		feedback_combined.status.motor_mode = motor_mode;  // True for scanning and False for manual
		feedback_combined.servo_data.distance = (float)(Distance); // UltraSound distance
		feedback_combined.servo_data.position_1 = ((float)(htim3.Instance->CCR1 - PWM_MIN) / (PWM_MAX - PWM_MIN)) * 180.0f;  //  Calculating motor 1 angle from ccr value
		feedback_combined.servo_data.position_2 = ((float)(htim4.Instance->CCR1 - PWM_MIN) / (PWM_MAX - PWM_MIN)) * 180.0f;  //  Calculating motor 2 angle from ccr value

		feedback_combined.has_servo_data = true;
		feedback_combined.has_status = true;

	    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
	    bool status = pb_encode(&stream, CombinedData_fields, &feedback_combined);
	    message_length = stream.bytes_written;

	    if (!status) {
	        Error_Handler();  // Handle encoding error
	    }

		/// Send the data
	    SendCANMessage(buffer, message_length, CAN_ID_STATUS);

		vTaskDelay(pdMS_TO_TICKS(20));
	}
}
    /* USER CODE END 5 */



/* USER CODE END 4 */

/* USER CODE BEGIN Header_CommandProcessorTask */
/**
  * @brief  Function implementing the CommandProcess thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_CommandProcessorTask */
void CommandProcessorTask(void const * argument)
{
  /* USER CODE BEGIN 5 */
	uint32_t ulNotificationValue;
	bool ultraTrig = false;
  /* Infinite loop */
	for(;;){
		lock_motors = received_combined.status.lock_motors || !HAL_GPIO_ReadPin(laserTrig_GPIO_Port, laserTrig_Pin);
		fire_laser = received_combined.status.fire_laser || !HAL_GPIO_ReadPin(laserTrig_GPIO_Port, laserTrig_Pin) || ultraTrig;
		motor_mode = received_combined.status.motor_mode;

		if (xTaskNotifyWait(0, 0xFFFFFFFF, &ulNotificationValue, 0) == pdTRUE) {

			if (ulNotificationValue & (1 << 0)) {
				ultraTrig = true; // Ultrasound noti, object close
			}
			else if (ulNotificationValue & (1 << 1)) {
				ultraTrig = false; // Ultrasound noti, object far
			}
		}


		if (fire_laser)
		{
			xSemaphoreGive(xLaserSemaphore);

			if (lock_motors) {
				xSemaphoreGive(xCanTransmitSemaphore);
			}
		} else {
			xSemaphoreTake(xLaserSemaphore, 0);
			xSemaphoreTake(xCanTransmitSemaphore, 0);
		}

		if (lock_motors) {
			xSemaphoreTake(xManualModeSemaphore, 0);
			xSemaphoreTake(xScanModeSemaphore, 0);
		}

		if (motor_mode && !lock_motors) // IF BUTTON IS BEING PRESSED, IT IS IN SCANNING MODE
		{
			HAL_GPIO_WritePin(testLED1_GPIO_Port, testLED1_Pin, GPIO_PIN_SET);
			xSemaphoreTake(xManualModeSemaphore, 0);
			xSemaphoreGive(xScanModeSemaphore);
		} else if (!motor_mode && !lock_motors) {
			xSemaphoreTake(xScanModeSemaphore, 0);
			xSemaphoreGive(xManualModeSemaphore);
			HAL_GPIO_WritePin(testLED1_GPIO_Port, testLED1_Pin, GPIO_PIN_RESET);
		}

		vTaskDelay(pdMS_TO_TICKS(60));
	}
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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

#ifdef  USE_FULL_ASSERT
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
