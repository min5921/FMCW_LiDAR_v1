// acquire.h
#pragma once
#include <windows.h>      // BOOL, HANDLE È®½ÇÈ÷
#include "AlazarApi.h"
#include "AlazarCmd.h"
#include "AlazarError.h"

struct Config {
	int sample_rate;
	int sample_point;
	int A_scanNum;
	int B_scannum;

	float x_start_angle;
	float x_end_angle;

	float y_start_angle;
	float y_end_angle;

	bool direction;

	int bandwidth;
	int sweeprate;
	int wavelength;

	std::string udp_ip;
	int udp_port;
};

BOOL LoadConfig(const std::string& path, Config& set);
BOOL ConfigureBoard(HANDLE boardHandle);
BOOL AcquireData(HANDLE boardHandle);
