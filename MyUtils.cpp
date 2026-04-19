////////////////////////////////////////////////////////////////////////////////////////
// Utility Class written by Daniel J. Hobson copyright 2024-2025
//
// 28-06-2024   -   Implemented getCPUInfo()
//              -   Implemented ShowErrorMessage()
//              -   Implemented String Conversion routine ConvertCharToLPWSTR()
// 24-11-2024   -   Implemented LogException() to assist with debugging.
////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "MyUtils.h"
#include <wchar.h>
#include <new>

inline void MyUtils::SetHWND (HWND hwnd)
{
	hWnd = hwnd;
}

inline void MyUtils::SleepForSeconds(int iSeconds)
{
    int iSecondCount = 0;
    while (iSecondCount < iSeconds)
    {
        Sleep(iSeconds * 1000);
        iSecondCount++;
    }
}

inline BYTE MyUtils::Clamp(int value, int min, int max)
{
    if (value < min) return static_cast<BYTE>(min);
    if (value > max) return static_cast<BYTE>(max);
    return static_cast<BYTE>(value);
}

inline void MyUtils::getCPUInfo()
{
//    int cpuInfo[4] = { -1 };
//    char cpuBrandString[0x40];
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = cpuInfo[0];

    memset(cpuBrandString, 0, sizeof(cpuBrandString));

    // Get the information associated with each extended ID.
    for (unsigned int i = 0x80000000; i <= nExIds; ++i)
    {
        __cpuid(cpuInfo, i);

        // Interpret CPU brand string
        if (i == 0x80000002)
            memcpy(cpuBrandString, cpuInfo, sizeof(cpuInfo));
        else if (i == 0x80000003)
            memcpy(cpuBrandString + 16, cpuInfo, sizeof(cpuInfo));
        else if (i == 0x80000004)
            memcpy(cpuBrandString + 32, cpuInfo, sizeof(cpuInfo));
    }

//    std::cout << "CPU: " << cpuBrandString << std::endl;

    // Get the CPU features
    __cpuid(cpuInfo, 1);
    bool hasFPU = cpuInfo[3] & (1 << 0);
    bool hasSSE = cpuInfo[3] & (1 << 25);
    bool hasSSE2 = cpuInfo[3] & (1 << 26);
    bool hasSSE3 = cpuInfo[2] & (1 << 0);

//    std::cout << "FPU: " << (hasFPU ? "Supported" : "Not Supported") << std::endl;
//    std::cout << "SSE: " << (hasSSE ? "Supported" : "Not Supported") << std::endl;
//    std::cout << "SSE2: " << (hasSSE2 ? "Supported" : "Not Supported") << std::endl;
//    std::cout << "SSE3: " << (hasSSE3 ? "Supported" : "Not Supported") << std::endl;
}

inline void MyUtils::ShowErrorMessage(HWND hwnd, LPCSTR lpMessage)
{
    MessageBoxA(hwnd, lpMessage, NULL, MB_OK | MB_ICONERROR);
}

inline LPWSTR MyUtils::ConvertCharToLPWSTR(const char* charString)
{
    // Determine the required buffer size for the wide character string
    int bufferSize = MultiByteToWideChar(CP_ACP, 0, charString, -1, NULL, 0);
    if (bufferSize == 0) {
        // Handle error
        return NULL;
    }

    // Allocate memory for the wide character string
    LPWSTR wideString = new WCHAR[bufferSize];

    // Perform the conversion
    int result = MultiByteToWideChar(CP_ACP, 0, charString, -1, wideString, bufferSize);
    if (result == 0) {
        // Handle error
        delete[] wideString;
        return NULL;
    }

    return wideString;
}

inline void MyUtils::LogException(const std::string& message)
{
    std::ofstream logFile(LOG_DEFAULT_NAME, std::ios::app);
    if (logFile.is_open())
    {
        SYSTEMTIME time;
        GetLocalTime(&time);

        logFile << "[" << time.wYear << "-" << time.wMonth << "-" << time.wDay << " "
            << time.wHour << ":" << time.wMinute << ":" << time.wSecond << "] "
            << message << std::endl;

        logFile.close();
    }
}

inline std::wstring MyUtils::GetErrorMessage (HRESULT hr)
{
    if (HRESULT_FACILITY(hr) == FACILITY_WINDOWS)
    {
        hr = HRESULT_CODE(hr);
    }

    LPVOID lpMsgBuf;
    DWORD bufLen = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, nullptr);

    if (bufLen)
    {
        std::wstring result((LPTSTR)lpMsgBuf, bufLen);
        LocalFree(lpMsgBuf);
        return result;
    }
    return std::wstring(L"Unknown error");
}

inline std::string MyUtils::ConvertWStringToString(const std::wstring& wstr)
{
    if (wstr.empty()) 
        return std::string();

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
}

inline void MyUtils::ThrowIfFailed(HRESULT hr)
{
    try
	{
		if (FAILED(hr))
		{
			std::string frontMsg = "HRESULT function call failed ->";
            std::string backMessage = ConvertWStringToString (GetErrorMessage(hr));
			std::string message = frontMsg + backMessage;
            throw std::runtime_error (message);
			ShowErrorMessage(NULL, message.c_str());
            PostQuitMessage(0);
		}
	}
	catch (const std::exception& e)
	{
		LogException(e.what());
        ShowErrorMessage(NULL, e.what());
        PostQuitMessage(0);
    }
}

// Function to convert integer to LPCWSTR
inline LPCWSTR MyUtils::IntToLPCWSTR(int value)
{
    // Convert integer to string using wstringstream
    std::wstringstream wss;
    wss << value;
    std::wstring ws = wss.str();

    // Allocate a buffer for the wide string
    size_t size = ws.size() + 1; // +1 for the null terminator
    wchar_t* buffer = new wchar_t[size];

    // Copy the wide string to the buffer
    wcscpy_s(buffer, size, ws.c_str());

    // Return the pointer to the buffer
    return buffer;
}

// Function to concatenate two LPCWSTR strings 
// Function to concatenate two LPCWSTR strings
inline LPCWSTR MyUtils::ConcatenateLPCWSTR(LPCWSTR str1, LPCWSTR str2)
{
    size_t len1 = wcslen(str1);
    size_t len2 = wcslen(str2);
    size_t totalLen = len1 + len2 + 1; // +1 for null terminator

    // Allocate memory for the concatenated string
    wchar_t* result = new wchar_t[totalLen];

    // Copy the first string
    wcscpy_s(result, len1 + 1, str1);

    // Append the second string
    wcscat_s(result, totalLen, str2);

    // Return the concatenated string
    return result;
}

inline void MyUtils::LogMatrix(const DirectX::XMMATRIX& matrix, const std::wstring& matrixName)
{
    std::wostringstream oss;
    oss << matrixName << L":\n";

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            oss << std::setw(10) << std::fixed << std::setprecision(4)
                << matrix.r[row].m128_f32[col] << L" ";
        }
        oss << L"\n";
    }

    OutputDebugString(oss.str().c_str());
}








