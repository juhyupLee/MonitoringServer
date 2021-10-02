#pragma once
/////////////////////////////////////////////////////////
// MySQL DB 연결 클래스
//프로젝트-> 속성 -> 디버깅 -> 환경
// PATH=~~\MYSQLLib;%PATH%  DLL 검색할수있도록 Path설정
/////////////////////////////////////////////////////////


#pragma comment(lib,"MYSQLLib/mysqlcppconn.lib")
#include <Windows.h>
#include <stdlib.h>
#include <iostream>
#include <string>


#include "MYSQLLib/Connector C++ 8.0/include/jdbc/cppconn/driver.h"
#include "MYSQLLib/Connector C++ 8.0/include/jdbc/cppconn/exception.h"
#include "MYSQLLib/Connector C++ 8.0/include/jdbc/cppconn/resultset.h"
#include "MYSQLLib/Connector C++ 8.0/include/jdbc/cppconn/statement.h"



class DBConnector
{
public:
	enum
	{
		QUERY_MAX_LEN = 2048,
		LOG_TIME = 500
	};

	//----------------------------------------------------
	// 	   Memory Pool을 안쓴다면, 그냥 생성자에 매개변수 던져서 초기화
	//----------------------------------------------------
	DBConnector(const WCHAR* dbIP, const WCHAR* dbPort, const WCHAR* user, const WCHAR* password, const WCHAR* dbName);
	virtual		~DBConnector();


	//////////////////////////////////////////////////////////////////////
	// MySQL DB 연결
	//////////////////////////////////////////////////////////////////////
	bool		Connect(void);

	//////////////////////////////////////////////////////////////////////
	// MySQL DB 끊기
	//////////////////////////////////////////////////////////////////////
	bool		Disconnect(void);


	//----------------------------------------------------
	// 	   Quert날리고 결과는 없음.
	//----------------------------------------------------
	bool		Query(WCHAR* stringFormat, ...);
	bool		Query(const WCHAR* stringFormat);

	//----------------------------------------------------
	// 	   Quert날리고 결과가 있음 FetchResult해서 결과를뽑아와야함
	//----------------------------------------------------
	bool		Query_Result(const WCHAR* stringFormat, ...);	
	bool		Query_Result(WCHAR* stringFormat);
	sql::ResultSet* FetchResult();


private:
	std::wstring m_UserID;
	std::wstring m_DBIP;
	std::wstring m_DBPORT;
	std::wstring m_DBPassword;
	std::wstring m_DBName;

	sql::Statement* m_Query;
	sql::ResultSet* m_Result;
	LARGE_INTEGER m_QueryStartTime;
	LARGE_INTEGER m_QueryEndTime;
	LARGE_INTEGER m_QPF;


	static sql::Driver* m_Driver;
	sql::Connection* m_Connect;

};

class TLSDBConnector
{

	enum
	{
		QUERY_MAX_LEN = 2048,
		LOG_TIME = 500
	};

public:
	TLSDBConnector(const WCHAR* dbIP, const WCHAR* dbPort, const WCHAR* user, const WCHAR* password, const WCHAR* dbName, int threadCount);
	~TLSDBConnector();
	void Crash();
public:

	//----------------------------------------------------
	// 	   Quert날리고 결과는 없음.
	//----------------------------------------------------
	bool		Query(WCHAR* stringFormat, ...);


	//----------------------------------------------------
	// 	   Quert날리고 결과가 있음 FetchResult해서 결과를뽑아와야함
	//----------------------------------------------------
	bool		Query_Result(const WCHAR* stringFormat, ...);

	sql::ResultSet* FetchResult();

	DBConnector* GetPointerTLS();
private:
	DWORD m_TlsIndex;
	DBConnector** m_DBConArray;
	LONG m_DBConIndex;

private:
	std::wstring m_UserID;

	std::wstring m_DBIP;
	std::wstring m_DBPORT;
	std::wstring m_DBPassword;
	std::wstring m_DBName;

};