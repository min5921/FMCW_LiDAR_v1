#include "mti_raster.h"

#include "MTIDefinitions.h"
#include "MTIDevice.h"
#include "logger.h"


#include <cmath>
#include <cstring>

bool MTI_Init(MTIDevice*& mti, const char* portName)
{
    if (mti) {
        AddLog("MTI_Init: already initialized.");
        return true;
    }

    AddLog("MTI_Init: creating device, port=%s", portName);

    mti = new MTIDevice();

    char portBuf[64] = {};
    strcpy_s(portBuf, sizeof(portBuf), portName);

    mti->ConnectDevice(portBuf);

    MTIError err = mti->GetLastError();
    if (err != MTIError::MTI_SUCCESS) {
        AddLog("MTI_Init FAILED: ConnectDevice(%s), err=%d", portBuf, (int)err);
        delete mti;
        mti = nullptr;
        return false;
    }

    AddLog("MTI_Init: connected to %s", portBuf);

    MTIDeviceParams params;
    mti->GetDeviceParams(&params);

    params.Vbias = 80;
    params.VdifferenceMax = 100;
    params.HardwareFilterBw = 300;   // 메뉴와 동일하게
    params.DataMode = MTIDataMode::Sample_Output;
    params.DataScale = 1.0f;
    params.SampleRate = 48000;
    params.DeviceAxes = MTIAxes::Normal;
    params.SyncMode = MTISync::Output_DOut0;

    mti->SetDeviceParams(&params);
    mti->ResetDevicePosition();
    mti->SetDeviceParam(MTIParam::MEMSDriverEnable, true);

    AddLog("MTI_Init OK: Vbias=%.1f, VdiffMax=%.1f, FilterBw=%d, SampleRate=%d",
        params.Vbias,
        params.VdifferenceMax,
        params.HardwareFilterBw,
        params.SampleRate);

    return true;
}

bool MTI_StartLinearRaster(MTIDevice* mti)
{
    if (!mti) {
        AddLog("MTI_StartLinearRaster FAILED: mti is null");
        return false;
    }


    MTIDataGenerator* datagen = new MTIDataGenerator();

    // -----------------------------
    // 메뉴 옵션과 동일한 설정
    // -----------------------------
    unsigned int sps = 48000;     // 메뉴 결과에 맞춤
    float xAmp = 1.0f;            // a: X amplitude
    float yAmp = 1.0f;            // a: Y amplitude
    unsigned int numLines = 25;  // 1: number of lines
    unsigned int numPixels = 50;  // 2: number of pixels
    float lineTime = 0.0025f;     // 4: duration of one line
    bool ppMode = false;          // t: linear raster(0)
    bool retrace = true;          // 5: bi-directional writing = 1
    int triggerShift = 2;         // digital pulse delay
    float theta = 0.0f;           // 3: angle of lines (0 = vertical)

    AddLog("MTI_StartLinearRaster: request lines=%u pixels=%u theta=%.3f lineTime=%.4f retrace=%d xAmp=%.3f yAmp=%.3f sps=%u",
        numLines, numPixels, theta, lineTime, (int)retrace, xAmp, yAmp, sps);


    MTIDeviceParams params;
    mti->GetDeviceParams(&params);

    // 메뉴 옵션과 동일
    params.HardwareFilterBw = 300;      // 6
    params.DataMode = MTIDataMode::Sample_Output;
    params.DataScale = 1.0f;            // amplitude 1.0 기준
    params.DeviceAxes = MTIAxes::Normal;
    params.SyncMode = MTISync::Output_DOut0;

    // offset = 0, 0
    mti->SetDeviceParams(&params);
    mti->SetDeviceParam(MTIParam::OutputOffsets, 0.0f, 0.0f);

    int spsMax = 65000;
    int spsMin = params.DeviceLimits.SampleRate_Min;

    int npts_alloc = 1000000;
    float* xData = new float[npts_alloc];
    float* yData = new float[npts_alloc];
    unsigned char* mData = new unsigned char[npts_alloc];

    int npts = datagen->LinearRasterPattern(
        xData, yData, mData,
        xAmp, yAmp,
        numLines, numPixels,
        lineTime,
        ppMode, retrace,
        triggerShift,
        theta,
        sps,
        spsMin, spsMax
    );



    // 함수가 sps를 조정할 수 있으므로, 반환된 sps 사용
    mti->SetDeviceParam(MTIParam::SampleRate, sps);
    mti->SetDeviceParam(MTIParam::DigitalOutputEnable, 1);

    AddLog("MTI_StartLinearRaster: actual sps=%u npts=%d totalTime=%.6f sec samplesPerLine=%.3f",
        sps,
        npts,
        (sps > 0) ? ((double)npts / (double)sps) : 0.0,
        (double)sps * (double)lineTime);

    unsigned int delaySamples = 0;
    bool minimizeJump = true;
    bool compensateFilterDelay = true;

    mti->SendDataStream(
        xData, yData, mData, npts,
        delaySamples,
        minimizeJump,
        compensateFilterDelay
    );

    AddLog("MTI_StartLinearRaster: SendDataStream OK (delay=%u, minimizeJump=%d, compensateFilterDelay=%d)",
        delaySamples,
        (int)minimizeJump,
        (int)compensateFilterDelay);

    delete[] xData;
    delete[] yData;
    delete[] mData;
    delete datagen;

    return true;
}
void MTI_Stop(MTIDevice* mti)
{
    if (!mti) {
        AddLog("MTI_Stop: mti is null");
        return;
    }
    AddLog("MTI_Stop: stopping raster...");

    mti->StopDataStream();
    mti->ResetDevicePosition();
    AddLog("MTI_Stop: done");

}

void MTI_Shutdown(MTIDevice*& mti)
{
    if (!mti) {
        AddLog("MTI_Shutdown: already null");
        return;
    }
    AddLog("MTI_Shutdown: begin");

    MTI_Stop(mti);
    mti->SetDeviceParam(MTIParam::MEMSDriverEnable, false);
    mti->DisconnectDevice();

    delete mti;
    mti = nullptr;

    AddLog("MTI_Shutdown: complete");

}