#pragma once

class MTIDevice;

struct MTIRasterConfig
{
    unsigned int sps = 2000;
    float xAmp = 0.9f;
    float yAmp = 0.75f;
    unsigned int numLines = 100;
    unsigned int numPixels = 100;
    float lineTime = 0.01f;
    bool ppMode = false;
    bool retrace = true;
    int triggerShift = 0;
    float theta = 1.5707963f; // pi/2
};

bool MTI_Init(MTIDevice*& mti, const char* portName);
bool MTI_StartLinearRaster(MTIDevice* mti); 
void MTI_Stop(MTIDevice* mti);
void MTI_Shutdown(MTIDevice*& mti);