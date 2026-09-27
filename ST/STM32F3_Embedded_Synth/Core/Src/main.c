/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "math.h"
#include "adsr.h"
#include "audio_config.h"
#include "midi_mapping.h"
#include "oscillator.h"
#include "oscillator_tables.h"
#include "svf_filter.h"

//MIDI
#define MIDI_NOTE_ON  0x90
#define MIDI_NOTE_OFF 0x80
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_BUF_LEN 64

uint8_t midi_ring_buffer[MIDI_BUF_LEN];
volatile uint8_t midi_read_idx = 0, midi_write_idx = 0;

uint8_t rx_byte;

uint8_t status;
uint8_t note;
uint8_t velocity;
uint8_t active_note = 0xFFU;
uint8_t held_notes[128];
uint32_t note_press_order[128];
uint32_t note_order_counter = 0;
// Ultimo valore ricevuto per ciascun MIDI Control Change (CC 0-127).
// Questi valori verranno successivamente associati a filtro e ADSR.
volatile uint8_t midi_cc_values[128];
volatile uint8_t last_cc_number = 0;
volatile uint8_t last_cc_value = 0;

float midi_freq = 0;
volatile uint8_t sound_enabled = 0;

typedef struct {
    uint32_t note_on_count;
    uint32_t note_off_count;
    uint32_t control_change_count;
    uint32_t dropped_bytes;
} MIDI_Diagnostics;

typedef struct {
    uint32_t error_count;
    uint32_t overrun_count;
} UART_Diagnostics;

typedef struct {
    uint8_t active_note;
    uint8_t state;
    float level;
} ADSR_Diagnostics;

typedef struct {
    uint32_t clipping_count;
    uint32_t non_finite_count;
    float cutoff_hz;
    float q;
} Filter_Diagnostics;

typedef struct {
    uint32_t last_cycles;
    uint32_t max_cycles;
    uint32_t deadline_cycles;
    uint32_t deadline_miss_count;
} Audio_Diagnostics;

typedef struct {
    uint8_t mipmapped_enabled;
    uint8_t interpolation_enabled;
    uint8_t active_mip_level;
    uint8_t waveform;
    uint16_t max_harmonic;
    uint32_t phase_increment;
    float frequency_hz;
} Oscillator_Diagnostics;

typedef struct {
    MIDI_Diagnostics midi;
    UART_Diagnostics uart;
    ADSR_Diagnostics adsr;
    Filter_Diagnostics filter;
    Audio_Diagnostics audio;
    Oscillator_Diagnostics oscillator;
} DebugStatus;

#ifdef DEBUG
volatile DebugStatus debug_status = {
    .adsr.active_note = 0xFFU
};
#define DEBUG_INCREMENT(field) (debug_status.field++)
#define DEBUG_SET(field, value) (debug_status.field = (value))
#else
#define DEBUG_INCREMENT(field) ((void)0)
#define DEBUG_SET(field, value) ((void)0)
#endif

//DAC
#define I2S_BUFFER_SIZE 512  // numero di campioni stereo (L+R = 2 x mono)

extern I2S_HandleTypeDef hi2s2;

int16_t i2s_buffer[I2S_BUFFER_SIZE * 2]; // stereo


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
I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_tx;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USB_PCD_Init(void);
static void MX_I2S2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void SetMIDINoteFrequency(uint8_t midi_note) {
    midi_freq = 440.0f * powf(2.0f, ((float)midi_note - 69.0f) / 12.0f);
    SetFrequency(midi_freq);
}

static uint8_t FindMostRecentHeldNote(void) {
    uint8_t selected_note = 0xFFU;
    uint32_t newest_order = 0U;

    for (uint16_t candidate = 0; candidate < 128U; candidate++) {
        if (held_notes[candidate] != 0U &&
            note_press_order[candidate] >= newest_order) {
            selected_note = (uint8_t)candidate;
            newest_order = note_press_order[candidate];
        }
    }

    return selected_note;
}

static void HandleMIDINoteOn(uint8_t midi_note) {
    held_notes[midi_note] = 1U;
    note_press_order[midi_note] = ++note_order_counter;
    active_note = midi_note;
    DEBUG_SET(adsr.active_note, active_note);

    SetMIDINoteFrequency(midi_note);
    sound_enabled = 1U;
    ADSR_NoteOn();
}

static void HandleMIDINoteOff(uint8_t midi_note) {
    uint8_t fallback_note;

    held_notes[midi_note] = 0U;

    if (midi_note != active_note) {
        return;
    }

    fallback_note = FindMostRecentHeldNote();

    if (fallback_note != 0xFFU) {
        // PrioritÃ  all'ultima nota ancora premuta, senza riattivare l'inviluppo.
        active_note = fallback_note;
        DEBUG_SET(adsr.active_note, active_note);
        SetMIDINoteFrequency(fallback_note);
    } else {
        active_note = 0xFFU;
        DEBUG_SET(adsr.active_note, active_note);
        ADSR_NoteOff();
    }
}

//buffer audio
void FillI2SBuffer(int16_t *buffer, uint16_t length) {
    #ifdef DEBUG
    uint32_t start_cycles = DWT->CYCCNT;
#endif
    //smoothing filter parameters
    filter_cutoff_hz +=
        0.2f * (filter_cutoff_target_hz - filter_cutoff_hz);

    filter_q +=
        0.2f * (filter_q_target - filter_q);

    SVF_SetParameters(
        &lowpass_filter,
        filter_cutoff_hz,
        filter_q,
        (float)SAMPLE_RATE
    );

    float filter_gain = 1.0f;

    if (filter_gain_compensation_enabled) {
        if (filter_q > 1.0f) {
            filter_gain = 1.0f / sqrtf(filter_q);
        }
    }
        
    for (uint16_t i = 0; i < length; i += 2) {

    	if(sound_enabled==1){


        float oscillator_sample = Oscillator_ProcessSample();
        float envelope = ADSR_ProcessSample();
        
        // Oscillator sample is already normalized to the range -1.0 to 1.0.
        float input_sample = oscillator_sample * envelope;

        // Apply low-pass filter

        float lowpass_sample = SVF_ProcessLowpass(&lowpass_filter, input_sample*filter_gain);

        float filtered_sample;

        if (filter_enabled == 1) {
            filtered_sample = lowpass_sample;
        } else {
            filtered_sample = input_sample;
        }

        //diagnostic: check for non-finite values
        if (!isfinite(filtered_sample)) {
        DEBUG_INCREMENT(filter.non_finite_count);

        filtered_sample = 0.0f;
        SVF_Reset(&lowpass_filter);
        }


        //saturation
        if (filtered_sample > 1.0f) {
            DEBUG_INCREMENT(filter.clipping_count);
            filtered_sample = 1.0f;
        } else if (filtered_sample < -1.0f) {
            DEBUG_INCREMENT(filter.clipping_count);
            filtered_sample = -1.0f;
        }

        //convert to pcm 16 bit format
        int16_t output_sample =(int16_t)(filtered_sample * 32767.0f);

        //output mono sample
        buffer[i] = output_sample;     // Left
        buffer[i + 1] = output_sample; // Right
    	}else{
    		buffer[i] = 0;     // Left
    		buffer[i + 1] = 0;
    	}
    }

    if (sound_enabled == 0U) {
    SVF_Reset(&lowpass_filter);
    }

    DEBUG_SET(adsr.state, (uint8_t)adsr_state);
    DEBUG_SET(adsr.level, adsr_level);
    DEBUG_SET(filter.cutoff_hz, filter_cutoff_hz);
    DEBUG_SET(filter.q, filter_q);
    DEBUG_SET(oscillator.mipmapped_enabled, oscillator_mipmapped_enabled);
    DEBUG_SET(oscillator.interpolation_enabled, wavetable_interpolation_enabled);
    DEBUG_SET(oscillator.active_mip_level, active_mip_level);
    DEBUG_SET(oscillator.waveform, wave);
    DEBUG_SET(oscillator.max_harmonic,
              oscillator_mip_max_harmonic[active_mip_level]);
    DEBUG_SET(oscillator.phase_increment, phase_increment);
    DEBUG_SET(oscillator.frequency_hz, current_frequency);

    #ifdef DEBUG
        uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;
        uint32_t audio_frames = length / 2U;

        uint32_t deadline_cycles =
            (uint32_t)(((uint64_t)SystemCoreClock * audio_frames)
                    / SAMPLE_RATE);

        debug_status.audio.last_cycles = elapsed_cycles;
        debug_status.audio.deadline_cycles = deadline_cycles;

        if (elapsed_cycles > debug_status.audio.max_cycles) {
            debug_status.audio.max_cycles = elapsed_cycles;
        }

        if (elapsed_cycles > deadline_cycles) {
            debug_status.audio.deadline_miss_count++;
        }
    #endif


}

//DMA

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
	//scrivo nella prima metÃ 
    FillI2SBuffer(i2s_buffer, I2S_BUFFER_SIZE);
}


void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
	//scrivo nella seconda metÃ 
    FillI2SBuffer(&i2s_buffer[I2S_BUFFER_SIZE], I2S_BUFFER_SIZE);
}

//inizializzazione
void StartDDSOutput(void) {
    GenerateSampleTable();
    SetFrequency(current_frequency);
    FillI2SBuffer(i2s_buffer, I2S_BUFFER_SIZE * 2);
    HAL_I2S_Transmit_DMA(&hi2s2,(uint16_t *) i2s_buffer, I2S_BUFFER_SIZE * 2);
}

//ricezione USART1
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	uint8_t next_write_idx = (uint8_t)((midi_write_idx + 1U) % MIDI_BUF_LEN);

	if (next_write_idx != midi_read_idx) {
		midi_ring_buffer[midi_write_idx] = rx_byte;
		midi_write_idx = next_write_idx;
	} else {
		// Buffer pieno: non sovrascrivere byte MIDI non ancora elaborati.
		DEBUG_INCREMENT(midi.dropped_bytes);
	}

	HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
	}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == USART1) {
		DEBUG_INCREMENT(uart.error_count);

		if ((huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U) {
			DEBUG_INCREMENT(uart.overrun_count);
			__HAL_UART_CLEAR_OREFLAG(huart);
		}

		// Riavvia sempre la ricezione dopo un errore, evitando che una nota
		// resti attiva perchÃ© il relativo Note Off non viene piÃ¹ ricevuto.
		HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
	}
}

void ProcessMIDI(void) {
    static uint8_t running_status = 0;
    static uint8_t midi_data[2];
    static uint8_t data_index = 0;

    uint8_t byte = midi_ring_buffer[midi_read_idx];
    midi_read_idx = (uint8_t)((midi_read_idx + 1U) % MIDI_BUF_LEN);

    if ((byte & 0x80U) != 0U) {
        // I messaggi channel voice (0x80-0xEF) possono usare il running status.
        if (byte < 0xF0U) {
            running_status = byte;
            data_index = 0;
        } else {
            // I messaggi system non sono usati dal sintetizzatore.
            running_status = 0;
            data_index = 0;
        }
        return;
    }

    if (running_status == 0U) {
        return;
    }

    midi_data[data_index++] = byte;

    if (data_index == 2U) {
        uint8_t message_type = running_status & 0xF0U;
        uint8_t data_1 = midi_data[0];
        uint8_t data_2 = midi_data[1];
        data_index = 0;

        status = running_status;

        if (message_type == MIDI_CONTROL_CHANGE) {
            DEBUG_INCREMENT(midi.control_change_count);
            last_cc_number = data_1;
            last_cc_value = data_2;
            midi_cc_values[data_1] = data_2;
            MIDI_ApplyControlChange(data_1, data_2);
        } else {
            note = data_1;
            velocity = data_2;

            if (message_type == MIDI_NOTE_ON && velocity > 0U) {
                DEBUG_INCREMENT(midi.note_on_count);
                HandleMIDINoteOn(note);
            } else if (message_type == MIDI_NOTE_OFF ||
                       (message_type == MIDI_NOTE_ON && velocity == 0U)) {
                DEBUG_INCREMENT(midi.note_off_count);
                HandleMIDINoteOff(note);
            }
    }
}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	wave = (wave + 1) % 3;
	GenerateSampleTable();
	switch(wave){
	case 0: HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_SET); HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET); break;
	case 1: HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET); HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_RESET); break;
	case 2: HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_SET); HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET); break;
	default: break;
	}

}
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
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USB_PCD_Init();
  MX_I2S2_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_SET);
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  SVF_Reset(&lowpass_filter);
  SVF_SetParameters(&lowpass_filter, filter_cutoff_hz, filter_q, (float)SAMPLE_RATE);

  #ifdef DEBUG
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  #endif

  StartDDSOutput();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if(midi_read_idx != midi_write_idx) {
		  ProcessMIDI();
	  }
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_USART1
                              |RCC_PERIPHCLK_I2C1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.USBClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00201D2B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2S2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S2_Init(void)
{

  /* USER CODE BEGIN I2S2_Init 0 */

  /* USER CODE END I2S2_Init 0 */

  /* USER CODE BEGIN I2S2_Init 1 */

  /* USER CODE END I2S2_Init 1 */
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_SYSCLK;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S2_Init 2 */

  /* USER CODE END I2S2_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */

  /* USER CODE END USB_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DRDY_Pin MEMS_INT3_Pin MEMS_INT4_Pin MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = DRDY_Pin|MEMS_INT3_Pin|MEMS_INT4_Pin|MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : CS_I2C_SPI_Pin LD4_Pin LD3_Pin LD5_Pin
                           LD7_Pin LD9_Pin LD10_Pin LD8_Pin
                           LD6_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
