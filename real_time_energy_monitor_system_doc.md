Real-Time Energy Monitor System

1. Introduction

The Real-Time Energy Monitor System is designed for the STM32F407VG microcontroller. It measures voltage and current from external sensors, calculates real-time power consumption, and logs the data via UART and an SD card using the FATFS file system.

2. Features

- Real-time monitoring:** Reads voltage and current using ADC.
- Power calculation:** Computes power consumption using calibrated sensor values.
- Data logging:** Logs sensor readings to an SD card and transmits via UART.
- Embedded C++ Implementation:** Uses STM32 HAL library and object-oriented design.

3. System Architecture

3.1 Hardware Components

- STM32F407VG**: Cortex-M4 microcontroller
- Voltage Sensor**: Connected to ADC (PA0)
- Current Sensor (ACS712)**: Connected to ADC (PA1)
- SD Card Module**: Uses SPI for storage
- UART (USART2)**: Serial communication for data output

3.2 Software Components

- STM32 HAL Drivers:** Manages peripherals (ADC, UART, SPI)
- FatFS Library:** Handles SD card operations
- CMake Build System:** For compilation and linking
- CLion IDE:** Used for development

4. Implementation Details

4.1 EnergyMonitor Class

This class encapsulates ADC reading, power calculation, and data logging functionalities.

4.1.1 Class Members:

In main cpp file
class EnergyMonitor {
private:
    FATFS fs;              // File system object
    FIL fil;               // File object
    FRESULT fresult;       // FatFS result code
    ADC_HandleTypeDef hadc1; // ADC handle
    UART_HandleTypeDef huart2; // UART handle
public:
    void init();             // Initialize peripherals
    float readVoltage();     // Read ADC voltage
    float readCurrent();     // Read ADC current
    void logData();          // Log data to SD card
};


4.2 Data Logging to SD Card

Data is stored in CSV format:

Timestamp, Voltage (V), Current (A), Power (W)
2025-03-20 12:00:01, 230.5, 5.2, 1197.6


4.3 UART Communication

Data is sent to the serial monitor in a structured format.

5. Setup & Execution

1. Flash Firmware**: Use STM32CubeProgrammer to upload the compiled binary.
2. Connect Sensors & SD Card**: Ensure correct wiring.
3. Open UART Terminal**: Monitor live energy data.

6. Future Enhancements

- Implement real-time wireless data transmission.
- Add over-current and voltage protection.
- Develop a GUI for graphical data representation.

7. Conclusion

This project provides an efficient way to monitor energy consumption in real time. It is scalable for industrial applications, integrating various communication protocols and data processing techniques.


