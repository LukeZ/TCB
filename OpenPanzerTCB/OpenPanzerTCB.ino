/* OpenPanzerTCB    Open Panzer Tank Control Board (TCB) firmware
 * Source:          openpanzer.org              
 * Authors:         Luke Middleton
 * 
 * Copyright 2017 Open Panzer
 *   
 * For more information, see the Open Panzer Wiki:
 * http://openpanzer.org/wiki/
 * 
 * TCB GitHub Repository:
 * https://github.com/OpenPanzerProject/TCB
 *    
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */ 

#include "OP_Settings.h"
#include "OP_FunctionsTriggers.h"
#include "OP_IO.h"
#include "OP_PPMDecode.h"
#include "OP_SBusDecode.h"
#include "OP_iBusDecode.h"
#include "OP_Motors.h"
#include "OP_Servo.h"
#include "OP_Sabertooth.h"
#include "OP_PololuQik.h"
#include "OP_Scout.h"
#include "OP_SimpleTimer.h"
#include "OP_Driver.h"
#include "OP_IRLib.h"
#include "OP_IRLibMatch.h"
#include "OP_Sound.h"
#include "OP_TBS.h"
#include "OP_Button.h"
#include "OP_EEPROM.h"
#include "EEPROMex.h"
#include "OP_Radio.h"
#include "OP_Tank.h"
#include "OP_PCComm.h"


// GLOBAL VARIABLES
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------>>
// PROJECT SPECIFIC EEPROM
    OP_EEPROM eeprom;                            // Wrapper class for dealing with eeprom. Note that EEPROM is also a valid object, it is the name of the EEPROMex class instance. Use the correct one!
                                                 // OP_EEPROM basically provides some further functionality beyond EEPROMex. 
// SIMPLE TIMER 
    OP_SimpleTimer timer;                        // SimpleTimer named "timer"
    boolean TimeUp = true;

// DEBUG FLAG
    boolean DEBUG = false;                       // Start at false, but it will later get set to whatever value is stored in EEPROM
    boolean SAVE_DEBUG = false;                  // We may temporarily want to disable the debug, but we save a copy of the actual state so we can revert it
    HardwareSerial *DebugSerial;                 // Which serial port to print debug messages to (HardwareSerial is equal to Serial0/Serial Port 0/USART0)

// RADIO INPUTS
    OP_Radio Radio;
    boolean Failsafe = false;                    // Are we in failsafe due to some radio problem?
    int RxSignalLostTimerID = 0;                 // Timer used to blink lights when the Rx signal is lost

// SPECIAL FUNCTIONS AND TRIGGERS
    void_FunctionPointer_uint16 SF_Callback[MAX_FUNCTION_TRIGGERS];  // An array of function pointers that we will tie to our special function triggers. 
    uint8_t triggerCount = 0;                    // How many triggers defined. Will be determined at run time. 

// I/O PINS
    external_io IO_Pin[NUM_IO_PORTS];            // Information about the general purpose I/O pins
    boolean UsingIOInputs = false;               // Set to true if any IO pin is set to input and that input has been set as a trigger for some function. Otherwise, no point in checking the port. 

// LIGHTS/AUX OUTPUT
    boolean BrakeLightsActive = false;           // Are the brake lights on. We need to know this and running light state because they are both on the same output.
    boolean RunningLightsActive = false;         // Are the running lights active. We don't want running lights and brake lights to turn each other off if they're not supposed to. 
    uint8_t RunningLightsDimLevel;               // We will convert the user's setting (0-100) to a PWM value (0-255)
    int AuxOutputTimerID = 0;                    // The AuxOutput can be set to blink or strobe. We will need a timer ID for it. 
    boolean AuxOutputBlinking = false;
    boolean AuxOutputRevolving = false;

// TANK OBJECTS
    OP_Servos TankServos;
    OP_Driver Driver;                         
    OP_Engine TankEngine;
    OP_Transmission TankTransmission;
    OP_Sound * TankSound;
    OP_Tank Tank;                              

// MOTOR OBJECTS
    // For tanks and certain half-tracks, we have individual tread motors
        Motor * RightTread;
        Motor * LeftTread;
    // For cars and half-tracks with synchronized track speeds, we have a steering servo and a drive motor
        Motor * DriveMotor;
        Motor * SteeringServo;
    // For ancient Tamiya gearboxes, the DKLM "Propulsion Dynamic" gearboxes, and any others that use a single motor for drive and a secondary motor to shift power from one tread to the other, 
    // we have a "SteeringMotor" object which will control the steering
        Motor * SteeringMotor;
    // We always have turret rotation & elevation motors
        Motor * TurretRotation;
        Motor * TurretElevation;
        Servo_PAN * Barrel;         // In cases where TurretElevation type = SERVO_PAN, we will also create a Servo_PAN object directly and call it barrel, for use in barrel stabilization.
    // We always have a recoil servo
        Servo_RECOIL * RecoilServo;
    // And of course we always have the mechanical smoker motor
        Onboard_Smoker * Smoker;
        boolean SmokerEnabled = true;   // The user can enable/disable the smoker on the fly, we assume it is enabled to begin with. 
    // We may optionally have up to 7 general purpose RC outputs, depending on how the user sets up the other motor objects and sound cards. The user can choose to have them be regular RC pass-through
    // or create them as Pan servo pass-throughs by selecting the appropriate function in OP Config. RC output #5 is always reserved for recoil no matter what, so it can't be repurposed for anything else. 
        Servo_ESC * RCOutput1;      // If the drive motors are onboard or serial controllers, this will be available
        Servo_ESC * RCOutput2;      // If the drive motors are onboard or serial controllers, AND if there is no steering servo specified (ie not car or halftrack), this will be available
        Servo_ESC * RCOutput3;      // If the turret rotation motor is serial or onboard, this will be available
        Servo_ESC * RCOutput4;      // If the turret elevation motor is serial or onboard, this will be available
        Servo_ESC * RCOutput6;      // May be available depending on which sound card is implemented (not with Benedini or Taigen)
        Servo_ESC * RCOutput7;      // May be available depending on which sound card is implemented (not with Benedini)
        Servo_ESC * RCOutput8;      // May be available depending on which sound card is implemented (not with Benedini)
        Servo_PAN * ServoOutput1;   // If the drive motors are onboard or serial controllers, this will be available
        Servo_PAN * ServoOutput2;   // If the drive motors are onboard or serial controllers, AND if there is no steering servo specified (ie not car or halftrack), this will be available
        Servo_PAN * ServoOutput3;   // If the turret rotation motor is serial or onboard, this will be available
        Servo_PAN * ServoOutput4;   // If the turret elevation motor is serial or onboard, this will be available
        Servo_PAN * ServoOutput6;   // May be available depending on which sound card is implemented (not with Benedini or Taigen)
        Servo_PAN * ServoOutput7;   // May be available depending on which sound card is implemented (not with Benedini)
        Servo_PAN * ServoOutput8;   // May be available depending on which sound card is implemented (not with Benedini)
        boolean RCOutput1_Available = false;
        boolean RCOutput2_Available = false;
        boolean RCOutput3_Available = false;
        boolean RCOutput4_Available = false;
        boolean RCOutput6_Available = false;
        boolean RCOutput7_Available = false;
        boolean RCOutput8_Available = false;
    // We may also be able to control the onboard motors A or B directly if they are not assigned to any drive or turret function
        Onboard_ESC * MotorA;
        Onboard_ESC * MotorB;
        boolean MotorA_Available = false;
        boolean MotorB_Available = false;

// MOTOR IDLE TIMER
    int IdleOffTimerID = 0;                       // User has the option of setting a length of time, after which, if the engine has been idle the whole time, the engine will automatically turn off. 

// DRIVING ADJUSTMENTS
    uint8_t DrivingProfile = 1;                   // There are 2 driving profiles possible - we default to 1, but if the user implements a special function they can change it to 2 (alternate) on the fly
    boolean Nudge = false;                        // We can nudge the motors when first moving from a stop, for a crisper response. When the Nudge flag is true, the nudge effect will be active. 

// INERTIAL MEASUREMENT UNIT (IMU)
//    OP_BNO055 IMU;                                // Class for handling the Bosch BNO055 9DOF IMU sensor (on the Adafruit breakout board) - NOT USED FOR NOW
    boolean UseIMU = false;
    boolean IMU_Present = false;
    boolean IMU_ReadyToSample = true;
    uint8_t BarrelSensitivity;                    // Number from 0-100 that defines how sensitive the barrel stabilization is
    uint8_t HillSensitivity;                      // Number from 0-100 that defines how sensitive the hill physics effect is

// PC COMMUNICATION OBJECT
    OP_PCComm PCComm;

// LVC STATUS
    boolean HavePower = false;                    // This will be set to true if battery is not unplugged, and voltage level is above the cutoff
    boolean LVC = false;                          // If true, we are in low-voltage cutoff mode
    boolean BatteryUnplugged = true;              // Assume unplugged to start. Try to detect if the user is running only USB power. This isn't strictly an LVC condition, but we also don't want to try running the motors. 
    int LVC_TimerID = 0;                          // This timer is used to periodically check the battery voltage level
    int LVC_BlinkTimerID = 0;                     // Timer used to blink lights when the battery voltage has got too low

// REPAIR
    #define REPAIR_NONE     0                     // No repair operation ongoing
    #define REPAIR_SELF     1                     // We are being repaired by another tank
    #define REPAIR_OTHER    2                     // We are repairing another tank
    uint8_t RepairOngoing = REPAIR_NONE;          // Init 

// INPUT BUTTON
    OP_Button InputButton = OP_Button(pin_Button, true, true, 25);   // Initialize a button object. Set pin, internal pullup = true, inverted = true, debounce time = 25 mS



void setup()
{   // Here we get everything started. Begin with the most important things, and keep going in descending order

    // LOAD VALUES FROM EEPROM    
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        boolean did_we_init = eeprom.begin();                      // begin() will initialize EEPROM if it never has been before, and load all EEPROM settings into our ramcopy struct
        //if (did_we_init) { DebugSerial->println(F("EEPROM Initalized")); } // We can use this for testing, but in general it's not needed, it doesn't happen often, and rarely would the user catch it. 

        
    // INIT SERIALS & COMMS
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        Serial.begin(USB_BAUD_RATE);                               // Hardware Serial 0 - Connected to FTDI/USB connector. We also have a baud rate in EEPROM (eeprom.ramcopy.USBSerialBaud) but for now we leave this static at the baud rate set in OP_Settings.h
        AuxSerial.begin(eeprom.ramcopy.AuxSerialBaud);             // Hardware Serial 1 - alternate communication port
        MotorSerial.begin(eeprom.ramcopy.MotorSerialBaud);         // Hardware Serial 2 - reserved for serial motor controllers
        Serial3Tx.begin(eeprom.ramcopy.Serial3TxBaud);             // Hardware Serial 3 - Receive used for serial radio receivers (SBus,iBus,etc). Tx brought out to Serial 3 connector, but Tx disabled if serial receiver detected. 
                                                                   //                     The original idea was to use Serial 3 for an Adafruit or Sparkfun serial LCD, and the connector is compatible with those, but no code was written for that application.
        PCComm.begin(&eeprom, &Radio);                             // Initialize the PC communication class. It needs a reference to OP_EEPROM annd OP_Radio objects which we pass by reference.
        //PCComm.skipCRC();                                        // We can skip CRC checking for testing, but don't use this in production. 
        SetActiveCommPort();                                       // Check Dipswitch #5 and set the active communication port to USB if switch On, or Serial 1 if switch Off

        // Now send our first message out the port, if we initialized the EEPROM
        SAVE_DEBUG = eeprom.ramcopy.PrintDebug;                    // Does the user want to see debug messages
        DEBUG = false;                                             // But we leave the actual DEBUG initialized to false. It will get set equal to SAVE_DEBUG only after a certain amount of time has passed, so we 
                                                                   // don't put messages out the port right after boot - they could conflict with our ability to detect communication efforts from the PC. 

    // PINS NOT RELATED TO OBJECTS - SETUP
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        // We want to setup the pins as early as possible to put all outputs in a safe state. But remember to read EEPROM first because some EEPROM settings will determine how the pins are set. 
        SetupPins();                                               // Any pin not explicitly set by a library gets initalized here. 
        RedLedOn();                                                // Keep the Red LED on solid until we are out of setup. 
        
    // BUTTON CHECK
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        // If the user holds down the button while rebooting, and keeps holding for 4 seconds, we conduct a factory reset - which just means, restore 
        // all eeprom variables to default. 
        do
        {
            InputButton.read();
            if (InputButton.pressedFor(3700))   // People count fast, so don't actually wait the full 4 seconds.
            {
                eeprom.factoryReset();
                // In general we try to avoid sending anything out the serial port for a few seconds after boot in case the PC is trying to communicate with us - 
                // but if we are doing a factory reset, then it's fair to assume we don't need to catch any comms right now. 
                PrintLines(2);
                PrintDebugLine();
                DebugSerial->print(F("FACTORY RESET! "));
                // Now blink both red and green LEDs slowly 5 times. This uses straight delays and blocks execution, which is fine for this case. 
                uint8_t HowMany = 5;
                for (int i=1; i<=HowMany; i++)
                {
                    GreenLedOn();
                    RedLedOn();
                    delay(750);
                    DebugSerial->print(F("! "));
                    GreenLedOff();
                    RedLedOff();
                    if (i < HowMany)
                    { delay(500);  }
                }                
                PrintLine();
                PrintDebugLine();
                PrintLines(2);
                break;
            }
        } while (InputButton.isPressed());

    // TIMERS
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    // These are macros, see OP_Settings.h for definitions
        SetupTimer1();
        SetupTimer4();
        SetupTimer5();
        
    // MOTOR OBJECTS
    // -------------------------------------------------------------------------------------------------------------------------------------------------->    
        TankServos.begin();                             // Do this before setting up motor objects
        InstantiateMotorObjects();

    // OTHER OBJECTS - BEGIN
    // -------------------------------------------------------------------------------------------------------------------------------------------------->    
        Radio.saveTimer(&timer);                        // Pass the radio a pointer our timer object
        Driver.begin(eeprom.ramcopy.DriveType, 
                     eeprom.ramcopy.TurnMode, 
                     eeprom.ramcopy.NeutralTurnAllowed);
        SetDrivingProfile(DrivingProfile);              // See Driving tab
        TankEngine.begin(eeprom.ramcopy.EnginePauseTime_mS, SAVE_DEBUG, DebugSerial);
        TankTransmission.begin(SAVE_DEBUG, DebugSerial);
        InstantiateSoundObject();                       // Do this after TankServos.begin();
        InstantiateOptionalServoOutputs();              // Do this after InstantiateSoundObject();
        // The tank object needs to be told whether IR is enabled, the weight class and settings, the IR and Damage protocols to use, whether or not the tank is a repair tank or battle, 
        // whether we are running an airsoft unit or mechanical recoil, the mechanical recoil delay, the machine gun blink interval, 
        // and it also needs a pointer to our Servo_RECOIL object and the TankSound object. RecoilServo is already a pointer, but TankSound we pass by reference.
        // First fill a temp battle_seetings struct
        battle_settings BattleSettings;
        BattleSettings.WeightClass = GetWeightClass();  // This is set by the user via dip-switch
        BattleSettings.ClassSettings = eeprom.ramcopy.CustomClassSettings;
        BattleSettings.IR_FireProtocol = eeprom.ramcopy.IR_FireProtocol;
        BattleSettings.IR_Team = eeprom.ramcopy.IR_Team;
        BattleSettings.IR_HitProtocol_2 = eeprom.ramcopy.IR_HitProtocol_2;
        BattleSettings.IR_RepairProtocol = eeprom.ramcopy.IR_RepairProtocol;
        BattleSettings.IR_MGProtocol = eeprom.ramcopy.IR_MGProtocol;
        BattleSettings.Use_MG_Protocol = eeprom.ramcopy.Use_MG_Protocol;
        BattleSettings.Accept_MG_Damage = eeprom.ramcopy.Accept_MG_Damage;
        BattleSettings.DamageProfile = eeprom.ramcopy.DamageProfile;
        BattleSettings.SendTankID = eeprom.ramcopy.SendTankID;
        BattleSettings.TankID = eeprom.ramcopy.TankID;
        // Now pass the battle settings and other settings
        Tank.begin(BattleSettings, 
                   eeprom.ramcopy.MechanicalBarrelWithCannon,
                   eeprom.ramcopy.Airsoft,
                   eeprom.ramcopy.ServoRecoilWithCannon,
                   eeprom.ramcopy.RecoilDelay, 
                   eeprom.ramcopy.HiFlashWithCannon,
                   eeprom.ramcopy.AuxFlashWithCannon,
                   eeprom.ramcopy.AuxLightFlashTime_mS,
                   eeprom.ramcopy.MGLightBlink_mS,
                   RecoilServo, 
                   TankSound,
                   &timer);


    // SPECIAL FUNCTIONS - has to come after object creation, because we may assign function pointers to object member functions
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        triggerCount = CountTriggers();
        LoadFunctionTriggers();
        SetActiveInputFlag();                   // Determines if any of the external IO are set to input, and if so, are they matched to a function


    // SETUP SOUND STUFF
    // -------------------------------------------------------------------------------------------------------------------------------------------------->            
        // We retreived our squeak intervals from EEPROM, now load into the sound object
        TankSound->SetSqueak1_Interval(eeprom.ramcopy.Squeak1_MinInterval_mS, eeprom.ramcopy.Squeak1_MaxInterval_mS);
        TankSound->SetSqueak2_Interval(eeprom.ramcopy.Squeak2_MinInterval_mS, eeprom.ramcopy.Squeak2_MaxInterval_mS);
        TankSound->SetSqueak3_Interval(eeprom.ramcopy.Squeak3_MinInterval_mS, eeprom.ramcopy.Squeak3_MaxInterval_mS);
        TankSound->SetSqueak4_Interval(eeprom.ramcopy.Squeak4_MinInterval_mS, eeprom.ramcopy.Squeak4_MaxInterval_mS);
        TankSound->SetSqueak5_Interval(eeprom.ramcopy.Squeak5_MinInterval_mS, eeprom.ramcopy.Squeak5_MaxInterval_mS);
        TankSound->SetSqueak6_Interval(eeprom.ramcopy.Squeak6_MinInterval_mS, eeprom.ramcopy.Squeak6_MaxInterval_mS);          
        // Also whether squeaks are even enabled
        TankSound->Squeak1_SetEnabled(eeprom.ramcopy.Squeak1_Enabled);
        TankSound->Squeak2_SetEnabled(eeprom.ramcopy.Squeak2_Enabled);
        TankSound->Squeak3_SetEnabled(eeprom.ramcopy.Squeak3_Enabled);
        TankSound->Squeak4_SetEnabled(eeprom.ramcopy.Squeak4_Enabled);
        TankSound->Squeak5_SetEnabled(eeprom.ramcopy.Squeak5_Enabled);
        TankSound->Squeak6_SetEnabled(eeprom.ramcopy.Squeak6_Enabled);        
        // And whether some other sounds are enabled
        TankSound->HeadlightSound_SetEnabled(eeprom.ramcopy.HeadlightSound_Enabled);
        TankSound->TurretSound_SetEnabled(eeprom.ramcopy.TurretSound_Enabled);
        TankSound->BarrelSound_SetEnabled(eeprom.ramcopy.BarrelSound_Enabled);

    
    // INERTIAL MEASUREMENT UNIT    (Bosch BNO055 on Adafruit breakout board)
    // -------------------------------------------------------------------------------------------------------------------------------------------------->    
        // Sadly, the Arduino i2c library (called "Wire" or TWI), is completely incompatible with a project of this nature. It breaks every good practice
        // by not only using long delays to block the rest of code, but doing so within ISRs so it blocks other interrupts as well. This wreaks havoc
        // with time-critical tasks like reading the incoming PPM stream, creating the outgoing servo pulses, and reading/writing IR signals. 
        // Therefore we need to use a non-blocking implementation (the underlying hardware uses interrupts so this should be possible). 
        // See the OP_I2C library for our version. 
        
        // Let's determine if we even want to use the IMU in the first place. Initialize to false. 
        UseIMU = false;
        
        // Barrel stabilization requires the turret elevation motor to be type SERVO_PAN, and the user also has to enable it
        if (eeprom.ramcopy.EnableBarrelStabilize)
        {
            if (eeprom.ramcopy.TurretElevationMotor == SERVO_PAN) { UseIMU = true; }
            else
            {
                // In this case, user wants to use Barrel Stabilization, but the turret motor is the wrong type. Disable it.
                eeprom.ramcopy.EnableBarrelStabilize = false;   // This doesn't actually change the setting in eeprom, just our working copy
            }
        }

        // Hill physics requires the user to enable it.
        if (eeprom.ramcopy.EnableHillPhysics) UseIMU = true;

        // EDIT: THE IMU IS NOT YET WORKING RELIABLY. I BELIEVE IT IS AN ISSUE WITH I2C COMMUNICATIONS, WHICH OCCASIONALLY CRASH THE PROGRAM. SEVERAL PORTIONS RELATED
        // TO THE IMU HAVE BEEN COMMENTED-OUT ON THIS TAB OF THE SKETCH, HOWEVER MOST OF THIS CODE IS PERFECTLY FINE IF YOU CAN GET THE LIBRARIES TO DO WHAT THEY'RE 
        // SUPPOSED TO

        // HARD-CODED TO DISABLE:
        IMU_Present = false;
        UseIMU = false;
/*
        // Now see if the IMU is even attached (don't predicate this on UseIMU = true, because the user may have elected to turn off/on the IMU from the radio)
        IMU.begin();                                // Initialize the IMU class
        IMU.checkIfPresent();                       // Now see if the IMU can be detected on the bus
        while (!IMU.process()) { delay(1); }        // Wait for transaction to complete (yes, we block but this is the setup routine so we don't care)
        if (IMU.isPresent()) 
        {   
            //IMU.setup(true, OP_BNO055::OPERATION_MODE_NDOF);    // Device is present, let's see if we can initialize it (true to use external crystal, mode = 9-DOF fusion mode)
            if (IMU.setup(true, OP_BNO055::OPERATION_MODE_IMUPLUS)) // IMUPLUS is fusion data of just the accel and gyro
            {
                IMU_Present = true;                  // Device is present and set-up, so set the flag
            }
            else 
            {
                IMU_Present = false;                 // We couldn't get setup to work, treat it as disconnected
                UseIMU = false;                
            }
        }
        else 
        {   
            IMU_Present = false; // Not attached
            UseIMU = false;     // If it's not present, we're also not going to be using it.
        }
*/        
       
        // Now just because IMU_Present might equal true, doesn't mean we will actually use it: UseIMU can still be false if nothing is enabled. 

        // We also obtain our sensitivity values
        BarrelSensitivity = eeprom.ramcopy.BarrelSensitivity;
        HillSensitivity = eeprom.ramcopy.HillSensitivity;
             

    // LIGHTS
    // -------------------------------------------------------------------------------------------------------------------------------------------------->            
        RunningLightsDimLevel = map(eeprom.ramcopy.RunningLightsDimLevelPct, 0, 100, 0, 255);   // The user sets the dim level as a percent from 0-100, but we want it as a PWM value from 0-255
        if (eeprom.ramcopy.RunningLightsAlwaysOn) RunningLightsOn();

       
    // WAIT FOR PC COMM - AND TRY TO DETECT RECEIVER (kill two birds with one stone)
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        // Assume no connection to start with. 
        StartFailsafe();    // This would usually give a message we don't need right now, except we've initialized DEBUG to false for now
        
        // Now we try to detect radio input. This loop will run forever until the Radio class successfully detects a PPM, SBus, iBus or other stream. 
        // While waiting, it will also be listening for any communication from the computer, since this frequently occurs on reboot. And in fact, 
        // even if we do detect the radio right away, we still wait a short bit of time to give the computer a chance to talk to us. Of course we can
        // accept PC comms later in the main loop too, but since the computer often reboots the device when it wants to communicate, most communication
        // attempts will be happening now. 
        boolean scheduleMsg = false;
        uint32_t startTime = millis();
        do
        {   
            // We will schedule a message for two seconds from now, that will let the user know we are waiting on the radio to do anything. 
            // We wait two seconds because we don't want to be putting traffic on the line right after boot in case the computer was the one that rebooted us
            // and is now trying to communicate. Also, if in two seconds the radio has been detected, or we've moved out of the setup() routine, then the message won't actually display. 
            if (!scheduleMsg && SAVE_DEBUG)
            {
                timer.setTimeout(2000, PrintWaitingForRadio);       // The function can be found in the Utilites tab
                scheduleMsg = true;                                 // Only schedule it once. 
            }
            
            // While we're waiting, check for PC comms
            if (PCComm.CheckPC()) 
            {   // Temporarily disable the failsafe lights
                StopFailsafeLights();
                RedLedOn(); // But leave the Red LED on because we still aren't to the main loop
                // Talk to the computer
                PCComm.ListenToPC();
                // But when we're done talking to the PC, restart the lights
                StartFailsafeLights();
            }
        
            // This will try to auto-detect PPM, SBus, iBus or any other supported protocols. 
            if (Radio.Status() != READY_state) { Radio.detect(); }
            
            PerLoopUpdates();

        } while(Radio.Status() != READY_state || (millis() - startTime) < 700 );
        
        // Ok, if we make it out of the loop, it means the radio is ready! 
        EndFailsafe(); 
        RedLedOn(); // But keep the Red LED one because we aren't into the main loop yet

        // Pass the eeprom ramcopy struct so the Radio object can initialize all channels to the settings saved in eeprom. 
        // But the PCComm class may already have called this, in which case we don't need to, which is why we check the hasBegun() function first. 
        if (!Radio.hasBegun()) Radio.begin(&eeprom.ramcopy);       
        
        // Now check how many channels were detected. If the Radio state is READY_state, we are assured of at least 4 channels.
        int ChannelsDetected = Radio.getChannelCount();
  

    // RANDOM SEED
    // -------------------------------------------------------------------------------------------------------------------------------------------------->    
        randomSeed(analogRead(A0));


    // SET DEBUG TO USER SETTING IN ANOTHER SECOND
    // -------------------------------------------------------------------------------------------------------------------------------------------------->    
        timer.setTimeout(1000, RestoreDebug);
}


void loop()
{
// MAIN LOOP VARIABLES
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------>>
// Drive Modes
    static _driveModes DriveModeCommand = STOP;                       // The Drive Mode presently being commanded by user
    static _driveModes DriveModeCommand_Previous = STOP;              // What was the command last time around? We can use this to ignore spurious transmitter signals.
    static uint8_t ModeChangeCount = 0;                               // How many times through the loop has a new drive mode command (different from the last) been present?
    static uint8_t ModeChangeLimit = 3;                               // The number of times through the loop a new drive mode needs to be detected before we actually change it. 
    static _driveModes DriveModeActual = STOP;                        // As opposed to DriveModeCommand, this is the actual DriveMode being implemented. 
    static _driveModes DriveMode_Previous = STOP;                     // The previous Drive Mode implemented
    static _driveModes DriveMode_LastDirection = STOP;
    static boolean Braking = false;                                   // Are we braking
    static boolean Braking_Previous = false;                          // Were we braking
// More Driving Variables
    static boolean DriveFlag = false;                                 // We start with movement allowed
    static unsigned long TransitionStart;                             // A marker which records the time when the shift transition begins
    static int ThrottleCommand = 0;                                   // Easier than writing "Radio.Sticks.Throttle.command" over and over. Throttle command is a command related to engine speed.
    static int ThrottleCommand_Previous = 0;                          // Value of throttle command from last time through loop
    static int DriveCommand = 0;                                      // Drive command is the speed to the wheels through the transmission. Not the same as throttle. 
    static int TurnCommand = 0;                                       // Turn command
    static int TurnCommand_Previous = 0;                              // Value of turn command last through loop
    static int DriveSpeed = 0;                                        // Differs from Command which is what we are being told by the Tx stick - DriveSpeed is what is being told the motors by software, after modifications to command. 
    static int DriveSpeed_Previous = 0;                               // Previous value of DriveSpeed
    static int ThrottleSpeed = 0;                                     // This will be the RPM of the engine, which is not the same as the drive speed
    static int ThrottleSpeed_Previous = 0;                            // 
    static int RightSpeed = 0;                                        // We also have variables for the speed split between the two tracks
    static int LeftSpeed = 0;
    static int RightSpeed_Previous = 0;                               // This is the last set of speeds. If the new set hasn't changed, we don't need to update the motor controller
    static int LeftSpeed_Previous = 0;
    static int TurnSpeed = 0;                                         // Differs from Command which is what we are being told by the Tx stick - turn speed is what is being told the motors by software, after modifications to command.
    static int TurnSpeed_Previous = 0;                                // Previous value of TurnSpeed
    static boolean WasRunning = false;                                // Has the engine been running? Used to identify the first moment it stops. 
    static boolean NudgeStarted = false;                              // Has a nudge started
    static boolean NudgeEnabled = false;                              // Is nudging even enabled
// Driving Variables Calculated at Startup
    static int ReverseSpeed_Max;                                      // Calculate an absolute figure for max reverse speed based on the user's setting of MaxReverseSpeedPct
    static int ForwardSpeed_Max;                                      // Calcluate an absolute figure for max forward speed based on teh user's setting of MaxForwardSpeedPct
    static float BrakeSensitivityPct;                                 // NOT IMPLEMENTED: We convert the user's BrakeSensitivityPct to a number between 0-1. 
    static int NeutralTurn_Max;                                       // Calculate an absolute figure for max neutral turn speed based on the user's setting of NeutralTurnPct
    static int HalftrackTurn_Max;                                     // Calculate an absolute figure for max rear tread turn based on the user's setting of HalftrackTreadTurnPct
    static uint8_t NudgeAmount = 0;                                   // User saves nudge amount as percent, we convert it to an actual number between 0-255
// Battle Variables
    static boolean Alive = true;                                      // Has the tank been destroyed? If so, Alive = false
    static int DestroyedBlinkerID = 0;                                // Timer ID for blinking lights when tank is destroyed
    HIT_TYPE HitType;                                                 // If we were hit, what kind of hit was it
// Inertial Measurement Unit
    const int IMU_SampleDelay = 20;                                   // The BNO055 can output fusion data up to 100hz. If we set this delay to 20mS that will actually be a refresh rate of 50 times per second. 
    static boolean IMU_WaitingForSample = false;
    static boolean IMU_Updated = false;
    float rawpitch;
    static float pitch;
    const float alpha = 0.90;                                         // Alpha determines the low pass filter on the pitch reading. If Alpha is set to 0.99, it is like taking the average of 100 readings. 
// Barrel stabilization
    static int Barrel_Level;   
    static int Barrel_Offset;
    static boolean BarrelPosChanged = true;
    float TankPitchRange;
// Hill physics
    static uint16_t ThrottlePulseMin = Radio.Sticks.Throttle.Settings->pulseMin;    // Save these so we can manipulate them but always know what to put them back to
    static uint16_t ThrottlePulseMax = Radio.Sticks.Throttle.Settings->pulseMax;
    float HillInclineRange;
// Sounds
    static uint8_t MinSqueakSpeed;
// Blinkers
    static int GreenBlinker = 0;
    static int RedBlinker = 0;
// Startup Flag
    static boolean Startup = true;                                    // Will only be true once
// Time Variables
    static unsigned long currentMillis; 
// Timing stuff
    currentMillis = millis(); 
// Button stuff
    enum {BUTTON_WAIT, BUTTON_TO_WAIT};       
    static uint8_t ButtonState;                                       //The current button state machine state


// MAIN LOOP SETUP - only run once
// ----------------------------------------------------------------------------------------------------------------------------------------------------->>
if (Startup)
{   // This is the first time through the loop - initalize some things

    // We take the user setting of NeutralTurnPct and calculate a max speed for neutral turns
        NeutralTurn_Max = (int)(((float)eeprom.ramcopy.NeutralTurnPct / 100.0) * (float)MOTOR_MAX_FWDSPEED); 
        
    // Same for the percent of turns that can be applied to the treds in halftrack mode
        HalftrackTurn_Max = (int)(((float)eeprom.ramcopy.HalftrackTreadTurnPct / 100.0) * (float)MOTOR_MAX_FWDSPEED); 
    
    // Convert the user setting of MaxForwardSpeedPct & MaxReverseSpeedPct into an absolute max forward/reverse speed
        if (eeprom.ramcopy.MaxForwardSpeedPct < 100) { ForwardSpeed_Max = (int)(((float)eeprom.ramcopy.MaxForwardSpeedPct / 100.0) * (float)MOTOR_MAX_FWDSPEED); }
        else { ForwardSpeed_Max = MOTOR_MAX_FWDSPEED; } // This part isn't really necessary, but we do it just in case we forget an if statement later
        if (eeprom.ramcopy.MaxReverseSpeedPct < 100) { ReverseSpeed_Max = (int)(((float)eeprom.ramcopy.MaxReverseSpeedPct / 100.0) * (float)MOTOR_MAX_REVSPEED); }
        else { ReverseSpeed_Max = MOTOR_MAX_REVSPEED; } // This part isn't really necessary, but we do it just in case we forget an if statement later
    
    // Check if nudging is active, if so, calculate the forward, reverse, and neutral turn nudge amounts from the user percent.
    // Note, if we used ForwardSpeed_Max above in the formula below instead of MOTOR_MAX_FWDSPEED, the nudge amount would become a percent of our adjsuted 
    // maximum forward speed. For example in that case, if ForwardSpeed_Max was set to 70% and MotorNudgePct was set to 100%, then in effect the MotorNudgePct
    // would only be 70% in absolute terms. For now we leave it as-is, which means that it is possibe for the NudgePct to equal a drive speed greater than the 
    // actual restricted speed limit. This can look silly but the user must be responsible for choosing settings that make sense for his model, and this way gives him
    // the greatest flexibility. 
        if (eeprom.ramcopy.MotorNudgePct == 0) NudgeEnabled = false;
        else
        {
            NudgeEnabled = true;
            NudgeAmount = (uint8_t)(((float)eeprom.ramcopy.MotorNudgePct / 100.0) * (float)MOTOR_MAX_FWDSPEED);
        }

    // The user can specify a minimum speed percent below which squeaks will not occur. We convert this percent to an absolute speed number. 
        MinSqueakSpeed = (uint8_t)(((float)eeprom.ramcopy.MinSqueakSpeedPct / 100.0) * (float)MOTOR_MAX_FWDSPEED);

    // If the user enabled LVC, check the voltage every so often
        if (eeprom.ramcopy.LVC_Enabled)
        {
            EnableRoutineVoltageCheck();   
        }
        else
        {   // Just assume everything is hunky-dory
            LVC = false;
            HavePower = true;
        }

    // Display some info if we have debug set. But wait until a few seconds after we've booted so the dump doesn't interfere with any PC communication attempts. 
    // Of course, the user can also always dump the info just by pressing the input button. 
        if (SAVE_DEBUG) { timer.setTimeout(1500, DumpSysInfo); }

    // Get the time
        currentMillis = millis();
        TransitionStart = currentMillis;
    
    // We won't run through this again
        Startup = false;    

    // Turn off the Red LED to let the user know startup is complete
    RedLedOff();
//    GreenBlinkFast(1);  // Blink the green LED once - this means we are ready for business. 
} 
// End of Startup loop - it won't be run again


    // PER-LOOP UPDATES
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        PerLoopUpdates();       // Reads the input button, and updates all timers


    // CHECK THE BUTTON
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        switch (ButtonState) 
        {
            // This state watches for short and long presses, dumps debug info with a short press, 
            // or enters a special menu with a long press
            case BUTTON_WAIT:                
                if (InputButton.wasReleased())
                {   // A single press (short) of the button will:
                    // 1) Save any adjustments the user has made to EEPROM
                    // 2) Dump the system info, regardless of whether DEBUG is true or not
                    SaveAdjustments();
                    DumpSysInfo();
                }
                else if (InputButton.pressedFor(1800)) // Two seconds in real life feels like longer than two seconds, so we do 1.8
                {
                    // User has held down the input button for two seconds. We are going to enter some special routine. 
                    
                    // The DIP switch selection will determine which menu we enter.
                    switch (GetMenuNumber())
                    {
                        case 1: 
                            // Barrel elevation servo setup
                            if (eeprom.ramcopy.TurretElevationMotor == SERVO_ESC || eeprom.ramcopy.TurretElevationMotor == SERVO_PAN)
                            { SetupServo(SERVONUM_TURRETELEVATION); }
                            else
                            { 
                                if (DEBUG) DebugSerial->println(F("Turret elevation is not of type Servo. No setup available.")); 
                                // Wait for them to release the button before proceeding
                                do { delay(10); InputButton.read(); } while (!InputButton.wasReleased()); 
                            }
                            break;
                        case 2: 
                            // Reserved for future use
                            break;        
                        case 3: 
                            // Reserved for future use
                            break;
                        case 4: 
                            // Recoil servo setup
                            SetupServo(SERVONUM_RECOIL); 
                            break;
                    }
                    // All the menus above wait for the button to be released before exiting, so we can go straight from here to BUTTON_WAIT
                    ButtonState = BUTTON_WAIT;
                }
                break;

            //This is a transition state where we just wait for the user to release the button
            //before moving back to the WAIT state.
            case BUTTON_TO_WAIT:
                if (InputButton.wasReleased()) ButtonState = BUTTON_WAIT;
                break;
        }


    // PC COMMUNICATION
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        // Does the computer want to talk to us? 
        if (PCComm.CheckPC())
        {   // Yep. Stop everything, then enter listening mode. 
            StopEverything();
            PCComm.ListenToPC();
        }

        
    
    // GET IMU DATA - BNO055 9-DOF IMU
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    // Product:  https://www.adafruit.com/products/2472
    // Because we are using a non-blockin i2c library, we basically submit a request, go on to do other things, then check back to see if the request is complete.
    // EDIT: BECAUSE THE IMU IS NOT WORKING RELIABLY YET, THIS PORTION OF CODE HAS BEEN COMMENTED-OUT. HOWEVER THERE IS NOTHING WRONG WITH IT, THE ISSUE IS I2C COMMUNICATIONS.
    //       WE WILL PROBABLY HAVE TO SWITCH TO SERIAL COMS WITH THE BNO055 OVER THE SERIAL 1 PORT. 
/*    
    if (UseIMU && HavePower)
    {
        IMU_Updated = false;    // This can only be true for one loop. We always start it at false, then set it to true below if an update did occur. 
        
        if (IMU_WaitingForSample)
        {
            if (IMU.process())  // Processing is done
            {   
                // Update the reading if the request was successful and the system calibration level is greater than 0
                if (IMU.transactionSuccessful()) 
                {   
                    // You may actually want to just request accel/gyro readings directly, and fuse them yourself using a complimentary filter. 
                    // The BNO fusion works well but is constantly going in and out of calibration...
                    
                    //if (IMU.calibration.system > 0)   // Use this in 9DOF mode
                    if (IMU.calibration.gyro > 0 && IMU.calibration.accel > 0)
                    {
                        // The beauty of this device is that it provides us absolute orientation directly - the accelerometer, gyro and magnetometer data have all been fused already. 
                        // The results are floating point Euler angles - in other words, we have pitch, roll and yaw directly without any maths. 
                        // Range of Euler is 0-359 (360 degrees) 
                        rawpitch = IMU.orientation.z;             // We can use any axis for pitch so long as we install the sensor in the correct orientation. Z makes most sense from an installation standpoint. 
                        if (rawpitch >= 180) rawpitch -= 360;     // Convert angles from 0:360 to -180:180 which works better for our purposes
                        //pitch = alpha * pitch + ((1.0 - alpha) * rawpitch);   // We can apply a filter to the reading to remove glitches if necessary
                        pitch = rawpitch;
                        IMU_Updated = true;
                    }
                }
                IMU_WaitingForSample = false;                           // We got the sample, successful or not, so we are not waiting for anything
                IMU_ReadyToSample = false;                              // We are also not yet ready to sample again
                timer.setTimeout(IMU_SampleDelay, SetReadyToSample);    // Instead we wait a while before taking another sample
            }
        }
        // Request the next reading if we are ready for the next sample
        if (IMU_ReadyToSample)
        {
            IMU.requestEuler();         // Request fusion data
            IMU_ReadyToSample = false;  // While false, no further samples will be taken
            IMU_WaitingForSample = true;    // 
        }
    }
*/
        

    // ADJUST FOR HILLS - DISABLED FOR NOW
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    // THIS WAS NOT QUITE COMPLETED. IF YOU CAN GET THE IMU TO WORK, THIS CODE IS CLOSE, BUT PROBABLY NEEDS A FEW MORE CONDITIONAL CHECKS, AND MAYBE
    // SOME ADJUSTMENT ON THE RANGES. HOWEVER THE PRINCIPLE IS SOUND AND TESTED TO WORK (eg, the idea that we reduce or increase speed by temporarily 
    // manipulating the throttle channel end-points)
    /*
    if (eeprom.ramcopy.EnableHillPhysics)
    {
        if (IMU_Updated)
        {
            HillInclineRange = 120.0 - float(HillSensitivity);   // Range will be 20 to 120
            //#define MaxHillPulseSubtract 300;
    //        #define MinExtra 200
            uint16_t ThrottleAdjust;
            ThrottleAdjust = abs(int(mapf(pitch, -HillInclineRange,  HillInclineRange, -400, 400))); 
            // Case where moving forward, or moving in reverse with throttle channel reversed
            if ((DriveModeActual == FORWARD && !Radio.Sticks.Throttle.Settings->reversed) || (DriveModeActual == REVERSE && Radio.Sticks.Throttle.Settings->reversed))
            {
                if (pitch > 0) { Radio.Sticks.Throttle.Settings->pulseMax = ThrottlePulseMax + ThrottleAdjust; } // Uphill forward   - reduce speed
                else           { Radio.Sticks.Throttle.Settings->pulseMax = ThrottlePulseMax - ThrottleAdjust; } // Downhill forward - increase speed
                //Radio.Sticks.Throttle.Settings->pulseMax = max(Radio.Sticks.Throttle.Settings->pulseMax, Radio.Sticks.Throttle.Settings->pulseCenter + MinExtra);
                // Keep the other side of the scale normal because we will use it for braking
                Radio.Sticks.Throttle.Settings->pulseMin = ThrottlePulseMin;
            }
            // Case where moving in reverse, or moving forward with throttle channel reversed
            else if ((DriveModeActual == REVERSE && !Radio.Sticks.Throttle.Settings->reversed) || (DriveModeActual == FORWARD && Radio.Sticks.Throttle.Settings->reversed))
            {
                if (pitch > 0) { Radio.Sticks.Throttle.Settings->pulseMin = ThrottlePulseMin + ThrottleAdjust; } // Downhill reverse - increase speed
                else           { Radio.Sticks.Throttle.Settings->pulseMin = ThrottlePulseMin - ThrottleAdjust; } // Uphill reverse   - reduce speed
                //Radio.Sticks.Throttle.Settings->pulseMin = min(Radio.Sticks.Throttle.Settings->pulseMin, Radio.Sticks.Throttle.Settings->pulseCenter - MinExtra);
                // Keep the other side of the scale normal because we will use it for braking
                Radio.Sticks.Throttle.Settings->pulseMax = ThrottlePulseMax;            
            }
        }
    }
    else
    {   // Restore end-points
        Radio.Sticks.Throttle.Settings->pulseMin = ThrottlePulseMin;
        Radio.Sticks.Throttle.Settings->pulseMax = ThrottlePulseMax;
    }
    */
    
    
    // GET RX COMMANDS
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        Radio.GetCommands();    // Only call this once per loop, otherwise you will discard frames
        // If we have lost connection with the radio, blink some lights and wait for it to reconnect
        if (Radio.InFailsafe) { StartFailsafe(); }
        while(Radio.InFailsafe)
        {
            Radio.GetCommands();
            if (PCComm.CheckPC()) 
            {   // Temporarily disable the failsafe lights
                StopFailsafeLights();
                // Talk to the computer
                PCComm.ListenToPC();
                // But when we're done talking to the PC, restart the lights
                StartFailsafeLights();
            }
            PerLoopUpdates();    // Update timers
        }
        // We're out of failsafe - stop the blinking
        EndFailsafe();


    // GET EXTERNAL INPUTS
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
        ReadIOPorts();  // This will only do anything if the user setup the external IO pins as inputs

        
    // SET TURRET 
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    // The turret can move without the engine started, but it can't move if the tank has been destroyed (Alive == false)
    if (Alive & HavePower)
    {   
        // BARREL UP / DOWN
        // We have two different sets of code for dealing with the barrel. If barrel stabilization is enabled, we manipulate the 
        // Servo_PAN class object called "Barrel"
        // EDIT: THIS IS TESTED TO WORK WELL. IF YOU GET THE IMU TO WORK RELIABLY, YOU CAN SIMPLY UN-COMMENT THE STUFF BELOW
/*        if (eeprom.ramcopy.EnableBarrelStabilize) // This will only be enabled if the elevation motor is of the correct type (SERVO_PAN)
        {
            // Move barrel up/down in response to user commands
            if (Radio.Sticks.Elevation.updated && (Radio.Sticks.Elevation.ignore == false)) 
            {   
                Barrel->setSpeed(Radio.Sticks.Elevation.command);   
                
                // If we are changing the position of the barrel, set the position changed flag
                if (Radio.Sticks.Elevation.command != 0) BarrelPosChanged = true;
                
                // If barrel elevation sound is enabled, play or stop the sound as appropriate
                if (eeprom.ramcopy.BarrelSound_Enabled)
                {
                    Radio.Sticks.Elevation.command == 0 ? TankSound->StopBarrel() : TankSound->Barrel();                      
                }                  
            }
    
            // In addition to barrel movement commanded by the user, we will also move the barrel to keep it stable as measured by the accelerometer. 
            // But we only stabilize the barrel when the user isn't commanding a position change (hence the check for command == 0)
            if (Radio.Sticks.Elevation.command == 0 && IMU_Updated)
            {   
                // Pitch will be a number from -180 to 179 degrees, but most servos will only travel from -45 to 45. But of course some servos
                // can travel more or less, and the actual range of travel of the barrel will depend on the linkage between the servo and the barrel. 
                // We adjust sensitivy by changing the range from which the pitch is being mapped. We let the user specify a number from 1 to 100, 
                // with 100 being most sensitive. Right in the middle would be 50, which is very close to the -45/45* the sensor is likely to actually move in 
                // practice, as well as the servo travel. Lower numbers will mean less sensitivity, higher numbers will mean more sensitivity. 
                TankPitchRange = 100.0 - float(BarrelSensitivity);  // Lower pitch numbers are actually more sensitive, but we want the user to see large numbers as more sensitive.
                if (eeprom.ramcopy.TurretElevation_Reversed)
                { Barrel_Level = int(mapf(pitch,  TankPitchRange, -TankPitchRange, eeprom.ramcopy.TurretElevation_EPMin, eeprom.ramcopy.TurretElevation_EPMax)); }
                else 
                { Barrel_Level = int(mapf(pitch, -TankPitchRange,  TankPitchRange, eeprom.ramcopy.TurretElevation_EPMin, eeprom.ramcopy.TurretElevation_EPMax)); }

                
                // If BarrelPosChanged (and command = 0), we know that the user has finished setting the barrel to a new position.
                // We save the current level as measured by the accelerometer as our offset for this position. 
                if (BarrelPosChanged)
                {
                    Barrel_Offset = Barrel_Level;
                    BarrelPosChanged = false;   // Set this to false so we don't record a new offset until user changes position again. 
                }
                // Barrel_Level:                 instantaneous pulse width that would set the barrel level with the ground (in other words, a measure of the tank's inclination)
                // Barrel_Offset:                will be the servo pulse width needed to put the barrel at the actual angle to the ground the user wants, at the inclination the tank was at when the user set it. 
                // Barrel_Offset - Barrel_Level: the change needed to adjust for a change in inclination since the set point. 
                
                // We don't use setSpeed in this case, because by definition this is a pan servo, and setSpeed for pan servos only sets the rate at which they move.
                // When stabilizing the servo we need to set the position directly, so we use the setPos function of the Servo_PAN class. 
                Barrel->setPos(Barrel->fixedPos + (Barrel_Offset - Barrel_Level));
            }
        }
        // Barrel stabilization is not enabled: in this case we maninpulate the Motor object called "TurretElevation" (because in this case the motor could be anything, not necessarily a pan servo)
        else
        {
*/            
            if (eeprom.ramcopy.TurretElevationMotor != DRIVE_DETACHED)  // Only apply turret stick movements if we have specified an actual motor type
            {
                // Move barrel up/down in response to user commands
                if (Radio.Sticks.Elevation.updated && (Radio.Sticks.Elevation.ignore == false)) 
                {   
                    TurretElevation->setSpeed(Radio.Sticks.Elevation.command); 
                    // If barrel elevation sound is enabled, play or stop the sound as appropriate
                    if (eeprom.ramcopy.BarrelSound_Enabled)
                    {
                        Radio.Sticks.Elevation.command == 0 ? TankSound->StopBarrel() : TankSound->Barrel();                      
                    }                    
                }                
            }
//        }

        // TURRET LEFT / RIGHT
        // Move turret left/right if command has changed. Also check if there is a turret sound associated with this movement
        if (eeprom.ramcopy.TurretRotationMotor != DRIVE_DETACHED)  // Only apply turret stick movements if we have specified an actual motor type
        {
            if (Radio.Sticks.Azimuth.updated && (Radio.Sticks.Azimuth.ignore == false))     
            {  
                TurretRotation->setSpeed(Radio.Sticks.Azimuth.command);
                // If turret rotation sound is enabled, play or stop the sound as appropriate
                if (eeprom.ramcopy.TurretSound_Enabled)
                {
                    Radio.Sticks.Azimuth.command == 0 ? TankSound->StopTurret() : TankSound->Turret();
                }
            }
        }
    }


    // DRIVING
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    if (TankEngine.Running() && HavePower)
    {
        // GET DRIVE MODE - COMMANDED & ACTUAL
        // ---------------------------------------------------------------------------------------------------------------------------------------------->
        // This runs faster than the radio updates - so skip it entirely if nothing new has come in
        if (Radio.Sticks.Throttle.updated || Radio.Sticks.Turn.updated)
        {    
            // We set Throttle (engine speed) and Drive (wheel speed) equal to begin with, but they will diverge as we proceed. 
            DriveCommand = ThrottleCommand = Radio.Sticks.Throttle.command;
            TurnCommand = Radio.Sticks.Turn.command;

            if (WasRunning == false) {WasRunning = true;}    // Means, we just started the engine running
            
            // Get drive mode command
            if (!TankTransmission.Engaged())
            {   // If the transmission is not engaged, we can't command any more movement. But if it was disengaged while coasting,
                // we allow opposite command so the user can still brake. 
                DriveModeCommand = DriveModeActual;
                switch (DriveModeActual)
                {   case FORWARD:
                        if (DriveCommand >= 0)
                        {   DriveCommand = 0; }
                        else
                        {   // In this case they are trying to brake, which we allow. Throttle speed goes to zero.
                            DriveModeCommand = REVERSE;
                            ThrottleCommand  = 0;
                        }
                        break;
                    case REVERSE:
                        if (DriveCommand < 0)
                        {   DriveCommand = 0; }
                        else
                        {   // In this case they are trying to brake, which we allow. Throttle speed goes to zero.
                            DriveModeCommand = FORWARD;
                            ThrottleCommand = 0;
                        }
                        break;
                    default:
                        DriveCommand = 0; 
                        TurnCommand = 0;
                        DriveModeCommand = STOP;
                        DriveSpeed = 0;
                        ThrottleSpeed = 0;
                        TurnSpeed = 0;
                }
            }
            else
            {   // If the transmission is engaged, we just get drive mode *command* from the actual command
                DriveModeCommand = Driver.GetDriveMode(DriveCommand, TurnCommand); 
            }
            
            // Check commanded mode against actual mode
            if (DriveModeCommand != DriveModeActual)
            {
                switch (DriveModeActual)
                {
                    case STOP:
                        // Because the radio signal isn't steady, at stop we may be getting what appear to be commands to move, but in fact are just glitches.
                        // We can eliminate some of that by the 'deadband' setting for the throttle and steering channels, but at too high a level this also reduces stick sensitivity. 
                        // Another approach is to only count a new drive mode command if it shows up multiple times in a row (ModeChangeLimit). If the command comes in fewer 
                        // times than that, we ignore it. 
                        if (++ModeChangeCount < ModeChangeLimit) DriveModeCommand = STOP;    // Cancel the change
                        else
                        {   
                            // Ok, we have enough times in a row to allow a transition, but now we need to check it against the transition time constraint. 
                            // This is a user-setting that limits how quicky the tank can change directions, for example from forward to reverse
                            if (eeprom.ramcopy.TimeToShift_mS > 0) 
                            {
                                if (DriveFlag == false && (((millis() - TransitionStart) >= eeprom.ramcopy.TimeToShift_mS) || (DriveMode_LastDirection == DriveModeCommand)))
                                {   // Enough time at stop has passed that we allow a change to another drive mode (or we are going in the same direction we were previously)
                                    DriveFlag = true;
                                }
                            }
                            else
                            {   // If the user set TimeToShif_mS = 0 then there is no time constraint
                                DriveFlag = true;
                            }
                            if (DriveFlag)
                            {
                                DriveModeActual = DriveModeCommand;                             // Allow the transition
                                // Since we are just now starting to move from a stop, give the motors a nudge if the nudge effect is enabled.
                                if (NudgeEnabled)
                                {                                                                
                                    NudgeStarted = true;                                        // Set a flag so we know it first began.
                                    Nudge = true;                                               // Since we are just starting from a stop, we set the Nudge flag
                                    timer.setTimeout(eeprom.ramcopy.NudgeTime_mS, Nudge_End);   // We will quit nudging after some period of time
                                }
                            }
                        }
                        break;
        
                    case NEUTRALTURN:
                        // In a neutral turn, we ignore throttle commands
                        DriveCommand = 0;
                        break;
        
                    default:
                        // We are either moving FORWARD or REVERSE, and we are commanding something different
        
                        // We could be commanding the opposite direction, in which case, we are braking
                        Braking = Driver.GetBrakeFlag(DriveModeActual, DriveModeCommand);
                                             
                        if (Braking && eeprom.ramcopy.TimeToShift_mS == 0 && Driver.isDecelRampEnabled() == false && Driver.isAccelRampEnabled() == false)
                        {   // If brake flag is set we know we are presently going either forward or reverse and we have just commanded the opposite direction.
                            // If TimeToShift_mS has been disabled (set to 0), and if there are no deceleration or acceleration constraints set, then allow the change directly without
                            // coming to a stop first. This is not good for your gearboxes but if someone wants to do it, we allow them. 
                            Braking = false;                    // Clear the brake flag, we are not going to brake, we are going to slam directly into the opposite direction
                            DriveModeActual = DriveModeCommand; // Set actual to command. 
                        }
                        
                        // If we are not braking, we are moving forward/reverse and are commanding a STOP or a NEUTRAL TURN.
                        // Either way, we ignore the new *mode* for now, but proceed with the actual throttle command, which in fact will be zero (no throttle command at stop or neutral turn).
                        // We will set the DriveModeActual to STOP or NEUTRAL TURN once we have actually stopped or the neutral turn is allowed. 
                }
            }
            else
            {   // In this case DriveModeCommand == DriveModeActual 
                // We can proceed as normal, but clear the brake flag
                Braking = false;

                ModeChangeCount = 0;    // Reset this
            }

            // Let's set the brake lights
            Braking ? BrakeLightsOn() : BrakeLightsOff();

        }  // End radio update check          


        // TURN SCALING
        // ---------------------------------------------------------------------------------------------------------------------------------------------->        
        // Neutral Turn
        if (eeprom.ramcopy.DriveType == DT_TANK && DriveModeActual == NEUTRALTURN)
        {   // Neutral Turn - if we are in a neutral turn (only for tanks), scale the turn command to the max neutral turn speed allowed. 
            TurnSpeed = Driver.ScaleTurnCommand(TurnCommand, NeutralTurn_Max); 

            // If we are just starting to move from a stop, and the nudge effect is enabled, we want to immediately set 
            // the drive motors to a pre-determined minimum amount. 
            if (NudgeStarted)
            {
                if      (TurnSpeed > 0) TurnSpeed_Previous =  NudgeAmount;
                else if (TurnSpeed < 0) TurnSpeed_Previous = -NudgeAmount;
                NudgeStarted = false;   // We only do this at the start, so set this to false. 
            }    

            // So long as the nudge flag is active (user determines how long it lasts), we don't let drive speed fall below the minimium set nudge amount.
            // Since this is a neutral turn, our drive speed is actually our TurnSpeed
            if (Nudge)
            {    
                if      (TurnSpeed > 0) TurnSpeed = max(TurnSpeed,  NudgeAmount);
                else if (TurnSpeed < 0) TurnSpeed = min(TurnSpeed, -NudgeAmount);
            }
                    
        }
        // Scaled turn for half-track vehicles
        else if (eeprom.ramcopy.DriveType == DT_HALFTRACK)
        {   // In halftrack mode, we limit the amount of turn command that gets applied to the rear treads (but 100 percent of turn will always go to the steering servo)
            TurnSpeed = Driver.ScaleTurnCommand(TurnCommand, HalftrackTurn_Max);
        }
        // Regular turning
        else (TurnSpeed = TurnCommand);


        // GET DRIVE SPEED
        // ---------------------------------------------------------------------------------------------------------------------------------------------->
        // Now get our drive speed
        if (DriveModeActual != NEUTRALTURN && DriveModeActual != STOP) 
        {   // We're going to start manipulating drive speed so we will use the DriveSpeed variable rather than DriveCommand, because we want DriveCommand (and DriveCommand_Previous)
            // to accurately reflect the actual command. 
            
            // Apply any user speed limitations to forward and reverse. We do this by scaling the command to the range the user specifies. 
            // Why not simply adjust the internal speed range of the motor object? That is a great idea, but unfortunately, because forward and reverse can have different
            // max speeds, limiting the motor object will cause uneven motor speed in Neutral Turns - for example, the tread turning in reverse will move slower than the tread
            // moving forward. We would have to keep changing the internal speed range on the fly depending on if we were moving forward, reverse, or in a neutral turn. 
            // It probably wouldn't be any more work really than what we've ended up doing which is adjusting the command, but that's how we're doing it. 
            // And no, this will NOT hinder the ability of the sound object from reaching full speed even with limited motor speeds. The engine sound speed is based off 
            // ThrottleCommand which we do not limit the way we are now with DriveCommand. 
            if (DriveModeActual == FORWARD && eeprom.ramcopy.MaxForwardSpeedPct < 100)
            {
                DriveSpeed = map(DriveCommand, 0, MOTOR_MAX_FWDSPEED, 0, ForwardSpeed_Max);
            }
            else if (DriveModeActual == REVERSE && eeprom.ramcopy.MaxReverseSpeedPct < 100)
            {
                DriveSpeed = map(DriveCommand, 0, MOTOR_MAX_REVSPEED, 0, ReverseSpeed_Max);  
            }
            else
            {
                DriveSpeed = DriveCommand;
            }
            
            // If we are just starting to move from a stop, and the nudge effect is enabled, we want to immediately set the drive motors
            // to a pre-determined minimum amount. To prevent the driver class from ramping up to this level, we artificially assign
            // this starting level to the DriveSpeed_Previous variable, so there will be no ramping required to reach it (ramping
            // is used to change speed from the prior amount to the current amount, but if it thinks the prior amount is already at the level
            // we want to be, it won't need to ramp)
            if (NudgeStarted)
            {
                if      (DriveModeActual == FORWARD) DriveSpeed_Previous =  NudgeAmount;
                else if (DriveModeActual == REVERSE) DriveSpeed_Previous = -NudgeAmount;
                NudgeStarted = false;   // We only do this at the start, so set this to false. 
            }    

            // So long as the nudge flag is active (user determines how long it lasts), we don't let throttle command fall below the minimium set nudge amount
            if (Nudge)
            {    
                if      (DriveModeActual == FORWARD) DriveSpeed = max(DriveSpeed,  NudgeAmount); 
                else if (DriveModeActual == REVERSE) DriveSpeed = min(DriveSpeed, -NudgeAmount); 
            }

            // Ok, DriveSpeed finally - GetDriveSpeed() primarily applies any acceleration/deceleration constraints. Remember, DriveSpeed is the speed of the vehicle.
            DriveSpeed = Driver.GetDriveSpeed(DriveSpeed, DriveSpeed_Previous, DriveModeActual, Braking);
        }
        else
        {   // This is a neutral turn
            DriveSpeed = 0; // Neutral turns ignore drive speed
            // If enabled, we apply acceleration ramping to TurnSpeed (speed of neutral turn). Deceleration ramping will automatically be ignored for neutral turns, even if enabled (it looks silly)
            TurnSpeed = Driver.GetDriveSpeed(TurnSpeed, TurnSpeed_Previous, DriveModeActual, Braking);
        }

        // GET AND SET THROTTLE SPEED
        // ---------------------------------------------------------------------------------------------------------------------------------------------->
        // Now we also calculate the throttle (engine speed). This is not the same as the DriveSpeed! Throttle speed can be different from drive speed for various effects. 
        // DriveSpeed is fed to the tank motors. ThrottleSpeed is fed to the sound system and the smoker. 

        // Rather than just pass the raw throttle command to the sound/smoker, we can modify it slightly depending on the rate of increase, etc...
        // This lets us activate an acceleration sound effect, let the throttle decrease slowly (so engine sound doesn't go directly to idle from full speed), etc... 
        if (DriveModeActual==NEUTRALTURN)
        {   // In this case, there is no drive speed, instead we pass the turn command (which is actually our drive speed in a neutral turn)
            ThrottleSpeed = Driver.GetThrottleSpeed(TurnCommand, ThrottleSpeed_Previous, TurnCommand, DriveModeActual, Braking); 
        }
        else
        {   
            // If the transmission is engaged but DriveSpeed is 0, we set throttle command = 0 no matter what. It is possible for the throttle command to be some value greater than 0
            // but so long as drive speed = 0, the tank won't be moving, and if we allow the throttle to rev then the sound won't be synchronized with the movement. Why can throtle command
            // be high even though speed is 0? Because when the tank comes to a stop, there is a transition timer that prevents it from moving again in a different direction until the timer
            // completes. The length of this transition timer can be set by the user, and the purpose is to prevent damaging the gearboxes for example by going directly into reverse from 
            // forward. But while this timer is running, the drive command will have no effect, so we also force throttle command to stay at 0 too. 
            // However, if the the transmission is *not* engaged, we allow the user to rev away all he wants. 
            if (TankTransmission.Engaged() && DriveSpeed == 0) { ThrottleCommand = 0; }
            
            // Now we calculate a throttle speed based on the command and other parameters
            ThrottleSpeed = Driver.GetThrottleSpeed(ThrottleCommand, ThrottleSpeed_Previous, DriveSpeed, DriveModeActual, Braking); 
        }

        // Now pass the throttle speed to the sound unit and the smoker 
        if (ThrottleSpeed != ThrottleSpeed_Previous)    // But only if the command has changed from last time...
        {
                TankSound->SetEngineSpeed(ThrottleSpeed);    // Sound unit speed
                SetSmoker_Speed(ThrottleSpeed);              // Smoker speed
        }

        // SET DRIVE SPEED 
        // ---------------------------------------------------------------------------------------------------------------------------------------------->
        // Decide if we've come to a stop, if so, disable driving and start the shift timer
        if ((DriveModeActual == NEUTRALTURN && TurnCommand == 0) || (DriveModeActual != NEUTRALTURN && DriveSpeed == 0))
        {   
            DriveModeActual = STOP;
            if (DriveMode_Previous != STOP) 
            { 
                DriveMode_LastDirection = DriveMode_Previous;    // We will use this to allow starts in the same direction without waiting for shift timer
                DriveFlag = false;                               // Disable driving for TimeToShift_MS
                TransitionStart = millis();                      // Start shift timer
                // Kill the motor(s)
                switch (eeprom.ramcopy.DriveType)
                {
                    case DT_TANK:       { RightTread->stop(); LeftTread->stop();     }  break;
                    case DT_HALFTRACK:  { RightTread->stop(); LeftTread->stop();     }  break;
                    case DT_CAR:        { DriveMotor->stop();                        }  break;
                    case DT_DKLM:       { DriveMotor->stop(); SteeringMotor->stop(); }  break;
                    default:                                                            break;
                }
            }
            
            // We're not moving, so stop the squeaking
            TankSound->StopSqueaks();

            // If the user set brake lights to come on automatically at stop, turn them on - but only if the engine is running,
            // which by definition it should be if we are at this point in the code
            if (eeprom.ramcopy.BrakesAutoOnAtStop) { BrakeLightsOn(); }
        }
        else
        {
            // We are moving 
            switch (eeprom.ramcopy.DriveType)
            {
                case DT_TANK:       
                case DT_HALFTRACK:
                    // In this case we have independent tread speeds 
                    // Now we mix the throttle and turn outputs to arrive at individual motor commands. 
                    // RightSpeed and LeftSpeed are passed by reference and updated by the function
                    Driver.MixSteering(DriveSpeed, TurnSpeed, &RightSpeed, &LeftSpeed);  
    
                    //Finally! We send the motor commands out to the motors, but only if something has changed since last time. 
                    if ((RightSpeed_Previous != RightSpeed) || (LeftSpeed_Previous != LeftSpeed))
                    {
                        RightTread->setSpeed(RightSpeed);
                        LeftTread->setSpeed(LeftSpeed);
                        RightSpeed_Previous = RightSpeed;    // Save these for next time
                        LeftSpeed_Previous = LeftSpeed;
                    }
                    // In the case of halftracks with steering servos, we set the servo a bit later, outside of this code block because we want steering control even when the engine is not running
                    break;

                case DT_CAR:
                    // In this case, there is only a single rear axle speed, and no such thing as neutral turns. 
                    // Send the signal if the DriveSpeed has changed
                    if (DriveSpeed_Previous != DriveSpeed)
                    {
                        DriveMotor->setSpeed(DriveSpeed);
                    }
                    // We set the front wheel steering servo a bit later, outside of this code block because we want servo steering control even when the engine is not running
                    break;

                case DT_DKLM:
                    // In this case the gearbox physically "mixes" turning, we just need to send it a drive speed for the single propulsion motor
                    if (DriveSpeed_Previous != DriveSpeed)
                    {
                        DriveMotor->setSpeed(DriveSpeed);
                    }
                    // And a steering command for the steering motor
                    if (TurnCommand_Previous != TurnCommand)
                    {
                        SteeringMotor->setSpeed(TurnCommand);   // We send TurnCommand rather than TurnSpeed because we don't want any scaling effects applied in this case. TurnCommand is just equal to the stick input. 
                    }
                    break;                    
            }

            // Other checks to do when we're moving: 

            // Start squeaking if we haven't already. The sound object will automatically ignore any squeaks the user disabled in settings. 
            if (TankSound->AreSqueaksActive() == false && abs(DriveSpeed) >= MinSqueakSpeed) { TankSound->StartSqueaks(); }
            else if (TankSound->AreSqueaksActive() && abs(DriveSpeed) <= MinSqueakSpeed)     { TankSound->StopSqueaks();  }

            // If the user set brake lights to come on automatically at stop, turn them off now, because we are no longer stopped
            if (eeprom.ramcopy.BrakesAutoOnAtStop && BrakeLightsActive) { BrakeLightsOff(); }

            // If we are moving, keep the idle timer going. Once we come to a stop we'll quit updating it, and if the user has EngineAutoStopTime_mS set to something more than 0 (meaning, they want the 
            // engine to automatically turn off after a certain amount of time sitting stopped at idle), then when the timer expires it will turn the engine off. UpdateEngineIdleTimer() also starts the timer
            // if this is the first time we're calling it. 
            if (DriveModeActual != STOP)   { UpdateEngineIdleTimer(); }
        }
    }
    else // In this case the engine is stopped
    {
        if (WasRunning) // Means, we were running last time we checked
        {   // So turn this stuff off
            DriveSpeed = 0;
            ThrottleSpeed = 0;
            TurnSpeed = 0;
            RightSpeed = 0;
            LeftSpeed = 0;
            switch (eeprom.ramcopy.DriveType)
            {
                case DT_TANK:       { RightTread->stop(); LeftTread->stop();     } break;
                case DT_HALFTRACK:  { RightTread->stop(); LeftTread->stop();     } break;
                case DT_CAR:        { DriveMotor->stop();                        } break;
                case DT_DKLM:       { DriveMotor->stop(); SteeringMotor->stop(); } break;
                default:                                                           break;
            }
            DriveModeActual = STOP;
            Braking = false;
            BrakeLightsOff();      // Don't leave the brake lights on when the engine stops
            WasRunning = false;    // The engine is no longer running
            TankSound->StopSqueaks(); 
            StopEngineIdleTimer();  // Stop the timer that turns off the engine after a set amount of time, since the engine is now already off. 
        }
    }

    // FRONT WHEEL SERVO STEERING 
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    // If this is a car or halftrack, we always allow front wheel steering via servo movement regardless of whether the engine is running or not, 
    // or even whether the tank is destroyed or not (but not when we are in LVC mode or the battery is unplugged)
    // The servo command is just passed through directly from the radio. 
    if (HavePower && (eeprom.ramcopy.DriveType == DT_HALFTRACK || eeprom.ramcopy.DriveType == DT_CAR))
    {
        // The servo object knows that "setSpeed" actually means "set servo position." 
        SteeringServo->setSpeed(Radio.Sticks.Turn.command);   
    }


    // RUN SPECIAL FUNCTIONS - But only if tank hasn't been destroyed, and if the battery voltage level is sufficient
    // -------------------------------------------------------------------------------------------------------------------------------------------------->
    if (Alive && HavePower)
    {
        for (uint8_t t=0; t<triggerCount; t++)
        {
            // Check for any trigger matching the current turret stick position
            if (Radio.UsingSpecialPositions && Radio.SpecialStick.updated && (eeprom.ramcopy.SF_Trigger[t].TriggerID == Radio.SpecialStick.Position)) { SF_Callback[t](0); }
    
            // Check for any trigger matched to current aux channel switch positions. Aux channel IDs are set by the formula: 
            // (trigger_id_multiplier_auxchannel * Aux Channel Number) + (number of switch positions * switch_pos_multiplier) + Switch Position
            for (uint8_t a=0; a<AUXCHANNELS; a++)
            {   // Digital aux channel triggers
                if (Radio.AuxChannel[a].Settings->Digital && 
                    Radio.AuxChannel[a].updated && 
                    (eeprom.ramcopy.SF_Trigger[t].TriggerID == (trigger_id_multiplier_auxchannel * (a+1)) + (switch_pos_multiplier * Radio.AuxChannel[a].Settings->numPositions) + Radio.AuxChannel[a].switchPos))
                    {
                        SF_Callback[t](0);
                    }
                // Anallog aux channel triggers
                if (Radio.AuxChannel[a].Settings->Digital == false &&
                    Radio.AuxChannel[a].updated &&
                    (eeprom.ramcopy.SF_Trigger[t].TriggerID == (trigger_id_multiplier_auxchannel * (a+1))))
                    {
                        SF_Callback[t](ScaleAuxChannelPulse_to_AnalogInput(a));
                    }
            } 

            // Check for any trigger associated with external inputs on I/O pins A or B. This will only apply if the user set these to input (they have the option of being outputs as well). 
            for (uint8_t io=0; io<NUM_IO_PORTS; io++)
            {   // FYI - dataDirection == 0 means "input"
                // The user can specify "digital" input (values converted to 1/0) 
                if (IO_Pin[io].Settings.dataDirection == 0 &&
                    IO_Pin[io].Settings.Digital && 
                    IO_Pin[io].updated &&
                    (eeprom.ramcopy.SF_Trigger[t].TriggerID == (trigger_id_multiplier_ports * (io+1)) + IO_Pin[io].inputValue))
                    {
                        SF_Callback[t](0);
                    }
                // Or the user can also keep this as an analog input
                if (IO_Pin[io].Settings.dataDirection == 0 &&
                    IO_Pin[io].Settings.Digital == false && 
                    IO_Pin[io].updated &&
                    (eeprom.ramcopy.SF_Trigger[t].TriggerID == (trigger_id_multiplier_ports * (io+1))))
                    {
                        SF_Callback[t](IO_Pin[io].inputValue);
                    }
            }
        }
        
        // Finally, sort of a one-off trigger: the user has the option of starting the engine with the throttle channel
        if (eeprom.ramcopy.EngineAutoStart && TankEngine.Running() == false && Radio.Sticks.Throttle.command > 126) // We check for throttle command greater than half
        {
            EngineOn();
        }
    }


    // BATTLE 
    // ------------------------------------------------------------------------------------------------------------------------------------------------>  
    // Were we hit? 
    if (HavePower && Alive) HitType = Tank.WasHit();
    else                    HitType = HIT_TYPE_NONE;
    
    if (HitType != HIT_TYPE_NONE)
    {
        // We were hit. But was it a damaging hit, or a repair hit? 
        switch (HitType)
        {
            case HIT_TYPE_CANNON: 
                if (DEBUG) 
                { 
                    DebugSerial->print(F("CANNON HIT! (")); 
                    DebugSerial->print(ptrIRName(Tank.LastHitProtocol()));
                    if (Tank.LastHitTeam() != IR_TEAM_NONE) DebugSerial->print(ptrIRTeam(Tank.LastHitTeam()));
                    DebugSerial->println(F(")"));
                    // Were we in the middle of a repair?
                    if (RepairOngoing) { DebugSerial->println(F("REPAIR OPERATION CANCELLED")); }
                }
                if (RepairOngoing) { RepairOngoing = REPAIR_NONE; }   // End repair if we were in the middle of one
                break;
            case HIT_TYPE_MG:
                if (DEBUG) 
                { 
                    DebugSerial->print(F("MACHINE GUN HIT! (")); 
                    DebugSerial->print(ptrIRName(Tank.LastHitProtocol()));
                    DebugSerial->println(F(")"));                    
                    // Were we in the middle of a repair?
                    if (RepairOngoing) { DebugSerial->println(F("REPAIR OPERATION CANCELLED")); }
                }
                if (RepairOngoing) { RepairOngoing = REPAIR_NONE; }   // End repair if we were in the middle of one
                break;
            case HIT_TYPE_REPAIR:
                if (!RepairOngoing && Tank.isRepairOngoing()) 
                {                   
                    // This marks the start of a repair operation
                    RepairOngoing = REPAIR_SELF;    // Repair self, meaning, we are the one being repaired
                    if (DEBUG) 
                    { 
                        DebugSerial->print(F("VEHICLE REPAIR STARTED (")); 
                        DebugSerial->print(ptrIRName(Tank.LastHitProtocol()));
                        DebugSerial->println(F(")"));                    
                    }
                    
                    // Disengage the transmission - we will keep it in neutral until the repair is over. If the tank is moving when the repair operation starts,
                    // it will coast to a stop. 
                    // Even if the user tries to re-engage it, the function will check if a repair is ongoing, if so, it won't do anything. 
                    // And without an engaged transmission, the tank will not move. 
                    TransmissionDisengage();
                }
                break;                
        }

        // Now apply "damage", it doesn't matter if we were "damaged" or "repaired". The Damage function will take into account
        // the current amount of damage. If we were repaired, the amount of damage will be less than before, so the Damage function will 
        // calculate a new, lesser damage. Of course if we were hit by cannon or machine gun fire, then Damage will calculate a new, more severe damage. 
        if (eeprom.ramcopy.DriveType == DT_TANK || eeprom.ramcopy.DriveType == DT_HALFTRACK)
        {
            // In this case, we have two treads. Either can be damaged. 
            // If this is a halftrack there is also a steering servo, which for now we don't bother damaging. 
            Tank.Damage(RightTread, LeftTread, TurretRotation, TurretElevation, Smoker, eeprom.ramcopy.SmokerControlAuto, eeprom.ramcopy.DriveType);
        }
        else if (eeprom.ramcopy.DriveType == DT_CAR)
        {
            // In this case, there is a single rear axle and a steering servo. We allow both to be damaged.
            Tank.Damage(DriveMotor, SteeringServo, TurretRotation, TurretElevation, Smoker, eeprom.ramcopy.SmokerControlAuto, eeprom.ramcopy.DriveType);
        }
        else if (eeprom.ramcopy.DriveType == DT_DKLM)
        {
            // In this case, there is a single propulsion motor as well as a separate steering motor. We allow both to be damaged.
            Tank.Damage(DriveMotor, SteeringMotor, TurretRotation, TurretElevation, Smoker, eeprom.ramcopy.SmokerControlAuto, eeprom.ramcopy.DriveType);
        }        
        // Force treads to update
        RightSpeed_Previous = 0;
        LeftSpeed_Previous = 0;

        // Now show the remaining health level if this was a damaging hit (not a repair hit)
        if (DEBUG && HitType != HIT_TYPE_REPAIR) { DebugSerial->print(F("Health Level: ")); DebugSerial->print(Tank.PctHealthRemaining()); DebugSerial->println(F("%")); }

        if (Tank.isDestroyed && Alive)
        {
            if (DEBUG) { DebugSerial->println(F("TANK DESTROYED")); }
            Alive = false;
            StopEverything();
            // But we might not want to stop the smoker, the user can set it to run while the tank is destroyed. 
            Smoker_SetDestroyedSpeed();
        }
    }


    // Were we in a repair operation, and now is it complete? 
    if (RepairOngoing && !Tank.isRepairOngoing()) 
    {                   
        if (DEBUG && REPAIR_SELF) // Only show our health level if we were the one being repaired (as opposed to repairing someone else)
        { 
            DebugSerial->println(F("VEHICLE REPAIR COMPLETE")); 
            DebugSerial->print(F("Health Level: ")); DebugSerial->print(Tank.PctHealthRemaining()); DebugSerial->println(F("%")); 
        }  
        // Tank repair is over. 
        RepairOngoing = REPAIR_NONE;        
        // Repair sound was started automatically, but for complicated reasons we need to shut it off manually
        TankSound->StopRepairSound();
        // If the engine is still running, as  courtesy to the user let's automatically re-engage the transmission. 
        TransmissionEngage();
    }

    
    // Were we destroyed and now are recovered? 
    if (!Alive && !Tank.isDestroyed && HavePower)
    {   // We're now alive
            Alive = true;
        // While battling, the motors had their speed range cut to simulate damage. Now restore to full range - this doesn't actually set a speed, it just restores the speed *range* 
        // The actual motors will differ depending on the vehicle type. 
            if (eeprom.ramcopy.DriveType == DT_TANK || eeprom.ramcopy.DriveType == DT_HALFTRACK)
            {
                RightTread->restore_Speed();
                LeftTread->restore_Speed();
                if (eeprom.ramcopy.DriveType == DT_HALFTRACK) SteeringServo->restore_Speed();
            }
            else // Car or DKLM
            {
                DriveMotor->restore_Speed();
                if (eeprom.ramcopy.DriveType == DT_CAR)  SteeringServo->restore_Speed();
                if (eeprom.ramcopy.DriveType == DT_DKLM) SteeringMotor->restore_Speed();
            }
            TurretRotation->restore_Speed();
            TurretElevation->restore_Speed();
            Smoker_RestoreSpeed();
        // To let the user know the tank is restored, we start the engine
            EngineOn();
            if (DEBUG) { DebugSerial->println(F("TANK RESTORED")); }
    }


// ====================================================================================================================================================>
//  DEBUGGING
// ====================================================================================================================================================>        
    if (HavePower)
    {
        boolean UpdateDebugLEDs = false;
        
        // Braking is not a drive mode so we check it separately
        // While braking, both the Red and Green LEDs are On. 
        //if (Braking == true && DriveModeActual != STOP)   { RedLedOn(); GreenLedOn(); }
        if (Braking) { RedLedOn(); GreenLedOn(); } else { RedLedOff(); GreenLedOff(); }
        if (DEBUG && Braking && !Braking_Previous) DebugSerial->println(F("Braking"));
        
        // Notify the user via lights and the debug port (if enabled) of the tank's current direction of travel
        switch (DriveModeActual)
        {
            case FORWARD: 
                // When moving forward, the Green LED is on an the Red LED is Off
                if (!Braking)
                {
                    RedLedOff(); 
                    GreenLedOn();
                    // We only print a message if the mode has changed, not every loop. This is the same for all the rest of the cases below. 
                    if (DEBUG && DriveModeActual != DriveMode_Previous) { DebugSerial->println(F("Moving Forward")); }
                }
                break;
            
            case REVERSE: 
                // When moving in reverse, the Red LED is on an the Green LED is Off
                if (!Braking)
                {
                    RedLedOn(); 
                    GreenLedOff(); 
                    if (DEBUG && DriveModeActual != DriveMode_Previous) { DebugSerial->println(F("Moving Reverse")); }
                }
                break;
                
            case NEUTRALTURN: 
                // In a left neutral turn the Red LED blinks slowly and the Green LED is off.
                // In a right neutral turn, the Green LED blinks slowly and the Red LED is off.
                if (TurnSpeed > 0)  // Right turn
                {
                    if (RedBlinker != 0)   { StopBlinking(RedBlinker); RedBlinker = 0; }
                    RedLedOff();
                    if (GreenBlinker == 0) { GreenBlinker = StartBlinking_ms(pin_GreenLED, 1, 400); }
                }
                else    // Left turn
                {
                    if (GreenBlinker != 0) { StopBlinking(GreenBlinker); GreenBlinker = 0; }
                    GreenLedOff();
                    if (RedBlinker == 0) { RedBlinker = StartBlinking_ms(pin_RedLED, 1, 400); }
                }
                // Only print a message if we are just now starting a neutral turn, not every time through the loop
                if (DEBUG && DriveMode_Previous != NEUTRALTURN) { DebugSerial->println(F("Neutral Turn")); }
                break;
            
            case STOP: 
                // When stopped both LEDs are off. But we let the user keep the brake lights on manually by holding brake even after they've stopped. 
                if (!Braking)
                {                
                    RedLedOff(); 
                    GreenLedOff();
                    if (DEBUG && DriveModeActual != DriveMode_Previous) { DebugSerial->println(F("Stopped")); }
                }
                break;
        }

        if (DriveModeActual != NEUTRALTURN)
        {   // If we aren't in a neutral turn, cancel the blinkers
            if (GreenBlinker != 0) { StopBlinking(GreenBlinker); GreenBlinker = 0; }
            if (RedBlinker != 0)   { StopBlinking(RedBlinker); RedBlinker = 0; }
        }
    }
    

// ====================================================================================================================================================>
//  SAVE COMMANDS FOR NEXT ITERATION
// ====================================================================================================================================================>    
    // Set previous variables to current
    DriveModeCommand_Previous = DriveModeCommand;
    DriveMode_Previous = DriveModeActual;
    DriveSpeed_Previous = DriveSpeed;
    TurnSpeed_Previous = TurnSpeed;
    Braking_Previous = Braking;

    ThrottleCommand_Previous = ThrottleCommand;
    ThrottleSpeed_Previous = ThrottleSpeed;
    TurnCommand_Previous = TurnCommand;
}







