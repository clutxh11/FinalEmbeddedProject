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
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tcpserver.h"
#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "string.h"
#include "message.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include "lwip/arch.h"
#include "lwip/apps/fs.h"
#include <stdio.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CAN_MAX_FRAME_SIZE 8 // Maximum bytes per CAN message
#define CAN_FIRST_FRAME_SIZE 7 // First frame bytes
#define MAX_MESSAGE_SIZE 32 // Maximum size of the entire message
#define CAN_ID 0x767        // Standard CAN ID
#define BIT_0 (1 << 0)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan1;

osThreadId HTTPHandle;
osThreadId TCPServerHandle;
/* USER CODE BEGIN PV */
BaseType_t RetValTCP;
osThreadId TCPHandle;

EventGroupHandle_t xEventGroup;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN1_Init(void);
void HTTP_Task(void const * argument);
void TCP_Server_Task(void const * argument);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// http
int motor_angle = 45;  // Initial angle of the motor
bool lock_motors = false;  // Lock motors in place
bool fire_laser = false;            // Fire laser
bool motor_mode = false;  // True for scanning and False for manual


// CAN
CAN_TxHeaderTypeDef TxHeader;
CAN_RxHeaderTypeDef RxHeader;

uint8_t TxData[8];
uint8_t RxData[8];

uint32_t TxMailbox[3];

uint8_t fullRxBuffer[MAX_MESSAGE_SIZE];

CombinedData received_combined = CombinedData_init_zero;
CombinedData feedback_combined = CombinedData_init_zero;

uint8_t totalBytesReceived = 0;
size_t expectedMessageSize = 0;

void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    // Get the received message
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO1, &RxHeader, RxData) != HAL_OK) {
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

        lock_motors = received_combined.status.lock_motors;
		fire_laser = received_combined.status.fire_laser;
		motor_mode = received_combined.status.motor_mode;
		motor_angle = received_combined.servo_data.position_1;

        // Reset for next message
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
            Error_Handler();  // Handle transmission error
        }

        frameIndex++;
    }
}

bool http_server(struct netconn *conn)
{
    struct netbuf *inbuf;
    err_t recv_err;
    char* buf;
    u16_t buflen;

    bool statusChanged = false;

    /* Read the data from the port, blocking if nothing yet there */
    recv_err = netconn_recv(conn, &inbuf);

    if (recv_err == ERR_OK)
    {
        if (netconn_err(conn) == ERR_OK)
        {
            /* Get the data pointer and length of the data inside a netbuf */
            netbuf_data(inbuf, (void**)&buf, &buflen);

            /* Check if request to get the index.html */
            if (strncmp((char const *)buf,"GET /index.html",15) == 0)
            {
                struct fs_file file;
                fs_open(&file, "/index.html");
                netconn_write(conn, (const unsigned char*)(file.data), (size_t)file.len, NETCONN_NOCOPY);
                fs_close(&file);
            }
            if (strncmp((char const *)buf, "GET /laser", 10) == 0)
            {
                // Handle laser control
            	fire_laser = !fire_laser;
                const char *response = "Laser toggled";
                netconn_write(conn, response, strlen(response), NETCONN_NOCOPY);

                statusChanged = true;
            }
            if (strncmp((char const *)buf, "GET /motor", 10) == 0)
            {
                // Handle motor control
                lock_motors = !lock_motors;
                const char *response = "Motor locked";
                netconn_write(conn, response, strlen(response), NETCONN_NOCOPY);

                statusChanged = true;
            }
            if (strncmp((char const *)buf, "GET /mode", 9) == 0)
            {
                // Handle mode change
            	motor_mode = !motor_mode;
                const char *response = "Mode changed";
                netconn_write(conn, response, strlen(response), NETCONN_NOCOPY);

                statusChanged = true;
            }
            if (strncmp((char const *)buf, "GET /status", 11) == 0)
            {
                // Return the status in JSON format
                char status_response[200];
                snprintf(status_response, sizeof(status_response),
                    "{\"laser\":\"%s\",\"motor_angle\":\"%d\",\"mode\":\"%s\",\"motor_locked\":\"%s\"}",
                    fire_laser ? "On" : "Off",
                    motor_angle,
                    motor_mode ? "Automatic" : "Manual",
                    lock_motors ? "Locked" : "Unlocked");

                netconn_write(conn, status_response, strlen(status_response), NETCONN_NOCOPY);
            }
            else
            {
                /* Load Error page */
                struct fs_file file;
                fs_open(&file, "/404.html");
                netconn_write(conn, (const unsigned char*)(file.data), (size_t)file.len, NETCONN_NOCOPY);
                fs_close(&file);
            }
        }
    }
    /* Close the connection (server closes in HTTP) */
    netconn_close(conn);

    /* Delete the buffer (netconn_recv gives us ownership,
   so we have to make sure to deallocate the buffer) */
    netbuf_delete(inbuf);

    return statusChanged;
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

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

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
  MX_CAN1_Init();
  /* USER CODE BEGIN 2 */

  HAL_CAN_Start(&hcan1);
  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO1_MSG_PENDING);

  xEventGroup = xEventGroupCreate();

  /* USER CODE END 2 */

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
  /* definition and creation of HTTP */
  osThreadDef(HTTP, HTTP_Task, osPriorityAboveNormal, 0, 512);
  HTTPHandle = osThreadCreate(osThread(HTTP), NULL);

  /* definition and creation of TCPServer */
  osThreadDef(TCPServer, TCP_Server_Task, osPriorityNormal, 0, 512);
  TCPServerHandle = osThreadCreate(osThread(TCPServer), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
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
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
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
  hcan1.Init.Prescaler = 12;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_4TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_4TQ;
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
  canfilterconfig.FilterBank = 10;  // which filter bank to use from the assigned ones
  canfilterconfig.FilterFIFOAssignment = CAN_FILTER_FIFO1;
  canfilterconfig.FilterIdHigh = 0x441<<5;
  canfilterconfig.FilterIdLow = 0;
  canfilterconfig.FilterMaskIdHigh = 0x441<<5;
  canfilterconfig.FilterMaskIdLow = 0x0000;
  canfilterconfig.FilterMode = CAN_FILTERMODE_IDMASK;
  canfilterconfig.FilterScale = CAN_FILTERSCALE_32BIT;
  canfilterconfig.SlaveStartFilterBank = 0;  // how many filters to assign to the CAN1 (master can)

  HAL_CAN_ConfigFilter(&hcan1, &canfilterconfig);

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/* USER CODE BEGIN Header_HTTP_Task */
/**
  * @brief  Function implementing the HTTP thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_HTTP_Task */
void HTTP_Task(void const * argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN 5 */
  struct netconn *conn, *newconn;
  err_t err, accept_err;

  uint8_t buffer[MAX_MESSAGE_SIZE];  // Adjust the buffer size according to your needs
  size_t message_length;

  /* Create a new TCP connection handle */
  conn = netconn_new(NETCONN_TCP);
  if (conn!= NULL)
  {
    /* Bind to port 80 (HTTP) with default IP address */
    err = netconn_bind(conn, IP_ADDR_ANY, 80);

    if (err == ERR_OK)
    {
      /* Put the connection into LISTEN state */
      netconn_listen(conn);

      while(1)
      {
        /* accept any incoming connection */
        accept_err = netconn_accept(conn, &newconn);
        if(accept_err == ERR_OK)
        {
          /* serve connection */
          if (http_server(newconn)) {
        	  xEventGroupSetBits(xEventGroup, BIT_0);

    		  feedback_combined.status.lock_motors = lock_motors;
    		  feedback_combined.status.fire_laser = fire_laser;
    		  feedback_combined.status.motor_mode = motor_mode;  // True for scanning and False for manual
    		  feedback_combined.has_status = true;
    		  feedback_combined.has_servo_data = false;

    		  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    		  bool status = pb_encode(&stream, CombinedData_fields, &feedback_combined);
    		  message_length = stream.bytes_written;

    		  if (!status) {
    			  Error_Handler();  // Handle encoding error
    		  }

    		  // Send the data over CAN
    		  SendCANMessage(buffer, message_length, 0x441);
          }

          /* delete connection */
          netconn_delete(newconn);
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 10 ms delay
      }
    }
  }

  vTaskDelay(pdMS_TO_TICKS(10));
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_TCP_Server_Task */
/**
* @brief Function implementing the TCPServer thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_TCP_Server_Task */
void TCP_Server_Task(void const * argument)
{
  /* USER CODE BEGIN TCP_Server_Task */
    err_t err, accept_err;
    struct netconn *conn, *newconn;

    conn = netconn_new(NETCONN_TCP);

    if (conn != NULL)
    {
        err = netconn_bind(conn, IP_ADDR_ANY, 7); // Port 7
        if (err == ERR_OK)
        {
            netconn_listen(conn);
            while (1)
            {
                accept_err = netconn_accept(conn, &newconn);
                if (accept_err == ERR_OK)
                {
                    while (1) // Keep the connection open to keep sending data
                    {
                        // Serialize the received_combined protobuf message
                        uint8_t buffer[128]; // Buffer size can be adjusted based on your message size
                        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
                        bool status = pb_encode(&stream, CombinedData_fields, &received_combined);

                        if (!status) {
                            // Handle the encoding error if needed
                            continue;
                        }

                        // Send the serialized message to the client
                        netconn_write(newconn, buffer, stream.bytes_written, NETCONN_COPY);

                        // Delay to simulate continuous data sending
                        vTaskDelay(pdMS_TO_TICKS(20));  // 1-second delay
                    }

                    netconn_close(newconn);
                    netconn_delete(newconn);
                }
            }
        }
        else
        {
            netconn_delete(conn);
        }
    }
  /* USER CODE END TCP_Server_Task */
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
