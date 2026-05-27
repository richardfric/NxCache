#include "winsvc.hpp"

#include <iostream>
#include <vector>

char szPath[MAX_PATH];
char SVCDESCRIPTION[] = "Local caching service";

LPSTR GetLastErrorText(DWORD err, LPSTR lpszBuf, DWORD dwSize)
{
	LPTSTR lpszTemp = NULL;
	DWORD dwRet = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY,
		NULL,
		err,
		LANG_NEUTRAL,
		(LPSTR)&lpszTemp,
		0,
		NULL);

	// supplied buffer is not long enough
	if (!dwRet || ((long)dwSize < (long)dwRet + 14)) {
		lpszBuf[0] = '\0';
	}
	else {
		lpszTemp[lstrlen(lpszTemp) - 2] = '\0';  //remove cr and newline character
		snprintf(lpszBuf, dwSize, "%s (0x%x)", lpszTemp, err);
	}

	if (lpszTemp)
		LocalFree((HLOCAL)lpszTemp);

	return lpszBuf;
}

DWORD install_win_service()
{
	DWORD err = 0;
	if (!GetModuleFileName(NULL, szPath, MAX_PATH)) {
		err = GetLastError();
		std::cerr << "Cannot install service, error:" << err << std::endl;
		return err;
	}

	// Get a handle to the SCM database. 
	SC_HANDLE schSCManager = OpenSCManager(
		NULL,                    // local computer
		NULL,                    // ServicesActive database 
		SC_MANAGER_ALL_ACCESS);  // full access rights 

	if (NULL == schSCManager) {
		err = GetLastError();
		if (err == ERROR_ACCESS_DENIED)
			std::cerr << "Insufficient rights to install service" << std::endl;
		else
			std::cerr << "OpenSCManager failed, error: " << err << std::endl;
		return err;
	}

	// Create the service
	SC_HANDLE schService = CreateService(
		schSCManager,              // SCM database 
		SVCNAME,                   // name of service 
		SVCDISPLAYNAME,            // service name to display 
		SERVICE_ALL_ACCESS,        // desired access 
		SERVICE_WIN32_OWN_PROCESS, // service type 
		SERVICE_AUTO_START,        // start type 
		SERVICE_ERROR_NORMAL,      // error control type 
		szPath,                    // path to service's binary 
		NULL,                      // no load ordering group 
		NULL,                      // no tag identifier 
		"NlaSvc\0",                // network dependency
		NULL,                      // LocalSystem account 
		NULL);                     // no password 

	if (schService != NULL) {
		SERVICE_DESCRIPTION sd{};
		sd.lpDescription = SVCDESCRIPTION;
		ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &sd);
		SERVICE_DELAYED_AUTO_START_INFO sdasi{};
		sdasi.fDelayedAutostart = TRUE;
		ChangeServiceConfig2(schService, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &sdasi);
		CloseServiceHandle(schService);
		std::cout << "Service " << SVCNAME << " installed successfully. You can start service from control panel or with command: \"net start " << SVCNAME << "\"" << std::endl;
	}
	else {
		err = GetLastError();
		std::cerr << "CreateService failed, error: " << err << std::endl;
	}

	CloseServiceHandle(schSCManager);
	return err;
}

DWORD remove_win_service()
{
	DWORD err = 0;
	char szErr[256];

	SC_HANDLE schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (schSCManager) {
		SC_HANDLE schService = OpenService(schSCManager, SVCNAME, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
		if (schService) {
			// try to stop the service
			SERVICE_STATUS ssStatus;
			if (ControlService(schService, SERVICE_CONTROL_STOP, &ssStatus)) {
				std::cout << "Stopping " << SVCNAME << "." << std::endl;
				Sleep(1000);
				DWORD counter = 0;
				while (QueryServiceStatus(schService, &ssStatus) && ++counter < 60) {
					if (ssStatus.dwCurrentState == SERVICE_STOP_PENDING) {
						std::cout << ".";
						Sleep(1000);
					}
					else
						break;
				}

				if (ssStatus.dwCurrentState == SERVICE_STOPPED)					
					std::cout << SVCNAME << " stopped." << std::endl;
				else
					std::cerr << SVCNAME << " failed to stop." << std::endl;
			}
			// remove the service
			if (DeleteService(schService)) {
				std::cout << SVCNAME << " removed." << std::endl;
			} 
			else {
				err = GetLastError();
				std::cerr << "DeleteService failed, error: " << GetLastErrorText(err, szErr, 256) << std::endl;
			}

			CloseServiceHandle(schService);
		}
		else {
			err = GetLastError();
			std::cerr << "OpenService failed, error: " << GetLastErrorText(err, szErr, 256) << std::endl;
		}

		CloseServiceHandle(schSCManager);
	}
	else {
		err = GetLastError();
		std::cerr << "OpenSCManager failed, error: " << GetLastErrorText(err, szErr, 256) << std::endl;
	}

	return err;
}
