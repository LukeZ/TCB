/* OP_PololuQik.cpp Open Panzer PololuQik library - functions for communicating with Pololu's Qik dual-serial motor controllers, 
 *                                                  such as the 2s12v10 and the 2s9v1 running the full Pololu Protocol in 7-bit mode.
 * Source:          http://www.Pololu.com
 * Authors:         Pololu
 *   
 * This library is a modification of the Pololu "qik-arduino" library, and should only be used with the Open Panzer project. 
 * For the original library, see Pololu's github page: https://github.com/pololu/qik-arduino
 *
 * The original Pololu copyright is listed below: 
 *
 * Copyright (c) 2012 Pololu Corporation.  For more information, see
 * http://www.pololu.com/
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * 
 */ 

#include "OP_PololuQik.h"

byte cmd[5]; // serial command buffer

//Constructor
OP_PololuQik::OP_PololuQik(byte deviceID, HardwareSerial *port) : _deviceID(deviceID), _port(port) {}

void OP_PololuQik::autobaud(boolean dontWait) const
{
    autobaud(port(), dontWait);
}

void OP_PololuQik::autobaud(HardwareSerial *thisport, boolean dontWait)
{
    if (!dontWait) { delay(1500); }
    thisport->write(QIK_INIT_COMMAND); // allow qik to autodetect baud rate
#if defined(ARDUINO) && ARDUINO >= 100
    thisport->flush();
#endif
    if (!dontWait) { delay(500); }
    
    // Start off with errors cleared
    thisport->write(QIK_GET_ERROR_BYTE);
}

void OP_PololuQik::command(byte command, byte value) const
{
    cmd[0] = QIK_INIT_COMMAND;
    cmd[1] = deviceID();
    cmd[2] = command;
    cmd[3] = value;    
    _port->write(cmd, 4);   
}

void OP_PololuQik::throttleCommand(byte command, int speed) const
{
    speed = constrain(speed, -126, 126);
    this->command(command, (byte)abs(speed));
}

void OP_PololuQik::motor(byte motor, int speed) const
{
    if      (motor == 1)    throttleCommand((speed < 0 ? QIK_MOTOR_M0_REVERSE : QIK_MOTOR_M0_FORWARD), speed);
    else if (motor == 2)    throttleCommand((speed < 0 ? QIK_MOTOR_M1_REVERSE : QIK_MOTOR_M1_FORWARD), speed);
    else    return;
}

void OP_PololuQik::allStop() const
{
    motor(1, 0);
    motor(2, 0);
}

// Configure device - the settings hardcoded here are designed to work with the TCB.
boolean OP_PololuQik::configurePololu(byte SetDeviceID)
{
    uint8_t secondParts = 0;
     
    // Before configuration, send the autobaud character (0xAA)
    autobaud(true); // True means - don't include the waiting period
    delay(5);
     
    // setConfigurationParameter returns 0 if successful, 1 or 2 if not (bad parameter or bad value)

    // At the very beginning we send an invalid command. This causes the Pololu to turn on its red LED to indicate a communication error. 
    // When this process is over, we will read the error byte, thus causing the Pololu to turn off its red LED. In other words, we will use
    // this feature indirectly to cause the red LED on the Pololu to blink, and this will serve as a visual confirmation to the user that the
    // configuration took place. 
    
    // There is no parameter 66 with a value of 66 on any Pololu device, so the Pololu will turn its red LED on to indicate a Frame Error. 
    setConfigurationParameter(66, 66);    

    // Param 0 - set Device ID
    if (setConfigurationParameter(QIK_PARAM_NUM_DEVICE_ID, SetDeviceID)) return false;
    
    // Param 1- set PWM
    // We set it to 0 which means 7-bit resolution, ~20kHz frequency on the 2s12v10 or ~31kHz on the 2s9v1. 
    if (setConfigurationParameter(QIK_PARAM_NUM_PWM, 0)) return false;
    
    // Param 2 - Shutdown motors on error
    // The values are different depending on the device. 
    // For 2s9v1 the only acceptable values are 0 or 1 (off or on for any error). However it doesn't have current detection, the only errors that
    // can occur are serial errors and we don't want to shut the motors off for those, because if we have multiple devices connected using different
    // protocols, it may decide some message meant for some other device was actually an error. So for 2s9v1 we turn off errors:
    if (setConfigurationParameter(QIK_PARAM_NUM_SHTDN_ERR, 0)) return false;
    // But for 2s12v10 we would like motor-over-current errors enabled and motor fault error enabled, but leave serial errors disabled. This equates to value of 6.
    // We set this one second - if the device is a 2s12v10 it will accept the change, if it is a 2s9v1 it will ignore this command because 6 is not a valid value for it 
    // (However the red LED on the 2s9v1 will turn solid for "invalid value" error). 
    if (setConfigurationParameter(QIK_PARAM_NUM_SHTDN_ERR, 6)) return false;
    
    // Param 3 - Serial timeout - disabled
    if (setConfigurationParameter(QIK_PARAM_NUM_SER_TIMEOUT, 0)) return false;

    // Beyond this point, the configuration parameters only apply to the 2s12v10 but not to the 2s9v1. 
    // We won't immediately return if these setConfigurationParameters fail because of couse for the 2s9v1 
    // they will all fail.  

    // !setConfigurationParameter will return true if the function returns 0, which means it was successful.
    // If successful, we incremenet secondParts. At the end secondParts will give us the count of how many of these second
    // set of configuration parameters succeeded. 

    // Param 4 & 5 - M0 & M1 Acceleration constraint
    // We disable these because the TCB creates its own acceleration constraints
    if (!setConfigurationParameter(QIK_PARAM_NUM_M0_ACCEL, 0)) secondParts += 1;
    if (!setConfigurationParameter(QIK_PARAM_NUM_M1_ACCEL, 0)) secondParts += 1;

    // Param 6 & 7 - Braking
    // Disabled because once again the TCB handles this
    if (!setConfigurationParameter(QIK_PARAM_NUM_M0_BRAKE, 0)) secondParts += 1;
    if (!setConfigurationParameter(QIK_PARAM_NUM_M1_BRAKE, 0)) secondParts += 1;

    // Param 8 & 9 - Current-limits - 
    // These are a bit complicated and you would have to know which Qik device the user had
    // to even start to be able to determine an appropriate current limit. 
    if (!setConfigurationParameter(QIK_PARAM_NUM_M0_CURLIMIT, 0)) secondParts += 1;
    if (!setConfigurationParameter(QIK_PARAM_NUM_M1_CURLIMIT, 0)) secondParts += 1;

    // Param 10 & 11 - Current-limit response
    // This value isn't important if the current-limiting is disabled. But to be safe, we 
    // set it to the default value of 4
    if (!setConfigurationParameter(QIK_PARAM_NUM_M0_CURRESPONSE, 4)) secondParts += 1;
    if (!setConfigurationParameter(QIK_PARAM_NUM_M0_CURRESPONSE, 4)) secondParts += 1;

    delay(50);  // We delay a bit to extend the time the red LED remains on

    // If this was a 2s9v1 we will have sent it several commands with invalid values because the 2s12v10 values do not all translate to the smaller device. 
    // This will have caused the 2s9v1 to turn the red LED on as an indication of error. Now we clear the error by calling the Get Error Byte function.
    _port->write(QIK_GET_ERROR_BYTE);

    // Decide whether we succeeded or not
    if (secondParts == 8 || secondParts == 0)
    {   // In this case all the common parameters were set successfully, and either all the 
        // 2s12v10-specific parameters were set or none of them were, in which case this was 
        // probably a 2s9v1
        return true;
    }
    else
    {   // In this case some of the 2s12v1 parameters were set successfully, but some were not.
        // This would seem to indicate we are communicating with a 2s12v10 but that something failed. 
        return false;
    }
}

byte OP_PololuQik::setConfigurationParameter(byte parameter, byte value)
{
    // Here we use the compact protocol. One of the parameters we may be getting or setting is the DeviceID itself,
    // and if we don't know it ahead of time, we can't include it in the full protocol. 
    cmd[0] = QIK_SET_CONFIGURATION_PARAMETER;
    cmd[1] = parameter;
    cmd[2] = value;
    cmd[3] = 0x55;    // 0x55 and 0x2A are format bytes that make it more difficult for this command 
    cmd[4] = 0x2A;    // to be accidentally sent, as might result from a noisy serial connection or buggy user code.
    _port->write(cmd, 5);
  
    // It takes 4mS for the Pololu to process this command. You should not send commands to the Qik again until you 
    // have received this byte, or waited at least 4mS. 
    //while (_port->available() < 1);
    //return _port->read();
  
    // Presently we are not reading serial data on the motor serial port (we can, but aren't - we're only transmitting).
    // So we're not going to pick up any response. Instead we just wait 5mS and hard-code it to success (0 = success)
    delay(5);
    return 0;
}

// This one is commented out because we are not presently reading anything on the motor serial port, only transmitting
/*
byte OP_PololuQik::getConfigurationParameter(byte parameter)
{
    cmd[0] = QIK_GET_CONFIGURATION_PARAMETER;
    cmd[1] = parameter;
    _port->write(cmd, 2);
    while (_port->available() < 1);
    return _port->read();
}
*/