#include <Windows.h>
#include <string>
#include <codecvt>
#include "Global.h"

MyLock g_EncodingLock;


void UTF16ToUTF8(const wchar_t* string, std::string& outString)
{
	int utf8Len = WideCharToMultiByte(CP_UTF8, 0, string, -1, NULL, 0, NULL, NULL);
	char* tempString2 = new char[utf8Len];

	int nullIndex2 = WideCharToMultiByte(CP_UTF8, 0, string, -1, tempString2, utf8Len, NULL, NULL);
	tempString2[nullIndex2] = L'\0';

	outString = tempString2;

	delete[] tempString2;
}

void UTF8ToUTF16(const char* string, std::wstring& outString)
{
	int utf16Strlen = MultiByteToWideChar(CP_UTF8, 0, string, -1, NULL, 0);
	WCHAR* tempString = new WCHAR[utf16Strlen];

	int nullIndex = MultiByteToWideChar(CP_UTF8, 0, string, -1, tempString, utf16Strlen);
	tempString[nullIndex] = L'\0';

	outString = tempString;

	delete[]tempString;
}


//void UTF16ToUTF8(const wchar_t* string,std::string& outString)
//{
//	g_EncodingLock.Lock();
//	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
//	outString = convert.to_bytes(string);
//	g_EncodingLock.Unlock();
//
//
//}
//void UTF8ToUTF16(const char* string,std::wstring& outString)
//{
//	g_EncodingLock.Lock();
//	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
//	outString = convert.from_bytes(string);
//
//	g_EncodingLock.Unlock();
//}