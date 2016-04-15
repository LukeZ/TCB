/* OP_PCComm.cpp    Open Panzer PC Communication - a library for talking to an application running on a PC
 * Source:          openpanzer.org              
 * Authors:         Luke Middleton
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
 *
 */ 

#include <OP_PCComm.h>


// Static variables must be declared outside the class
OP_EEPROM       * OP_PCComm::_op_eeprom;
HardwareSerial  * OP_PCComm::_serial;
OP_Radio        * OP_PCComm::_radio;
OP_SimpleTimer    OP_PCComm::commTimer;
int               OP_PCComm::watchdogTimerID;
boolean           OP_PCComm::Timeout;
boolean           OP_PCComm::Disconnect;
boolean           OP_PCComm::eepromUpdated;
boolean           OP_PCComm::CRCRequired;
int               OP_PCComm::numErrors;
DataSentence      OP_PCComm::SentenceIN;


//------------------------------------------------------------------------------------------------------------------------>>
// CONSTRUCT, BEGIN, MISC
//------------------------------------------------------------------------------------------------------------------------>>
OP_PCComm::OP_PCComm(void) {}                                       // Constructor

void OP_PCComm::begin(OP_EEPROM * opeeprom, OP_Radio * radio)       // Begin
{
    _op_eeprom = opeeprom;
    _radio = radio;
    _serial = &DEFAULT_SERIAL_PORT; // Initialize to default set in OP_PCComm.h
    watchdogTimerID = 0;
    Timeout = false;
    Disconnect = false;
    eepromUpdated = false;
    numErrors = 0;
    
    // We default to CRC being required, but the user can turn it off using skipCRC()
    CRCRequired = true; 
}



//------------------------------------------------------------------------------------------------------------------------>>
// LISTEN
//------------------------------------------------------------------------------------------------------------------------>>

// Before we go into full-blown listening/communication mode, we check for the special starting string
// This string consists of the three characters: 'OPZ' (or whatever is defined in INIT_STRING)
// If we get that, we know the computer wants to communicate, so the main sketch will then call ListenToPC()
boolean OP_PCComm::CheckPC(void)
{
char CharIn;
static char input_line[VALUE_BUFF];
static char init_array[VALUE_BUFF];
const String Init = INIT_STRING;            // The string we're looking for
const uint8_t InitLength = Init.length();   // Number of characters in INIT_STRING
static boolean startCharReceived = false;   // Have we got a character that matches the first character of INIT_STRING? 
static uint8_t numBytes = 0;                // Number of bytes received once we get a character match
boolean returnval = false;                  // Not static, starts false every time we enter this routine

    while (_serial->available())
    {
        CharIn = _serial->read();
        if (CharIn == INIT_STRING[0])
        {
            startCharReceived = true;   // First char received
            input_line[0] = CharIn;     // Save it in our array
            numBytes = 1;               // Subsequent bytes will be added to the array until we have enough to compare against INIT_STRING
        }
        else if (startCharReceived)
        {
            input_line[numBytes++] = CharIn;
            if (numBytes >= InitLength) break;  // We have enough bytes after the first char to do a comparison with INIT_STRING, so break and do so
        }
    }

    if (numBytes >= InitLength)
    {
        Init.toCharArray(init_array, VALUE_BUFF);
        returnval = true;   // Start off true, if any characters don't match, we'll change to false
        for (int i=0; i<InitLength; i++)
        {
            if (input_line[i] != INIT_STRING[i])
            {
                returnval = false;
            }
        }
        
        // Start everything over
        input_line[0] = '\0';
        startCharReceived = false;
        numBytes = 0;
    }
    
    // If nothing, return false
    return returnval;
}


// Input sentences come as Command DELIMITER Address DELIMITER Value DELIMITER Checksum NEWLINE
// In other words, there will always be four numbers, separated by three special delimiter symbols and terminated by a newline character.
// Commands are always 1 byte long, unsigned, meaning they will be some number between 0 and 255
// Addresses are always 2 bytes, unsigned, meaning addresses can be some number between 0 and 65,535
// Values can be numbers that fit into byte, char, int8, int16, or int32. They can be signed but the negative symbol must be the first character
//        Note! the number of bytes received for the value may be many more than the number of bytes it takes to store the actual number. 
//        The number 65,535 is 5 digits long, meaning 5 bytes received over serial, but the number itself can fit into a 2-byte unsigned integer
// Checksum will be a checksum of all bytes up to and including the last *delimiter*.

// This is the main communication function. It loops over and over, waiting for and reading incoming serial data. It attempts to read 
// full sentences and then parse those sentence into their component parts (Command, Address, Value, Checksum). Then it does whatever it
// is supposed to do with the information received. 
// The loop continues forever until one of the following conditions is met: 
// A) the PC sends the disconnect command (PCCMD_DISCONNECT | PCCMD_DISCONNECT | 0 | 0 newline), or
// B) no transmission has been detected in SERIAL_COMM_TIMEOUT seconds, or
// C) the number of reception or other communication errors has exceed MAX_COMM_ERRORCOUNT
// 
// While this is running, the main sketch is completely paused. 
void OP_PCComm::ListenToPC(void) 
{   
    // Initialize our flags
    Disconnect = false;
    eepromUpdated = false;
    numErrors = 0;
    
    // Start the onboard LEDs - Red LED on solid, Green LED blinks slowly
    StartLEDs();
    
    // Prior to Arduino 1.0, you could use the flush() function to remove any buffered incoming data.
    // Confusingly, after 1.0, flush() actually waits for serial transmission to complete. 
    // To clear out anything in the serial input buffer, use this instead: 
    while(_serial->available()) _serial->read();
    
    // We are ready to start communicating. Ask for the next sentence
    AskForNextSentence();
    _serial->flush();       // This causes a pause until the serial transmission is complete
    
    // Start the watchdog timer, so we don't sit here waiting forever if communication stops
    startWatchdog();
    
    // Now keep looping
    do
    {
        // This checks the serial port for data, and attempts to construct a sentence out of any data that comes in. 
        // If ReadData() is true, there will be sentence data available in our SentenceIN struct, which ProcessCommand() will
        // use to do something. 
        if (ReadData()) ProcessCommand();
    
        // Update the watchdog timer
        updateTimer();
        _radio->Update();
        
        // If we have too many errors, or the watchdog timer has expired, take our leave
        if (numErrors >= MAX_COMM_ERRORCOUNT || Timeout) 
        { 
            // In this case we aren't going to wait for a disconnect signal from the PC, we will 
            // initiate it ourselves. But we need to let the PC know. 
            TellPC_Goodbye();
            Disconnect = true;  // This will cause the loop to end
        }

    // Now keep looping through receiving more sentences
    } while (!Disconnect);
    
    // If any eeprom data was updated, let's post it to RAM now
    if (eepromUpdated) _op_eeprom->loadRAMcopy();
    
    // Reset the LEDs
    StopLEDs();

}       

// Process incoming bytes on the serial port
boolean OP_PCComm::ReadData(void)
{
byte endsWith;    
boolean SentenceReceived = false;   // Start off false, will get set to true if a valid sentence was received
static char responseData[SENTENCE_BUFF];    
static uint8_t numBytes = 0;        // Start off with no data received


    if (_serial->available() > 0)   // We have at least one byte to read
    {
        while(_serial->available()) // Read all the bytes that are available
        {
            // Add the next character to our responseData array and increments numBytes
            endsWith = responseData[numBytes++] = _serial->read();
            
            // If we see a newline character, check if we have a valid sentence
            if (endsWith == NEWLINE) //'\n')    
            {
                if (ParseSentence(responseData, numBytes))
                {
                    SentenceReceived = true;
                }
                else
                {
                    // Unable to parse the sentence. Increment the error count, and ask for a repeat
                    numErrors += 1;
                    RepeatSentence();
                }
                
                //  But now whether the sentence was valid or not, we hit newline, so clear the buffer
                // in order to start fresh for the next communication. 
                responseData[0] = (char)0;  // Clear the response buffer (only need to set the first element to zero)
                numBytes = 0;               // And reset the number of bytes received to zero
            }
        }
    }
    
    return SentenceReceived;
}


// ParseSentence takes an array of characters and tries to split them out in the individual pieces of our sentence.
boolean OP_PCComm::ParseSentence(char *data, int datasize)
{
static boolean validSentence = false;
static char line_segment[VALUE_BUFF];   // Buffer large enough to hold all the characters of one portion of the sentence
uint8_t segment_length = 0;
uint8_t numDelimiters = 0;
byte c;

    // Initialize sentence components
    SentenceIN.Command = SentenceIN.ID = SentenceIN.Value = SentenceIN.Checksum = 0;

    for (int line_length=0; line_length<datasize; line_length++)
    {
        c = data[line_length];
        switch (c)
        {
        case NEWLINE:           // end of transmission
            if (numDelimiters = 2 && segment_length > 0)
            {
                // This is the fourth delimiter. The last number received will be the checksum
                // We subtract 1 from segment_length so that the newline char doesn't get counted as part of the number
                SentenceIN.Checksum = constructNumber(line_segment, segment_length-1);

                // Although we haven't yet verified the checksum, this was still an appropriately-constructed sentence. 
                // So reset the watchdog timer (tell it start counting over from now)
                resetWatchdog();
                
                // Now process sentence - here we use the full char array minus the checksum portion at the end 
                if (CRCRequired && (SentenceIN.Checksum != calcrc(data, line_length - segment_length)))
                {   // Checksum doesn't add up. 
                    validSentence = false;
                }
                else
                {
                    // CRC matches, or wasn't required - sentence successfully received and parsed!
                    validSentence = true;
                }
            }
            else
            {
                // There weren't enough segments received to make a complete sentence,
                // but we got to the end of the transmission. Something bad happened. 
                validSentence = false;
            }
            break;

        case '\r':   // discard carriage return
            break;

        case DELIMITER:   // Delimiter
            // terminate the array with a null byte
            line_segment[segment_length] = 0;
            switch (numDelimiters)
            {
                case 0:
                    // This is the first number. The first number received will be our Command
                    SentenceIN.Command = constructNumber(line_segment, segment_length);
                    break;

                case 1:
                    // This is the second delimiter. The number we received will be our ID
                    SentenceIN.ID = constructNumber(line_segment, segment_length);
                    break;

                case 2:
                    // This is the third delimiter. The number we received will be our Value
                    SentenceIN.Value = constructNumber(line_segment, segment_length);
                    break;

                //default:
                    // Theoretically we shouldn't end up here. Ignore whatever came in
            }
            // reset segment
            segment_length = 0;
            numDelimiters += 1;
            break;

        default:
            // keep adding if not full (minus 1 to allow for terminating null byte)
            if (line_length < (SENTENCE_BUFF - 1))
            {
                line_segment[segment_length++] = c;
            }
            else
            {
                // Overflow! Something didn't go right, maybe we're reading random junk
                validSentence = false;
            }
        }
    }
    return validSentence;
}


//------------------------------------------------------------------------------------------------------------------------>>
// RESPOND
//------------------------------------------------------------------------------------------------------------------------>>
// This function assumes the global struct SentenceIN is correctly populated with sentence data! 
// Here we do whatever it is the received command instructed us to do. 
void OP_PCComm::ProcessCommand()
{
// We use this prefix for sending information back
char arrOut[SENTENCE_BUFF];
SentencePrefix s;
char prefixString[VALUE_BUFF];
uint8_t prefixLength = 0;

// Radio detect time
#define WaitForRadio 1000   // Time in mS
uint32_t lastTime;

// These variables are additionally used when sending radio stream data back
static boolean StreamRadio; 
SentencePrefix altS;
char altPrefixString[VALUE_BUFF];
uint8_t altPrefixLength = 0;

static uint8_t HiLo = LOW;  

char pulseString[SENTENCE_BUFF];    
uint8_t pulseStrLength = 0;
uint8_t strLen = 0;


    switch (SentenceIN.Command)
    {
        case PCCMD_NUM_CHANNELS:
            // Computer is requesting number of radio channels. If the radio isn't ready, try to read it for a short amount of time before giving up. 
            _radio->Update();
            lastTime = millis();
            while((_radio->Status() != READY_state) && ((millis() - lastTime) < WaitForRadio))    
            {   
                _radio->detect();       // This will try to detect the radio signal
                _radio->Update();       // Update the radio
                updateTimer();          // Update the PC comm watchdog timer
            }
            
            // Time up, or we sucessfully read the radio.
            if (_radio->Status() != READY_state)
            {   // Radio could not be read, fail. 
                sendNullValueSentence(DVCMD_RADIO_NOTREADY);
            }
            else
            {   // Success, give the computer our number of channels, but first begin the radio object if it hasn't been already. 
                if (!_radio->hasBegun()) _radio->begin(&_op_eeprom->ramcopy);  
                GivePC_Int(PCCMD_NUM_CHANNELS, _radio->ChannelsUtilized);
            }
            break;
        
        case PCCMD_STARTSTREAM_RADIO:
            // We don't try waiting for the radio in this segment, because before this one is called, OP Config will always call first for PCCMD_NUM_CHANNELS, and that one will have already
            // tried getting the radio going. 
            if (_radio->Status() != READY_state) 
            {   // If the radio isn't ready (disconnected, not on, whatever), tell the PC
                sendNullValueSentence(DVCMD_RADIO_NOTREADY);
            }
            else
            {
                // At 115,200 baud it would take 8.5mS to send a 16 channel SBus radio sentence. The sentence would also be about 95 bytes long, but we are trying to keep sentences under 64. 
                // If we are only sending 8 channels of data in a sentence the time would only be about 5mS and the sentence ~55 bytes
                // 8 channels is the max for PPM anyway. While incoming PPM frame lengths can vary by the number of channels and their positions, they are often around 20ms.
                // That means at 115200 we easily have enough time to transmit each PPM frame out the serial port. 
                // SBus is a different story. Although there are theoretically different SBus frame rates, the FrSky X4R we tested sent a new frame every 9mS and the frame
                // itself takes 3mS to read at the SBus baud rate of 100000. That only leaves a 6mS gap to send a 8.5mS sentence to the PC (assuming we sent all 16 channels). 
                // That would not be possible. We do two things to get around this problem with SBus:  
                // First, we don't send a sentence with 16 channels. We set the max to 8 and in the case of SBus we sent channels 1-8 one time, then 9-16 the next time, then back to 1-8, etc... 
                // But even this is very tight - recall we have a 6mS gap between SBus frames and sending an 8 channel sentence to the PC takes 5mS at least. So: 
                // Secondly, we toss every other SBus frame. This is taken care of in the OP_SBusDecode class. The refresh rate is still more than adequate and although we increase latency 
                // a little, it's not noticeable and the advantage is we can do this PC communication business, and gives us more time generally in the main sketch as well. 

                // Start with this set to true
                StreamRadio = true;

                // Construct the prefix once rather than each time through the loop
                s.Command = DVCMD_RETURN_VALUE;                                 // Command - tell PC we are returning a value
                s.ID = DVID_RADIOSTREAM_LO;                                     // In this case we do not repeat the Command, we specify an ID explicitly to let the PC know which bank of channels it's getting
                prefixToByteArray(s, prefixString, VALUE_BUFF, prefixLength);   // Construct the sentence prefix: "Command|ID|"

                // We may need two prefixes depending on if we're sending the first 8 channels or the second 8
                if (_radio->ChannelsUtilized > 8)
                {
                    altS.Command = DVCMD_RETURN_VALUE;
                    altS.ID = DVID_RADIOSTREAM_HI;                              // Let the PC know these are channels 9-(up to)16
                    prefixToByteArray(altS, altPrefixString, VALUE_BUFF, altPrefixLength);
                }

                // But we always start with the first 8 channels
                HiLo = LOW;
                
                // Now loop
                do 
                {
                    if (_radio->NewFrame())
                    {
                        // Clear before start                       
                        strLen = 0;
                        arrOut[0] = pulseString[0] = '\0';                  

                        // Add prefix string to arrOut
                        if (HiLo == LOW)
                        {
                            strcat(arrOut, prefixString);
                            strLen += prefixLength;
                        }
                        else if (HiLo = HIGH)
                        {   // Alternate sentence prefix with the HI ID
                            strcat(arrOut, altPrefixString);
                            strLen += altPrefixLength;                      
                        }

                        // Fill ppmString with values
                        _radio->GetStringFrame(pulseString, SENTENCE_BUFF, pulseStrLength, DELIMITER, HiLo);

                        // Add pulse string to arrOut
                        strcat(arrOut, pulseString);            // Concatenate the arrays
                        strLen += pulseStrLength;               // add the pulse string length
                        arrOut[strLen] = '\0';                  // Mark the end of the array

                        // Send out sentence including CRC
                        _serial->print(arrOut);                 // Now print what we have so far
                        _serial->print(calcrc(arrOut, strLen)); // Calculate the CRC for all the above and print that
                        _serial->print(NEWLINE);                // End sentence
                        _serial->flush();                       // This is supposed to wait until the transmission is done
                        
                        // Now if we have more than 8 channels to send, send the opposite 8 next time around
                        if (_radio->ChannelsUtilized > 8) HiLo == LOW ? HiLo = HIGH : HiLo = LOW;
                    }
                    _radio->Update();                   // Update the radio
                    updateTimer();                      // Update the PC comm watchdog timer
                    if (ReadData()) ProcessCommand();   // recursive - so we know when the PC tells us to stop
                } while (!Disconnect && StreamRadio && !Timeout && numErrors < MAX_COMM_ERRORCOUNT);
            }
            //AskForNextSentence();
            break;
            
        case PCCMD_STOPSTREAM_RADIO:
            // PC wants us to stop the streaming. Set the stream flag to false.
            StreamRadio = false;
            AskForNextSentence();
            break;
        
        case PCCMD_UPDATE_EEPROM:           // Here we need to write whatValue to the EEPROM variable with the matching whatAddress
            if (_op_eeprom->updateEEPROM_byID(SentenceIN.ID, SentenceIN.Value))
            {   // Ok, it was updated. Ask for next command.
                AskForNextSentence();
            }
            else
            {
                // In this case, some error occured updating the EEPROM value
                numErrors += 1;
                // Tell the computer to repeat
                RepeatSentence();
            }
            // Set this flag so we know an update was performed
            if (!eepromUpdated) eepromUpdated = true;
            break;

        case PCCMD_READ_EEPROM:         // Here the computer wants to know what the value is at a certain address.
            GivePC_Value_byID(SentenceIN.ID);
            break;
        
        case PCCMD_READ_VERSION:            // Computer wants to know what firmware version we are running
            if (SentenceIN.ID == SentenceIN.Command)    // On commands with no value ID, the command should be repeated in the ID slot
            {
                GivePC_FirmwareVersion();
            }
            break;

        case PCCMD_STAY_AWAKE:          // Computer has nothing for us to do, but doesn't want us to disconnect yet
            if (SentenceIN.ID == SentenceIN.Command)    // On commands with no value ID, the command should be repeated in the ID slot
            {
                if (!StreamRadio) AskForNextSentence(); // We take no action except respond by letting the computer know we are still ready for the next sentence. 
                // But we don't need to bother with this if we are streaming radio data, since we will be giving it another sentence shortly anyway
            }
            break;
            
        case PCCMD_DISCONNECT:          // Computer is done with us
            if (SentenceIN.ID == SentenceIN.Command)    
            {
                Disconnect = true;      // Setting this flag to true will cause the ReadData loop to quit, and control will return to the sketch. 
            }
            break;
    }
}


// Get some eeprom value and send it to the PC
void OP_PCComm::GivePC_Value_byID(uint16_t ID)
{
char sentenceOut[SENTENCE_BUFF];
char value[VALUE_BUFF]; 
uint8_t valueStrLen = 0;
uint8_t strLen = 0;
SentencePrefix s;

    if (_op_eeprom->readSerialEEPROM_byID(ID, value, VALUE_BUFF, valueStrLen))
    {
        // Good, we have our number, converted to a char array and held in "value" of "valueStrLen" length. 
        // We return a sentence, similar in structure to the sentences we receive: 
        // Command DELIMITER ID DELIMITER Value DELIMITER CRC NEWLINE
        s.Command = DVCMD_RETURN_VALUE;                             // Command - tell PC we are returning a value
        s.ID = ID;                                                  // ID - ID of the value we are sending
        prefixToByteArray(s, sentenceOut, SENTENCE_BUFF, strLen);   // Construct the sentence prefix: "Command|ID|"
        strcat(sentenceOut, value);                                 // Now add the value char array that we got from readSerialEEPROM_byID
        strLen += valueStrLen;                                      // String length is now this long
        sentenceOut[strLen++] = DELIMITER;                          // Add final delimiter
        sentenceOut[strLen] = '\0';                                 // Mark the end of the array
        _serial->print(sentenceOut);                                // Now print: Command | ID | Value |
        _serial->print(calcrc(sentenceOut, strLen));                // Calculate the CRC for all the above and print that
        _serial->print(NEWLINE);                                    // End sentence
        _serial->flush();   
    }
    else
    {
        // We were unable to retrieve the number, probably because it does not exist
        sendNullValueSentence(DVCMD_NOSUCH_VALUE);
    }
}

void OP_PCComm::GivePC_Int(uint16_t returnID, int32_t val)
{
String str = "";
char sentenceOut[SENTENCE_BUFF];
char value[VALUE_BUFF]; 
uint8_t valueStrLen = 0;
uint8_t strLen = 0;
SentencePrefix s;

    // Prefix
    s.Command = DVCMD_RETURN_VALUE;                             // Command - tell PC we are returning a value
    s.ID = returnID;                                            // ID - ID of the value we are sending
    prefixToByteArray(s, sentenceOut, SENTENCE_BUFF, strLen);   // Construct the sentence prefix: "Command|ID|"
    
    // Append value
    str = String(val, DEC);
    str.toCharArray(value, VALUE_BUFF);
    valueStrLen = str.length(); 
    strcat(sentenceOut, value);                                 // Now add the value char array that we got from readSerialEEPROM_byID
    strLen += valueStrLen;                                      // String length is now this long
    sentenceOut[strLen++] = DELIMITER;                          // Add final delimiter
    sentenceOut[strLen] = '\0';                                 // Mark the end of the array
    _serial->print(sentenceOut);                                // Now print: Command | ID | Value |
    _serial->print(calcrc(sentenceOut, strLen));                // Calculate the CRC for all the above and print that
    _serial->print(NEWLINE);                                    // End sentence
    _serial->flush();   
}

// Give the PC our firmware version
void OP_PCComm::GivePC_FirmwareVersion(void)
{
    // Send the firmware version number. This is defined in OP_Settings.h

    // This routine is a bit unusual since in all other cases we are sending a value of 0, 
    // or else some numerical value. But in this case we are sending a string value. 
    // Of course everything gets converted into the equivalent of a string as it gets sent out,
    // but we have to handle this one a bit different to correctly determine the CRC
    
    char sentenceOut[SENTENCE_BUFF];        // buffer for sentence
    char fValue[VALUE_BUFF];                // smaller buffer for firmware version string
    uint8_t strLen;                         // total string length

    // The firmware version does not have an ID number like our other values in EEPROM
    // For the ID we instead use the CMD that was given us requesting the firmware version
    // This is unusual but lets the computer identify this as a firmware value and not
    // some eeprom value. 
    SentencePrefix s;
        s.Command = DVCMD_RETURN_VALUE;
        s.ID = PCCMD_READ_VERSION;
    
    // Now convert the command | ID | to a byte array
    prefixToByteArray(s, sentenceOut, SENTENCE_BUFF, strLen);

    String str = FIRMWARE_VERSION;          // Now convert firmware version string to char array
    str += DELIMITER;                       // add final delimiter
    strLen += str.length();                 // Add to the string length
    fValue[0] = '\0';                       // Convert the string to a char array in fValue
    str.toCharArray(fValue, VALUE_BUFF);    
    strcat(sentenceOut, fValue);            // Concatenate the arrays
    sentenceOut[strLen] = '\0';             // Mark the end of the array

    _serial->print(sentenceOut);            // Now print command | ID | 0 |
    _serial->print(calcrc(sentenceOut, strLen));    // Calculate the CRC for all the above and print that
    _serial->print(NEWLINE);                // End sentence
    _serial->flush();   
    
}

void OP_PCComm::AskForNextSentence(void)
{
    sendNullValueSentence(DVCMD_NEXT_SENTENCE);
}

void OP_PCComm::RepeatSentence(void)
{
    sendNullValueSentence(DVCMD_REPEAT_SENTENCE);
}

void OP_PCComm::TellPC_Goodbye(void)
{
    sendNullValueSentence(DVCMD_GOODBYE);
}

void OP_PCComm::sendNullValueSentence(uint8_t command)
{
    char sentenceOut[SENTENCE_BUFF];
    uint8_t strLen;

    // Null value sentences repeat the command in the ID field
    SentencePrefix s;
        s.Command = s.ID = command;
    // Now convert the command | ID | to a byte array
    prefixToByteArray(s, sentenceOut, SENTENCE_BUFF, strLen);
    // Add the null value and the final delimiter
    sentenceOut[strLen++] = '0';    // no value
    sentenceOut[strLen++] = DELIMITER; 
    sentenceOut[strLen] = '\0'; // Mark the end of the array
    // Now print command | ID | 0 |
    _serial->print(sentenceOut);
    // Calculate the CRC for all the above and print that
    _serial->print(calcrc(sentenceOut, strLen));
    // End sentence
    _serial->print(NEWLINE);
    _serial->flush();   
}

void OP_PCComm::prefixToByteArray(SentencePrefix s, char *prefix, uint8_t prefixBUFF, uint8_t &returnStrLen)
{
// Clear to begin
prefix[0] = '\0';
returnStrLen = 0;

String str;
str = String(s.Command, DEC);
str += DELIMITER;
str += String(s.ID, DEC);
str += DELIMITER;

// Set the passed length to the string length
returnStrLen = str.length();
// Convert the string to a char array, stored in the array passed
str.toCharArray(prefix, prefixBUFF);

}


//------------------------------------------------------------------------------------------------------------------------>>
// UTILITIES
//------------------------------------------------------------------------------------------------------------------------>>

// Listen to the alternate serial port instead
void OP_PCComm::switchToAltSerial(void)
{
    _serial = &ALT_SERIAL_PORT;
}

// Reverts back to default serial port
void OP_PCComm::revertToDefaultSerial(void)
{
    _serial = &DEFAULT_SERIAL_PORT;
}

// This will convert a string of characters into a number
// The number must be able to fit into a 4-byte int32
int32_t OP_PCComm::constructNumber(char *c, int numBytes)
{
    boolean negative = false;
    int32_t constructedNumber = 0;
    for (int i=0; i<=numBytes; i++)
    {   
        if ((i==0) && (c[i] == '-')) { negative = true; }
        else if (c[i] >= '0' && c[i] <= '9')
        {
            constructedNumber *= 10;          // Prior digit gets shifted over by power of ten
            constructedNumber += c[i] - '0';  // Char will be decimal equivalent of number. Decimal 48 is char 0 and it goes up from there. So we subtract char 0 from the received char to get the actual number
        }    
    }
    if (negative) { constructedNumber = -constructedNumber; }
    return constructedNumber; 
    
}

// This will calculate a CRC value from a character string
int16_t OP_PCComm::calcrc(char *ptr, int16_t count) 
{ 
    int16_t crc; 
    uint8_t i; 
    
    crc = 0; 
    while (--count >= 0) 
    { 
        crc = crc ^ (int16_t) *ptr++ << 8; 
        i = 8; 
        do 
        { 
            if (crc & 0x8000) 
                crc = crc << 1 ^ 0x1021; 
            else 
                crc = crc << 1; 
        } while(--i); 
    } 
    return (crc); 
}

// Turn on/off CRC checking
void OP_PCComm::skipCRC(void)
{
    CRCRequired = false;
}
void OP_PCComm::requireCRC(void)
{
    CRCRequired = true;
}


//------------------------------------------------------------------------------------------------------------------------>>
// WATCHDOG TIMER
//------------------------------------------------------------------------------------------------------------------------>>

void OP_PCComm::startWatchdog(void)
{
    // Make sure no other timer is running
    stopWatchdog();
    
    // Clear the timeout flag
    Timeout = false;
    
    // Start the timer
    watchdogTimerID = commTimer.setTimeout(SERIAL_COMM_TIMEOUT, triggerWatchdog);
}

void OP_PCComm::triggerWatchdog(void)
{
    // If we make it to here, not good. Set the Timeout flag so we know to disconnect
    Timeout = true;
}

void OP_PCComm::resetWatchdog(void)
{
    // Every time we receive a valid sentence over the serial port, we "reset" the watchdog
    // timer, meaning we start it over counting from now. So long as data keeps coming in, 
    // the timer will keep getting reset, and we won't hit the timeout
    commTimer.restartTimer(watchdogTimerID);
}

void OP_PCComm::stopWatchdog(void)
{
    if (commTimer.isEnabled(watchdogTimerID)) { commTimer.deleteTimer(watchdogTimerID); }
}

void OP_PCComm::updateTimer(void)
{
    commTimer.run();
    UpdateLEDs();
}


//------------------------------------------------------------------------------------------------------------------------>>
// LED CONTROL
//------------------------------------------------------------------------------------------------------------------------>>
void OP_PCComm::StartLEDs(void)
{
    // Turn on the Red LED, and leave it on
    digitalWrite(pin_RedLED, HIGH);
    // The Green LED will blink slowly. 
    UpdateLEDs(); 
}
void OP_PCComm::UpdateLEDs(void)
{
    static uint32_t last_millis = millis();
    
    // For as long as we are communicating with the PC, we want the Green LED to blink slowly
    if ((millis() - last_millis) > 1000)
    {
        digitalRead(pin_GreenLED) ? digitalWrite(pin_GreenLED, LOW) : digitalWrite(pin_GreenLED, HIGH);
        last_millis = millis();
    }
}
void OP_PCComm::StopLEDs(void)
{
    digitalWrite(pin_RedLED, LOW);
    digitalWrite(pin_GreenLED, LOW);
}
