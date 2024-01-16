#include <Arduino.h>
#include <ArduinoJson.h>
#include <etc.h>
#include <WiFi.h>
#include "FS.h"
#include <LittleFS.h>
#include <ETH.h>
#include "config.h"
#include "log.h"
#include "web.h"
#include "intelhex.h"

extern struct ConfigSettingsStruct ConfigSettings;
extern struct zbVerStruct zbVer;

extern const char *tempFile;

const byte cmdLed0 = 0x27;
const byte cmdLed1 = 0x0A;
const byte cmdLedIndex = 0x01; // for led 1
const byte cmdLedStateOff = 0x00;
const byte cmdLedStateOn = 0x01;
const byte cmdFrameStart = 0xFE;
const byte cmdLedLen = 0x02;

const byte zigLed1Off[] = {cmdFrameStart, cmdLedLen, cmdLed0, cmdLed1, cmdLedIndex, cmdLedStateOff, 0x2E}; // resp FE 01 67 0A 00 6C
const byte zigLed1On[] = {cmdFrameStart, cmdLedLen, cmdLed0, cmdLed1, cmdLedIndex, cmdLedStateOn, 0x2F};
const byte cmdLedResp[] = {0xFE, 0x01, 0x67, 0x0A, 0x00, 0x6C};

void clearS2Buffer()
{
    while (Serial2.available())
    { // clear buffer
        Serial2.read();
    }
}

void getZbVer()
{
    zbVer.zbRev = 0;
    const byte cmdFrameStart = 0xFE;
    const byte zero = 0x00;
    const byte cmd1 = 0x21;
    const byte cmd2 = 0x02;
    const byte cmdSysVersion[] = {cmdFrameStart, zero, cmd1, cmd2, 0x23};
    for (uint8_t i = 0; i < 6; i++)
    {
        if (Serial2.read() != cmdFrameStart || Serial2.read() != 0x0a || Serial2.read() != 0x61 || Serial2.read() != cmd2)
        {                    // check for packet start
            clearS2Buffer(); // skip
            Serial2.write(cmdSysVersion, sizeof(cmdSysVersion));
            Serial2.flush();
            delay(100);
        }
        else
        {
            const uint8_t zbVerLen = 11;
            byte zbVerBuf[zbVerLen];
            for (uint8_t i = 0; i < zbVerLen; i++)
            {
                zbVerBuf[i] = Serial2.read();
            }
            zbVer.zbRev = zbVerBuf[5] | (zbVerBuf[6] << 8) | (zbVerBuf[7] << 16) | (zbVerBuf[8] << 24);
            zbVer.maintrel = zbVerBuf[4];
            zbVer.minorrel = zbVerBuf[3];
            zbVer.majorrel = zbVerBuf[2];
            zbVer.product = zbVerBuf[1];
            zbVer.transportrev = zbVerBuf[0];
            printLogMsg(String("[ZBVER]") + " Rev: " + zbVer.zbRev + " Maintrel: " + zbVer.maintrel + " Minorrel: " + zbVer.minorrel + " Majorrel: " + zbVer.majorrel + " Transportrev: " + zbVer.transportrev + " Product: " + zbVer.product);
            clearS2Buffer();
            break;
        }
    }
}

void zbCheck()
{

    // Serial2.begin(115200, SERIAL_8N1, CC2652P_RXD, CC2652P_TXD); //start zigbee serial
    bool respOk = false;
    for (uint8_t i = 0; i < 12; i++)
    { // wait for zigbee start
        if (respOk)
            break;
        clearS2Buffer();
        Serial2.write(zigLed1On, sizeof(zigLed1On));
        Serial2.flush();
        delay(400);
        for (uint8_t i = 0; i < 5; i++)
        {
            if (Serial2.read() != 0xFE)
            {                   // check for packet start
                Serial2.read(); // skip
            }
            else
            {
                for (uint8_t i = 1; i < 4; i++)
                {
                    if (Serial2.read() != cmdLedResp[i])
                    { // check if resp ok
                        respOk = false;
                        break;
                    }
                    else
                    {
                        respOk = true;
                    }
                }
            }
        }
        digitalWrite(LED_USB, !digitalRead(LED_USB)); // blue led flashing mean wait for zigbee resp
    }
    delay(500);
    if (!respOk)
    {
        digitalWrite(LED_PWR, 1);
        digitalWrite(LED_USB, 1);
        for (uint8_t i = 0; i < 5; i++)
        { // indicate wrong resp
            digitalWrite(LED_PWR, !digitalRead(LED_PWR));
            digitalWrite(LED_USB, !digitalRead(LED_USB));
            delay(1000);
        }
        printLogMsg("[ZBCHK] Wrong answer");
        printLogMsg("[ZBVER] Unknown");
        zbVer.zbRev = 0;
    }
    else
    {
        Serial2.write(zigLed1Off, sizeof(zigLed1Off));
        Serial2.flush();
        delay(250);
        clearS2Buffer();
        printLogMsg("[ZBCHK] Connection OK");
    }
    digitalWrite(LED_PWR, 0);
    digitalWrite(LED_USB, 0);
}

void zbLedToggle()
{
    bool respOk = false;
    clearS2Buffer();
    if (ConfigSettings.zbLedState == 0)
    {
        printLogMsg("[ZB] LED toggle ON");
        Serial2.write(zigLed1On, sizeof(zigLed1On));
    }
    else
    {
        printLogMsg("[ZB] LED toggle OFF");
        Serial2.write(zigLed1Off, sizeof(zigLed1Off));
    }
    Serial2.flush();
    delay(400);
    for (uint8_t i = 0; i < 5; i++)
    {
        if (Serial2.read() != 0xFE)
        {                   // check for packet start
            Serial2.read(); // skip
        }
        else
        {
            for (uint8_t i = 1; i < 4; i++)
            {
                if (Serial2.read() != cmdLedResp[i])
                { // check if resp ok
                    respOk = false;
                    break;
                }
                else
                {
                    respOk = true;
                }
            }
        }
    }
    if (respOk)
    {
        printLogMsg("[ZB] LED toggle OK");
        ConfigSettings.zbLedState = !ConfigSettings.zbLedState;
    }
}

void getZbChip()
{
    zigbeeEnableBSL();
    const byte cmdChipID[] = {0x03, 0x28, 0x28};
    for (uint8_t i = 0; i < 6; i++)
    {
        // if (Serial2.read() != cmdFrameStart || Serial2.read() != 0x0a || Serial2.read() != 0x61 || Serial2.read() != cmd2){//check for packet start
        clearS2Buffer(); // skip
        Serial2.write(cmdChipID, sizeof(cmdChipID));
        Serial2.flush();
        delay(300);
        //}else{
        const uint8_t zbChipLen = 20;
        byte zbChipBuf[zbChipLen];
        for (uint8_t i = 0; i < zbChipLen; i++)
        {
            zbChipBuf[i] = Serial2.read();
            printLogMsg(String("[zbChipBuf]") + zbChipBuf[i]);
        }
        uint32_t zbChipID = (zbChipBuf[2] << 8) | zbChipBuf[3];
        // zbVer.zbRev =  zbVerBuf[5] | (zbVerBuf[6] << 8) | (zbVerBuf[7] << 16) | (zbVerBuf[8] << 24);

        printLogMsg(String("[ZB_Chip_ID]") + zbChipID);

        clearS2Buffer();
        // break;
        //}
    }
}

void preParse()
{
    DEBUG_PRINTLN("Starting the parsing process");
}

void postParse()
{
    DEBUG_PRINTLN("Parsing complete");
}

void parseCallback(uint32_t address, uint8_t len, uint8_t *data)
{
    // DEBUG_PRINT(".");

    /* // Print each parsed record for debugging purposes
    DEBUG_PRINT("Address: 0x");
    DEBUG_PRINT(String(address, HEX));
    DEBUG_PRINT(", Length: 0x");
    DEBUG_PRINTLN(String(len, HEX));

    for (uint8_t i = 0; i < len; i++)
    {
        DEBUG_PRINT("0x");
        DEBUG_PRINT(String(data[i], HEX));
        DEBUG_PRINT(" ");
    }
    DEBUG_PRINTLN(""); */
}

void checkFwHex(const char *tempFile) // check Zigbee FW file using IntelHEX, than check BSL pin.
{
    IntelHex zb_hex(tempFile);

    if (!zb_hex.parse(preParse, parseCallback, postParse))
    {
        DEBUG_PRINTLN("Failed to parse the zb_hex HEX file. Corrupted file");
    }

    if (!zb_hex.fileParsed())
    {
        DEBUG_PRINTLN("File not good");
    }

    if (zb_hex.bslActive())
    {
        DEBUG_PRINTLN("BSL (" + String(zb_hex.bslAddr() ? "P7 and R7 chips" : "All chips") + ") pin " + String(zb_hex.bslPin()) + " level " + String(zb_hex.bslLevel() ? "HIGH" : "LOW"));
        if (zb_hex.bslAddr() == ALL_CHIP_ID && zb_hex.bslPin() == BSL_PIN && zb_hex.bslLevel() == BSL_LEVEL) // All series DIO 15 LOW
        {
            zb_hex.setFileValidated(true);
            DEBUG_PRINTLN("BSL config is right!");
        }
    }
    else
    {
        DEBUG_PRINTLN("BSL config error. Range not found or CCFG incorrect.");
    }

    if (!zb_hex.fileValidated())
    {
        DEBUG_PRINTLN("BSL config incorect. Wrong chip, pin or level.");
    }
}
