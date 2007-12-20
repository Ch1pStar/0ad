/**
 * =========================================================================
 * File        : wmi.cpp
 * Project     : 0 A.D.
 * Description : wrapper for Windows Management Instrumentation
 * =========================================================================
 */

// license: GPL; see lib/license.txt

#include "precompiled.h"
#include "wmi.h"

#include <wbemidl.h>

#include "winit.h"

#pragma comment(lib, "wbemuuid.lib")

WINIT_REGISTER_EARLY_INIT(wmi_Init);
WINIT_REGISTER_EARLY_SHUTDOWN(wmi_Shutdown);


static IWbemServices* pSvc;

_COM_SMARTPTR_TYPEDEF(IWbemLocator, __uuidof(IWbemLocator));
_COM_SMARTPTR_TYPEDEF(IWbemClassObject, __uuidof(IWbemClassObject));
_COM_SMARTPTR_TYPEDEF(IEnumWbemClassObject, __uuidof(IEnumWbemClassObject));


static LibError wmi_Init()
{
	HRESULT hr;

	hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	if(FAILED(hr))
		WARN_RETURN(ERR::_1);

	hr = CoInitializeSecurity(0, -1, 0, 0, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, 0, EOAC_NONE, 0);
	if(FAILED(hr))
		WARN_RETURN(ERR::_2);

	{
		IWbemLocatorPtr pLoc = 0;
		hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&pLoc);
		if(FAILED(hr))
			WARN_RETURN(ERR::_3);

		hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), 0, 0, 0, 0, 0, 0, &pSvc);
		if(FAILED(hr))
			WARN_RETURN(ERR::_4);
	}

	hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, 0, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, 0, EOAC_NONE);
	if(FAILED(hr))
		WARN_RETURN(ERR::_5);

	return INFO::OK;
}


static LibError wmi_Shutdown()
{
	// the memory pointed to by pSvc is already invalidated at this point;
	// maybe some other module has already wiped out COM?
	//pSvc->Release();
	//CoUninitialize();
	return INFO::OK;
}


LibError wmi_GetClass(const char* className, WmiMap& wmiMap)
{
	HRESULT hr;

	IEnumWbemClassObjectPtr pEnum = 0;
	char query[200];
	sprintf_s(query, ARRAY_SIZE(query), "SELECT * FROM %s", className);
	hr = pSvc->ExecQuery(L"WQL", _bstr_t(query), WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY, 0, &pEnum);
	if(FAILED(hr))
		WARN_RETURN(ERR::FAIL);
	debug_assert(pEnum);

	for(;;)
	{
		IWbemClassObjectPtr pObj = 0;
		ULONG numReturned = 0;
		hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &numReturned);
		if(FAILED(hr))
			WARN_RETURN(ERR::FAIL);
		if(numReturned == 0)
			break;
		debug_assert(pEnum);

		pObj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
		for(;;)
		{
			BSTR name;
			VARIANT value;
			VariantInit(&value);
			if(pObj->Next(0, &name, &value, 0, 0) != WBEM_S_NO_ERROR)
			{
				SysFreeString(name);
				break;
			}
			wmiMap[name] = value;
			SysFreeString(name);
		}
	}

	return INFO::OK;
}
