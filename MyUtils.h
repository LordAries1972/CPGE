#pragma once

#ifndef _GAMEENGINE_MYUTILS_
#define _GAMEENGINE_MYUTILS_

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <DirectXMath.h>
#include <iomanip> // For formatting
#include <iostream>
#include <intrin.h>
#include <iostream> 
#include <fstream> 
#include <sstream>
#include <exception> 
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;                            // Use the WRL namespace

const LPCWSTR LOG_DEFAULT_NAME = L"ERROR_log.txt";		 // Default Error Log Filename.

class MyUtils
{
// Public variables and declarations.
public:
    // CPU Related variables.
    bool hasFPU;
    bool hasSSE;
    bool hasSSE2;
    bool hasSSE3;
    int cpuInfo[4] = { -1 };
    char cpuBrandString[0x40];

    // Public functions.
	void SetHWND (HWND hwnd);
    void getCPUInfo();
    LPWSTR ConvertCharToLPWSTR(const char* charString);
    void ShowErrorMessage(HWND hwnd, LPCSTR lpMessage);
    std::wstring GetErrorMessage(HRESULT hr);
    std::string ConvertWStringToString(const std::wstring& wstr);
    void LogException(const std::string& message);
    void ThrowIfFailed(HRESULT hr);
    LPCWSTR IntToLPCWSTR(int value);
	LPCWSTR ConcatenateLPCWSTR(LPCWSTR str1, LPCWSTR str2);
	void LogMatrix(const DirectX::XMMATRIX& matrix, const std::wstring& matrixName);
	void SleepForSeconds(int iSeconds);
    BYTE Clamp(int value, int min, int max);

// Private variables and declarations.
private:

    HWND hWnd;

};

#endif
