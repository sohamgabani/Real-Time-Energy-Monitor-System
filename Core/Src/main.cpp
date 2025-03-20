/*
 * Real-Time Energy Monitoring System for STM32F407VG with SD Card Logging
 * This program reads voltage and current from sensors, calculates power,
 * logs data in real-time via UART, and saves data to an SD card.
 */

#include "stm32f4xx_hal.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>  // For strlen function
#include "fatfs.h"

// Constants for sensor calibration
constexpr float VOLTAGE_CALIBRATION_FACTOR = 0.0107f;  // For 3.3V reference and voltage divider
constexpr float CURRENT_CALIBRATION_FACTOR = 0.0264f;  // For ACS712 30A sensor

// Pin definitions
#define VOLTAGE_SENSOR_PIN GPIO_PIN_0
#define VOLTAGE_SENSOR_PORT GPIOA
#define CURRENT_SENSOR_PIN GPIO_PIN_1
#define CURRENT_SENSOR_PORT GPIOA

class EnergyMonitor {
private:
    FATFS fs;              // File system object
    FIL fil;               // File object
    FRESULT fresult;       // FatFS result code
    ADC_HandleTypeDef hadc1; // ADC handle
    UART_HandleTypeDef huart2; // UART handle
    SPI_HandleTypeDef hspi1;   // SPI handle for SD card

    // Buffer for SD card operations
    char logBuffer[256]{};

public:
    EnergyMonitor() : fs{}, fil{}, fresult{FR_OK}, hadc1{}, huart2{}, hspi1{} {
        // Initialize the HAL Library
        HAL_Init();

        // Configure the system clock
        SystemClock_Config();

        // Initialize peripherals
        initADC();
        initUART();
        initSDCard();
    }

    // Configure system clock for 168MHz operation
    static void SystemClock_Config() {
        RCC_OscInitTypeDef RCC_OscInitStruct = {0};
        RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

        // Configure the main internal regulator output voltage
        __HAL_RCC_PWR_CLK_ENABLE();
        __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

        // Initialize the CPU, AHB and APB buses clocks
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        RCC_OscInitStruct.HSEState = RCC_HSE_ON;
        RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
        RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
        RCC_OscInitStruct.PLL.PLLM = 8;
        RCC_OscInitStruct.PLL.PLLN = 336;
        RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
        RCC_OscInitStruct.PLL.PLLQ = 7;
        HAL_RCC_OscConfig(&RCC_OscInitStruct);

        // Initialize the CPU, AHB and APB buses clocks
        RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                    |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
        RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
        RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
        RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
        RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
        HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
    }

    void initADC() {
        // Enable GPIO and ADC clocks
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_ADC1_CLK_ENABLE();

        // Configure GPIO pins for analog input
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = VOLTAGE_SENSOR_PIN | CURRENT_SENSOR_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        // Configure ADC1
        hadc1.Instance = ADC1;
        hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
        hadc1.Init.Resolution = ADC_RESOLUTION_12B;
        hadc1.Init.ScanConvMode = DISABLE;
        hadc1.Init.ContinuousConvMode = DISABLE;
        hadc1.Init.DiscontinuousConvMode = DISABLE;
        hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
        hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
        hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
        hadc1.Init.NbrOfConversion = 1;
        hadc1.Init.DMAContinuousRequests = DISABLE;
        hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
        HAL_ADC_Init(&hadc1);
    }

    uint16_t readADC(const uint8_t channel) {
        ADC_ChannelConfTypeDef sConfig = {0};

        // Configure the ADC channel
        sConfig.Channel = channel;
        sConfig.Rank = 1;
        sConfig.SamplingTime = ADC_SAMPLETIME_56CYCLES;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);

        // Start ADC conversion
        HAL_ADC_Start(&hadc1);

        // Wait for conversion to complete
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);

        // Read the ADC value
        const uint16_t adc_value = HAL_ADC_GetValue(&hadc1);

        // Stop ADC conversion
        HAL_ADC_Stop(&hadc1);

        return adc_value;
    }

    static float calculateVoltage(const uint16_t adc_value) {
        // Convert ADC value to voltage (12-bit ADC with 3.3V reference)
        // Calibration factor accounts for voltage divider if used
        return static_cast<float>(adc_value) * VOLTAGE_CALIBRATION_FACTOR;
    }

    static float calculateCurrent(uint16_t adc_value) {
        // Convert ADC value to current based on sensor characteristics
        // For ACS712 30A sensor: 0A = 2.5V (approx 2048 ADC value)
        const float voltage = static_cast<float>(adc_value) * 3.3f / 4096.0f;
        const float current = (voltage - 2.5f) / 0.066f; // 66mV/A for 30A sensor
        return current;
    }

    static float calculatePower(const float voltage, const float current) {
        return voltage * current;
    }

    void initUART() {
        // Enable GPIO and UART clocks
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART2_CLK_ENABLE();

        // Configure UART2 pins (PA2 = TX, PA3 = RX)
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        // Configure UART2
        huart2.Instance = USART2;
        huart2.Init.BaudRate = 115200;
        huart2.Init.WordLength = UART_WORDLENGTH_8B;
        huart2.Init.StopBits = UART_STOPBITS_1;
        huart2.Init.Parity = UART_PARITY_NONE;
        huart2.Init.Mode = UART_MODE_TX_RX;
        huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
        huart2.Init.OverSampling = UART_OVERSAMPLING_16;
        HAL_UART_Init(&huart2);
    }

    void sendUART(const std::string& message) {
        HAL_UART_Transmit(&huart2, reinterpret_cast<uint8_t*>(const_cast<char*>(message.c_str())),
                          static_cast<uint16_t>(message.length()), HAL_MAX_DELAY);
    }

    void initSDCard() {
        // Initialize SPI for SD card
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_SPI1_CLK_ENABLE();

        // Configure CS pin (PB6)
        GPIO_InitTypeDef GPIO_InitStruct = {0};
        GPIO_InitStruct.Pin = GPIO_PIN_6;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        // Configure SPI1 pins (PA5=SCK, PA6=MISO, PA7=MOSI)
        GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        // Configure SPI1
        hspi1.Instance = SPI1;
        hspi1.Init.Mode = SPI_MODE_MASTER;
        hspi1.Init.Direction = SPI_DIRECTION_2LINES;
        hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
        hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
        hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
        hspi1.Init.NSS = SPI_NSS_SOFT;
        hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
        hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
        hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
        hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
        hspi1.Init.CRCPolynomial = 10;
        HAL_SPI_Init(&hspi1);

        // Mount SD card
        sendUART("Mounting SD card...\r\n");
        fresult = f_mount(&fs, "", 1);
        if (fresult != FR_OK) {
            sendUART("SD Card Mount Failed! Error code: ");
            sendUART(std::to_string(fresult) + "\r\n");
        } else {
            sendUART("SD Card Mounted Successfully\r\n");

            // Create log file with header if it doesn't exist
            if (f_open(&fil, "log.txt", FA_OPEN_ALWAYS | FA_WRITE) == FR_OK) {
                if (f_size(&fil) == 0) {
                    const char* header = "Time,Voltage(V),Current(A),Power(W)\r\n";
                    UINT bytesWritten;
                    f_write(&fil, header, strlen(header), &bytesWritten);
                }
                f_close(&fil);
            }
        }
    }

    void logDataToSD(const std::string& data) {
        fresult = f_open(&fil, "log.txt", FA_OPEN_ALWAYS | FA_WRITE);
        if (fresult == FR_OK) {
            f_lseek(&fil, f_size(&fil)); // Append to end of file
            UINT bytesWritten;
            f_write(&fil, data.c_str(), data.length(), &bytesWritten);
            f_close(&fil);
        } else {
            sendUART("Failed to open log file! Error code: ");
            sendUART(std::to_string(fresult) + "\r\n");
        }
    }

    static uint32_t getTimestamp() {
        // Return system tick as timestamp (in milliseconds since startup)
        return HAL_GetTick();
    }

    void run() {
        sendUART("Energy Monitor System Started\r\n");
        sendUART("Reading sensors and logging data...\r\n");

        // Main loop - intended to run continuously
        while (true) {
            // Read sensor values
            uint16_t voltage_adc = readADC(ADC_CHANNEL_0); // PA0
            uint16_t current_adc = readADC(ADC_CHANNEL_1); // PA1

            // Calculate measurements
            float voltage = calculateVoltage(voltage_adc);
            float current = calculateCurrent(current_adc);
            float power = calculatePower(voltage, current);
            uint32_t timestamp = getTimestamp();

            // Format data for logging
            std::ostringstream buffer;
            buffer << timestamp << "," << voltage << "," << current << "," << power << "\r\n";
            std::string logEntry = buffer.str();

            // Format data for UART display
            std::ostringstream displayBuffer;
            displayBuffer << "Time: " << timestamp << "ms, Voltage: " << voltage << "V, "
                    << "Current: " << current << "A, Power: " << power << "W\r\n";
            std::string displayEntry = displayBuffer.str();

            // Send data to UART
            sendUART(displayEntry);

            // Log data to SD card
            logDataToSD(logEntry);

            // Wait before next reading (1 second)
            HAL_Delay(1000);
        }
    }
};

int main() {
    EnergyMonitor monitor;
    monitor.run();

    // This line is included for standards compliance, but it's unreachable
    return 0;
}