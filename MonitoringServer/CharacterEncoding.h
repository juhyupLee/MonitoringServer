#pragma once
#include <string>
void UTF16ToUTF8(const wchar_t* string, std::string& outString);
void UTF8ToUTF16(const char* string, std::wstring& outString);