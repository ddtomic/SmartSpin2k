/*
 * Main.cpp - SmartSpin2K Project Main File
 * This file contains the main setup and loop functions for the SmartSpin2K device, integrating various features and functionalities.
 * Detailed comments have been added to enhance understandability and maintainability for new contributors.
 */

// Include headers and libraries
#include "Main.h"  // Main definitions and configurations
#include "SS2KLog.h"  // Logging utility
#include <TMCStepper.h>  // For stepper motor control
#include <Arduino.h>  // Core Arduino functionality
#include <LittleFS.h>  // Filesystem for storing configurations
#include <HardwareSerial.h>  // Serial communication
#include "FastAccelStepper.h"  // Advanced stepper control
#include "ERG_Mode.h"  // ERG mode functionality
#include "UdpAppender.h"  // UDP logging
#include "WebsocketAppender.h"  // WebSocket logging
#include <Constants.h>  // Project-specific constants

// Define hardware serial ports for stepper motor and auxiliary devices
HardwareSerial stepperSerial(2);  // Stepper motor serial
TMC2208Stepper driver(&stepperSerial, R_SENSE);  // Stepper motor driver initialization

HardwareSerial auxSerial(1);  // Auxiliary serial port (e.g., for Peloton bike communication)
AuxSerialBuffer auxSerialBuffer;  // Buffer for auxiliary serial data

// Stepper motor control setup
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;  // Pointer to stepper motor

// Task handles for asynchronous operations
TaskHandle_t moveStepperTask;  // Task for moving stepper motor
TaskHandle_t maintenanceLoopTask;  // Task for maintenance operations

// Board configuration and selection
Boards boards;  // Available boards
Board currentBoard;  // Currently selected board

// Configuration initialization
SS2K *ss2k = new SS2K;  // Main SS2K object
userParameters *userConfig = new userParameters;  // User-specific parameters
RuntimeParameters *rtConfig = new RuntimeParameters;  // Runtime parameters
physicalWorkingCapacity *userPWC = new physicalWorkingCapacity;  // Physical working capacity parameters

// Logging appenders setup
UdpAppender udpAppender;  // UDP logging appender
WebSocketAppender webSocketAppender;  // WebSocket logging appender
///////////// Log Appender /////////////
UdpAppender udpAppender;
WebSocketAppender webSocketAppender;

///////////// BEGIN SETUP /////////////
#ifndef UNIT_TEST

void SS2K::startTasks() {
  SS2K_LOG(MAIN_LOG_TAG, "Start BLE + ERG Tasks");
  spinBLEClient.intentionalDisconnect = 0;
  if (BLECommunicationTask == NULL) {
    setupBLE();
  }
  if (ErgTask == NULL) {
    setupERG();
  }
}

void SS2K::stopTasks() {
  SS2K_LOG(BLE_CLIENT_LOG_TAG, "Shutting Down all BLE services");
  spinBLEClient.reconnectTries        = 0;
  spinBLEClient.intentionalDisconnect = NUM_BLE_DEVICES;
  if (NimBLEDevice::getInitialized()) {
    NimBLEDevice::deinit();
    ss2k->stopTasks();
  }
  SS2K_LOG(MAIN_LOG_TAG, "Stop BLE + ERG Tasks");
  if (BLECommunicationTask != NULL) {
    vTaskDelete(BLECommunicationTask);
    BLECommunicationTask = NULL;
  }
  if (ErgTask != NULL) {
    vTaskDelete(ErgTask);
    ErgTask = NULL;
  }
  if (BLEClientTask != NULL) {
// BEGIN SETUP
// This section contains the initial setup code executed once at startup.
// It initializes serial ports, configures board-specific settings, and prepares the system for operation.

#ifndef UNIT_TEST  // Exclude this section from unit tests

void setup() {
  // Initialize debugging serial port
  Serial.begin(512000);
  SS2K_LOG(MAIN_LOG_TAG, "Compiled %s%s", __DATE__, __TIME__);

  // Determine the current board based on voltage measurement
  pinMode(REV_PIN, INPUT);
  int actualVoltage = analogRead(REV_PIN);
  if (actualVoltage - boards.rev1.versionVoltage >= boards.rev2.versionVoltage - actualVoltage) {
    currentBoard = boards.rev2;
  } else {
    currentBoard = boards.rev1;
  }
  SS2K_LOG(MAIN_LOG_TAG, "Current Board Revision is: %s", currentBoard.name);

  // Initialize stepper and auxiliary serial ports
  stepperSerial.begin(57600, SERIAL_8N2, currentBoard.stepperSerialRxPin, currentBoard.stepperSerialTxPin);
  if (currentBoard.auxSerialTxPin) {
    auxSerial.begin(19200, SERIAL_8N1, currentBoard.auxSerialRxPin, currentBoard.auxSerialTxPin, false);
    if (!auxSerial) {
      SS2K_LOG(MAIN_LOG_TAG, "Invalid Serial Pin Configuration");
    }
    auxSerial.onReceive(SS2K::rxSerial, false);  // Setup callback for receiving data
  }

  // Filesystem initialization and configuration loading
  SS2K_LOG(MAIN_LOG_TAG, "Mounting Filesystem");
  if (!LittleFS.begin(false)) {
    // Handle filesystem mount failure
    FSUpgrader upgrade;
    SS2K_LOG(MAIN_LOG_TAG, "An Error has occurred while mounting LittleFS.");
    upgrade.upgradeFS();  // Attempt filesystem upgrade if necessary
  }
  // Load and print user configuration
  userConfig->loadFromLittleFS();
  userConfig->printFile();
  userConfig->saveToLittleFS();

  // Load and manage PWC data
  userPWC->loadFromLittleFS();
  userPWC->printFile();
  userPWC->saveToLittleFS();

  // Firmware update check and initialization of web server
  startWifi();
  httpServer.FirmwareUpdate();

  // Configure board-specific inputs and outputs
  pinMode(currentBoard.shiftUpPin, INPUT_PULLUP);
  pinMode(currentBoard.shiftDownPin, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(currentBoard.enablePin, OUTPUT);
  pinMode(currentBoard.dirPin, OUTPUT);
  pinMode(currentBoard.stepPin, OUTPUT);
  digitalWrite(currentBoard.enablePin, HIGH);  // Disables FETs, effectively disabling the motor
  digitalWrite(currentBoard.dirPin, LOW);  // Set motor direction
  digitalWrite(currentBoard.stepPin, LOW);  // Initialize stepper motor step pin
  digitalWrite(LED_PIN, LOW);  // Initialize system LED

  // Stepper driver setup and task creation for stepper and maintenance operations
  ss2k->setupTMCStepperDriver();  // Initialize TMC stepper driver
  ss2k->startTasks();  // Start background tasks for BLE and ERG mode
  httpServer.start();  // Start web server for remote management

  ss2k->resetIfShiftersHeld();  // Check and handle shifter button reset
  attachInterrupt(digitalPinToInterrupt(currentBoard.shiftUpPin), ss2k->shiftUp, CHANGE);  // Setup interrupt for shift up
  attachInterrupt(digitalPinToInterrupt(currentBoard.shiftDownPin), ss2k->shiftDown, CHANGE);  // Setup interrupt for shift down
  digitalWrite(LED_PIN, HIGH);  // Indicate system readiness

  // Finalize setup by creating tasks for stepper movement and maintenance
  xTaskCreatePinnedToCore(SS2K::moveStepper, "moveStepperFunction", STEPPER_STACK, NULL, 18, &moveStepperTask, 0);  // Task for controlling stepper motor movement
  xTaskCreatePinnedToCore(SS2K::maintenanceLoop, "maintenanceLoopFunction", MAIN_STACK, NULL, 20, &maintenanceLoopTask, 1);  // Task for performing maintenance operations
}

#else
// Additional code or configurations for unit testing
#endif

void loop() {
  // Main program loop, typically empty in ESP32 projects where tasks are used instead.
  vTaskDelete(NULL);  // Delete this task to free up memory, as it's not used.
}
}

void loop() {  // Delete this task so we can make one that's more memory efficient.
  vTaskDelete(NULL);
}

void SS2K::maintenanceLoop(void *pvParameters) {
  static int loopCounter              = 0;
  static unsigned long intervalTimer  = millis();
  static unsigned long intervalTimer2 = millis();
  static unsigned long rebootTimer    = millis();
  static bool isScanning              = false;

  while (true) {
    vTaskDelay(73 / portTICK_RATE_MS);
    ss2k->FTMSModeShiftModifier();

    if (currentBoard.auxSerialTxPin) {
// Begin Maintenance Loop
// This function is called periodically and is responsible for managing various tasks such as Bluetooth communication, saving configurations, and handling system reboots.

void SS2K::maintenanceLoop(void *pvParameters) {
  static int loopCounter = 0;  // Counter for loop iterations
  static unsigned long intervalTimer = millis();  // Timer for periodic actions
  static unsigned long intervalTimer2 = millis();  // Secondary timer for additional periodic actions
  static unsigned long rebootTimer = millis();  // Timer to track reboot intervals
  static bool isScanning = false;  // Flag to manage Bluetooth scanning state

  while (true) {
    vTaskDelay(73 / portTICK_RATE_MS);  // Short delay for task pacing

    ss2k->FTMSModeShiftModifier();  // Adjust mode based on shifter position

    // Transmit serial data if applicable
    if (currentBoard.auxSerialTxPin) {
      ss2k->txSerial();
    }

    // Handle system reboot request
    if (ss2k->rebootFlag) {
      vTaskDelay(100 / portTICK_RATE_MS);
      ESP.restart();  // Perform system reboot
    }

    // Save configurations if flagged
    if (ss2k->saveFlag) {
      ss2k->saveFlag = false;
      userConfig->saveToLittleFS();  // Save user configuration to filesystem
      userPWC->saveToLittleFS();  // Save physical working capacity data to filesystem
    }

    // Periodic actions for logging and Bluetooth device management
    if ((millis() - intervalTimer) > 2003) {
      // Perform periodic actions such as log writing and Bluetooth scan management
      logHandler.writeLogs();
      webSocketAppender.Loop();
      intervalTimer = millis();  // Reset timer
    }

    if ((millis() - intervalTimer2) > 6007) {
      // Additional periodic actions
      if (NimBLEDevice::getScan()->isScanning() && isScanning) {
        // Force stop scanning to prevent runaway scans
        NimBLEDevice::getScan()->stop();
        isScanning = false;
      } else {
        isScanning = true;
      }
      intervalTimer2 = millis();  // Reset secondary timer
    }

    // Reboot device for maintenance or inactivity
    if ((millis() - rebootTimer) > 1800000 && !NimBLEDevice::getServer()->getConnectedCount()) {
      // Reboot device if inactive for more than half an hour
      ss2k->rebootFlag = true;
    }

    loopCounter++;  // Increment loop counter
  }
}

            (rtConfig->watts.getTarget() + (shiftDelta * ERG_PER_SHIFT) > userConfig->getMaxWatts())) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift to %dw blocked", rtConfig->watts.getTarget() + shiftDelta);
          break;
        }
        rtConfig->watts.setTarget(rtConfig->watts.getTarget() + (ERG_PER_SHIFT * shiftDelta));
        SS2K_LOG(MAIN_LOG_TAG, "ERG Shift. New Target: %dw", rtConfig->watts.getTarget());
// Format output for FTMS passthrough
#ifndef INTERNAL_ERG_4EXT_FTMS
        int adjustedTarget         = rtConfig->watts.getTarget() / userConfig->getPowerCorrectionFactor();
        const uint8_t translated[] = {FitnessMachineControlPointProcedure::SetTargetPower, (uint8_t)(adjustedTarget & 0xff), (uint8_t)(adjustedTarget >> 8)};
        spinBLEClient.FTMSControlPointWrite(translated, 3);
#endif
        break;
      }

      case FitnessMachineControlPointProcedure::SetTargetResistanceLevel:  // Resistance Mode
      {
        rtConfig->setShifterPosition(ss2k->lastShifterPosition);  // reset shifter position because we're remapping it to resistance target
        if (rtConfig->getMaxResistance() != DEFAULT_RESISTANCE_RANGE) {
          if (rtConfig->resistance.getTarget() + shiftDelta < rtConfig->getMinResistance()) {
            rtConfig->resistance.setTarget(rtConfig->getMinResistance());
            SS2K_LOG(MAIN_LOG_TAG, "Resistance shift less than min %d", rtConfig->getMinResistance());
            break;
          } else if (rtConfig->resistance.getTarget() + shiftDelta > rtConfig->getMaxResistance()) {
            rtConfig->resistance.setTarget(rtConfig->getMaxResistance());
            SS2K_LOG(MAIN_LOG_TAG, "Resistance shift exceeded max %d", rtConfig->getMaxResistance());
            break;
          }
          rtConfig->resistance.setTarget(rtConfig->resistance.getTarget() + shiftDelta);
          SS2K_LOG(MAIN_LOG_TAG, "Resistance Shift. New Target: %d", rtConfig->resistance.getTarget());
        }
        break;
      }

      default:  // Sim Mode
      {
        SS2K_LOG(MAIN_LOG_TAG, "Shift %+d pos %d tgt %d min %d max %d r_min %d r_max %d", shiftDelta, rtConfig->getShifterPosition(), ss2k->targetPosition, rtConfig->getMinStep(),
                 rtConfig->getMaxStep(), rtConfig->getMinResistance(), rtConfig->getMaxResistance());

        if (((ss2k->targetPosition + shiftDelta * userConfig->getShiftStep()) < rtConfig->getMinStep()) ||
            ((ss2k->targetPosition + shiftDelta * userConfig->getShiftStep()) > rtConfig->getMaxStep())) {
          SS2K_LOG(MAIN_LOG_TAG, "Shift Blocked by stepper limits.");
          rtConfig->setShifterPosition(ss2k->lastShifterPosition);
        } else if ((rtConfig->resistance.getValue() < rtConfig->getMinResistance()) && (shiftDelta > 0)) {
          // User Shifted in the proper direction - allow
        } else if ((rtConfig->resistance.getValue() > rtConfig->getMaxResistance()) && (shiftDelta < 0)) {
          // User Shifted in the proper direction - allow
        } else if ((rtConfig->resistance.getValue() > rtConfig->getMinResistance()) && (rtConfig->resistance.getValue() < rtConfig->getMaxResistance())) {
          // User Shifted in bounds - allow
        } else {
          // User tried shifting further into the limit - block.
          SS2K_LOG(MAIN_LOG_TAG, "Shift Blocked by resistance limit.");
          rtConfig->setShifterPosition(ss2k->lastShifterPosition);
        }
        uint8_t _controlData[] = {FitnessMachineControlPointProcedure::SetIndoorBikeSimulationParameters, 0x00, 0x00, 0x00, 0x00, 0x28, 0x33};
        spinBLEClient.FTMSControlPointWrite(_controlData, 7);
      }
    }
    ss2k->lastShifterPosition = rtConfig->getShifterPosition();
    spinBLEServer.notifyShift();
  }
}

void SS2K::restartWifi() {
  httpServer.stop();
  vTaskDelay(100 / portTICK_RATE_MS);
  stopWifi();
  vTaskDelay(100 / portTICK_RATE_MS);
  startWifi();
  httpServer.start();
}

void SS2K::moveStepper(void *pvParameters) {
  engine.init();
  bool _stepperDir = userConfig->getStepperDir();
  stepper          = engine.stepperConnectToPin(currentBoard.stepPin);
  stepper->setDirectionPin(currentBoard.dirPin, _stepperDir);
  stepper->setEnablePin(currentBoard.enablePin);
  stepper->setAutoEnable(true);
  stepper->setSpeedInHz(DEFAULT_STEPPER_SPEED);
  stepper->setAcceleration(STEPPER_ACCELERATION);
  stepper->setDelayToDisable(1000);

  while (1) {
    if (stepper) {
      ss2k->stepperIsRunning = stepper->isRunning();
      if (!ss2k->externalControl) {
        if ((rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetPower) ||
            (rtConfig->getFTMSMode() == FitnessMachineControlPointProcedure::SetTargetResistanceLevel)) {
          ss2k->targetPosition = rtConfig->getTargetIncline();
        } else {
          // Simulation Mode
          ss2k->targetPosition = rtConfig->getShifterPosition() * userConfig->getShiftStep();
          ss2k->targetPosition += rtConfig->getTargetIncline() * userConfig->getInclineMultiplier();
        }
      }

      if (ss2k->syncMode) {
        stepper->stopMove();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        stepper->setCurrentPosition(ss2k->targetPosition);
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }

      if (rtConfig->getMaxResistance() != DEFAULT_RESISTANCE_RANGE) {
        if ((rtConfig->resistance.getValue() >= rtConfig->getMinResistance()) && (rtConfig->resistance.getValue() <= rtConfig->getMaxResistance())) {
          stepper->moveTo(ss2k->targetPosition);
        } else if (rtConfig->resistance.getValue() < rtConfig->getMinResistance()) {  // Limit Stepper to Min Resistance
          stepper->moveTo(stepper->getCurrentPosition() + 10);
        } else {  // Limit Stepper to Max Resistance
          stepper->moveTo(stepper->getCurrentPosition() - 10);
        }

      } else {
        if ((ss2k->targetPosition >= rtConfig->getMinStep()) && (ss2k->targetPosition <= rtConfig->getMaxStep())) {
          stepper->moveTo(ss2k->targetPosition);
        } else if (ss2k->targetPosition <= rtConfig->getMinStep()) {  // Limit Stepper to Min Position
          stepper->moveTo(rtConfig->getMinStep());
        } else {  // Limit Stepper to Max Position
          stepper->moveTo(rtConfig->getMaxStep());
        }
      }

      vTaskDelay(100 / portTICK_PERIOD_MS);
      rtConfig->setCurrentIncline((float)stepper->getCurrentPosition());

      if (connectedClientCount() > 0) {
        stepper->setAutoEnable(false);  // Keep the stepper from rolling back due to head tube slack. Motor Driver still lowers power between moves
        stepper->enableOutputs();
      } else {
        stepper->setAutoEnable(true);  // disable output FETs between moves so stepper can cool. Can still shift.
      }

      if (_stepperDir != userConfig->getStepperDir()) {  // User changed the config direction of the stepper wires
        _stepperDir = userConfig->getStepperDir();
        while (stepper->isRunning()) {  // Wait until the motor stops running
          vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        stepper->setDirectionPin(currentBoard.dirPin, _stepperDir);
      }
    }
  }
}

bool IRAM_ATTR SS2K::deBounce() {
  if ((millis() - lastDebounceTime) > debounceDelay) {  // <----------------This should be assigned it's own task and just switch a global bool whatever the reading is at, it's
                                                        // been there for longer than the debounce delay, so take it as the actual current state: if the button state has changed:
    lastDebounceTime = millis();
    return true;
  }

  return false;
}

///////////// Interrupt Functions /////////////
void IRAM_ATTR SS2K::shiftUp() {  // Handle the shift up interrupt IRAM_ATTR is to keep the interrupt code in ram always
  if (ss2k->deBounce()) {
    if (!digitalRead(currentBoard.shiftUpPin)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() - 1 + userConfig->getShifterDir() * 2);
    } else {
      ss2k->lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void IRAM_ATTR SS2K::shiftDown() {  // Handle the shift down interrupt
  if (ss2k->deBounce()) {
    if (!digitalRead(currentBoard.shiftDownPin)) {  // double checking to make sure the interrupt wasn't triggered by emf
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1 - userConfig->getShifterDir() * 2);
    } else {
      ss2k->lastDebounceTime = 0;
    }  // Probably Triggered by EMF, reset the debounce
  }
}

void SS2K::resetIfShiftersHeld() {
// Interrupt Handlers and Miscellaneous Functions
// This section defines functions that handle external interrupts and perform various auxiliary tasks within the SmartSpin2k device.

// Debounce logic for shift button presses
bool IRAM_ATTR SS2K::deBounce() {
  // Logic to ensure button press is legitimate and not due to noise
  if ((millis() - lastDebounceTime) > debounceDelay) {
    lastDebounceTime = millis();  // Update debounce time
    return true;  // Valid button press
  }
  return false;  // Invalid press, likely noise
}

// Shift up interrupt handler
void IRAM_ATTR SS2K::shiftUp() {
  if (ss2k->deBounce()) {
    // Check if the shift up button was legitimately pressed
    if (!digitalRead(currentBoard.shiftUpPin)) {
      // Adjust shifter position accordingly
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() - 1 + userConfig->getShifterDir() * 2);
    } else {
      // Reset debounce time if triggered by electromagnetic interference
      ss2k->lastDebounceTime = 0;
    }
  }
}

// Shift down interrupt handler
void IRAM_ATTR SS2K::shiftDown() {
  if (ss2k->deBounce()) {
    // Check if the shift down button was legitimately pressed
    if (!digitalRead(currentBoard.shiftDownPin)) {
      // Adjust shifter position accordingly
      rtConfig->setShifterPosition(rtConfig->getShifterPosition() + 1 - userConfig->getShifterDir() * 2);
    } else {
      // Reset debounce time if triggered by electromagnetic interference
      ss2k->lastDebounceTime = 0;
    }
  }
}

// Resets device to default settings if both shifters are held down simultaneously
void SS2K::resetIfShiftersHeld() {
  if ((digitalRead(currentBoard.shiftUpPin) == LOW) && (digitalRead(currentBoard.shiftDownPin) == LOW)) {
    // Indicate reset process through LED blinking
    for (int x = 0; x < 10; x++) {
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      digitalWrite(LED_PIN, LOW);
    }
    // Perform filesystem format and reset user configuration to defaults
    for (int i = 0; i < 20; i++) {
      LittleFS.format();
      userConfig->setDefaults();
      userConfig->saveToLittleFS();  // Save default configuration
    }
    ESP.restart();  // Reboot device
  }
}
void SS2K::updateStepperPower() {
  uint16_t rmsPwr = (userConfig->getStepperPower());
  driver.rms_current(rmsPwr);
  uint16_t current = driver.cs_actual();
  SS2K_LOG(MAIN_LOG_TAG, "Stepper power is now %d.  read:cs=%U", userConfig->getStepperPower(), current);
}

// Applies current StealthChop to driver
void SS2K::updateStealthChop() {
  bool t_bool = userConfig->getStealthChop();
  driver.en_spreadCycle(!t_bool);
  driver.pwm_autoscale(t_bool);
  driver.pwm_autograd(t_bool);
  SS2K_LOG(MAIN_LOG_TAG, "StealthChop is now %d", t_bool);
}

// Applies current stepper speed
void SS2K::updateStepperSpeed() {
  int t = userConfig->getStepperSpeed();
  stepper->setSpeedInHz(t);
  SS2K_LOG(MAIN_LOG_TAG, "StepperSpeed is now %d", t);
}

// Checks the driver temperature and throttles power if above threshold.
void SS2K::checkDriverTemperature() {
  static bool overTemp = false;
  if (static_cast<int>(temperatureRead()) > THROTTLE_TEMP) {  // Start throttling driver power at 72C on the ESP32
    uint8_t throttledPower = (THROTTLE_TEMP - static_cast<int>(temperatureRead())) + currentBoard.pwrScaler;
    driver.irun(throttledPower);
    SS2K_LOG(MAIN_LOG_TAG, "Over temp! Driver is throttling down! ESP32 @ %f C", temperatureRead());
    overTemp = true;
  } else if (static_cast<int>(temperatureRead()) < THROTTLE_TEMP) {
    if (overTemp) {
      SS2K_LOG(MAIN_LOG_TAG, "Temperature is now under control. Driver current reset.");
      driver.irun(currentBoard.pwrScaler);
    }
    overTemp = false;
  }
}

void SS2K::motorStop(bool releaseTension) {
  stepper->stopMove();
  stepper->setCurrentPosition(ss2k->targetPosition);
  if (releaseTension) {
    stepper->moveTo(ss2k->targetPosition - userConfig->getShiftStep() * 4);
  }
}

void SS2K::txSerial() {  // Serial.printf(" Before TX ");
  if (PELOTON_TX && (txCheck >= 1)) {
    static int alternate = 0;
    byte buf[4]          = {PELOTON_REQUEST, 0x00, 0x00, PELOTON_FOOTER};
    switch (alternate) {
      case 0:
        buf[PELOTON_REQ_POS] = PELOTON_POW_ID;
        alternate++;
        break;
      case 1:
        buf[PELOTON_REQ_POS] = PELOTON_CAD_ID;
        alternate++;
        break;
      case 2:
        buf[PELOTON_REQ_POS] = PELOTON_RES_ID;
        alternate            = 0;
        txCheck--;
        break;
    }
    buf[PELOTON_CHECKSUM_POS] = (buf[0] + buf[1]) % 256;
    if (auxSerial.availableForWrite() >= PELOTON_RQ_SIZE) {
      auxSerial.write(buf, PELOTON_RQ_SIZE);
    }
  } else if (PELOTON_TX && txCheck <= 0) {
    if (txCheck == 0) {
      txCheck = -TX_CHECK_INTERVAL;
    } else if (txCheck == -1) {
      txCheck = 1;
    }
    rtConfig->setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig->setMaxResistance(DEFAULT_RESISTANCE_RANGE);
    txCheck++;
  }
}

void SS2K::pelotonConnected() {
  txCheck = TX_CHECK_INTERVAL;
  if (rtConfig->resistance.getValue() > 0) {
    rtConfig->setMinResistance(MIN_PELOTON_RESISTANCE);
    rtConfig->setMaxResistance(MAX_PELOTON_RESISTANCE);
  } else {
    rtConfig->setMinResistance(-DEFAULT_RESISTANCE_RANGE);
    rtConfig->setMaxResistance(DEFAULT_RESISTANCE_RANGE);
  }
}

void SS2K::rxSerial(void) {
  while (auxSerial.available()) {
    ss2k->pelotonConnected();
    auxSerialBuffer.len = auxSerial.readBytesUntil(PELOTON_FOOTER, auxSerialBuffer.data, AUX_BUF_SIZE);
    for (int i = 0; i < auxSerialBuffer.len; i++) {  // Find start of data string
      if (auxSerialBuffer.data[i] == PELOTON_HEADER) {
        size_t newLen = auxSerialBuffer.len - i;  // find length of sub data
        uint8_t newBuf[newLen];
        for (int j = i; j < auxSerialBuffer.len; j++) {
          newBuf[j - i] = auxSerialBuffer.data[j];
        }
        collectAndSet(PELOTON_DATA_UUID, PELOTON_DATA_UUID, PELOTON_ADDRESS, newBuf, newLen);
      }
    }
  }
}
