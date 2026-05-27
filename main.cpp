#include "NxCache.h"
#include <future>
#include <boost/program_options.hpp>

#if defined(_MSC_VER)
#include "winsvc.hpp"
#include <signal.h>
#include <strsafe.h>
#include <shlobj.h>
#else
#include <boost/asio.hpp>
#include <csignal>
#include <sys/stat.h>
#endif

namespace bpo = boost::program_options;

static std::promise<int> exitPromise;

#if defined(_MSC_VER)
static std::once_flag exitFlag;
static SERVICE_STATUS svcStatus;
static SERVICE_STATUS_HANDLE svcStatusHandle;

void StopWinService()
{
    std::call_once(exitFlag, []() { exitPromise.set_value(SIGTERM); });
}

bool IsRunningAsSystemService()
{
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    return sessionId == 0;
}

void SvcReportEvent(LPTSTR szFunction)
{
    HANDLE hEventSource = RegisterEventSource(NULL, SVCNAME);
    if (NULL != hEventSource) {
        char Buffer[80];
        StringCchPrintf(Buffer, 80, "%s failed with %d", szFunction, GetLastError());

        LPCSTR lpszStrings[2];
        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;
        ReportEvent(hEventSource, // event log handle
            EVENTLOG_ERROR_TYPE, // event type
            0,                   // event category
            0,                   // event identifier
            NULL,                // no security identifier
            2,                   // size of lpszStrings array
            0,                   // no binary data
            lpszStrings,         // array of strings
            NULL);               // no binary data

        DeregisterEventSource(hEventSource);
   }
}

void ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    // Report the status of the service to the SCM.
    svcStatus.dwCurrentState = dwCurrentState;
    svcStatus.dwWin32ExitCode = dwWin32ExitCode;
    svcStatus.dwWaitHint = dwWaitHint;
    svcStatus.dwControlsAccepted = dwCurrentState == SERVICE_START_PENDING || dwCurrentState == SERVICE_STOP_PENDING ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN;
    svcStatus.dwCheckPoint = dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED ? 0 : dwCheckPoint++;
    SetServiceStatus(svcStatusHandle, &svcStatus);
}

DWORD WINAPI SvcCtrlHandler(DWORD dwCtrl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
    switch (dwCtrl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_PRESHUTDOWN:
            ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
            StopWinService();
            [[fallthrough]];
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            break;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType)
{
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            std::call_once(exitFlag, []() { exitPromise.set_value(SIGTERM); });
            return TRUE;
        default:
            return FALSE;
    }
}

#elif defined(__linux__) || defined(__APPLE__)

int start_as_daemon()
{
    // Fork off the parent process
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    // If we got a good PID, then we can exit the parent process
    if (pid > 0) {
        return 1;
    }

    // Change the file mode mask and open any logs here
    umask(0);

    // Create a new SID for the child process
    pid_t sid = setsid();
    if (sid < 0) {
        return -1;
    }

    // Change the current working directory
    if ((chdir("/")) < 0) {
        return -1;
    }

    // Redirect the standard file descriptors
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null != -1) {
		dup2(dev_null, STDIN_FILENO);
		dup2(dev_null, STDOUT_FILENO);
		dup2(dev_null, STDERR_FILENO);
		if (dev_null > STDERR_FILENO) {
			close(dev_null);
		}
    }
    return 0;
}
#endif

int main_internal(int argc, char** argv, bool run_as_daemon = false)
{
    try {
        std::filesystem::path cache_dir;
#if defined(_MSC_VER)
        SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, szPath);
        cache_dir = szPath;
        cache_dir /= SVCNAME;
#else
        cache_dir = "/var/lib/" SVCNAME;
#endif

        bpo::variables_map options;
        bpo::options_description app_options(SVCDISPLAYNAME);
        app_options.add_options()
            ("cache-dir,c", bpo::value<std::filesystem::path>()->default_value(cache_dir), "Directory containing cache files (absolute or relative to current working directory)")
            ("port,p", bpo::value<uint16_t>()->default_value(8080u), "Port to listen on")
            ("token,t", bpo::value<std::string>()->default_value(TOKEN), "Authentication token");

        if (!run_as_daemon) {
            app_options.add_options()
#if defined(_MSC_VER)
                ("register-service", "Register itself as Windows service")
                ("unregister-service", "Unregister itself as Windows service")
#else
                ("daemon", "Run as daemon")
#endif
                ("help,h", "Print this help message and exit");

            try {
                bpo::parsed_options optparsed = bpo::command_line_parser(argc, argv).options(app_options).run();
                bpo::store(optparsed, options);
            }
            catch (const boost::program_options::error& e) {
                std::cerr << "Error parsing command line: " << e.what() << std::endl;
                return EXIT_FAILURE;
            }

            if (options.count("help")) {
                std::cout << app_options << std::endl;
                return EXIT_SUCCESS;
            }
#if defined(_MSC_VER)
            else if (options.count("register-service"))
            {
                return install_win_service();
            }
            else if (options.count("unregister-service"))
            {
                return remove_win_service();
            }
#else
            run_as_daemon = options.count("daemon");
#endif
        }

        if (run_as_daemon) {
#if defined(_MSC_VER)
            DWORD len = GetModuleFileName(NULL, szPath, MAX_PATH);
            if (len == 0 || len == MAX_PATH) {
                return EXIT_FAILURE;
            }
            std::filesystem::path config_file = szPath;
            config_file = config_file.parent_path() / "config.ini";
#else
            int ret = start_as_daemon();
            if (ret < 0) {
                std::cerr << "Error running as daemon." << std::endl;
                return EXIT_FAILURE;
            }
            else if (ret == 1) {
                return EXIT_SUCCESS;
            }

            std::filesystem::path config_file = "/etc/" SVCNAME;
#endif
            if (std::filesystem::exists(config_file)) {
                try {
                    std::ifstream cfg_stream(config_file);
                    bpo::store(bpo::parse_config_file<char>(cfg_stream, app_options, true), options);
                }
                catch (const std::exception& e) {
                    std::cerr << e.what() << std::endl;
                    return EXIT_FAILURE;
                }
                catch (...) {
                    std::cerr << "Unknown exception caught" << std::endl;
                    return EXIT_FAILURE;
                }
            }
        }

        cache_dir = options["cache-dir"].as<std::filesystem::path>();
        if (cache_dir.is_relative())
            cache_dir = std::filesystem::current_path() / cache_dir;

        std::filesystem::create_directories(cache_dir);

		std::string url = std::format("http://127.0.0.1:{}", options["port"].as<uint16_t>());
        NxCacheService service(utility::conversions::to_string_t(url), options["token"].as<std::string>(), cache_dir);
        service.open();

        if (run_as_daemon) {
#if defined(_MSC_VER)
            ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
#endif
            exitPromise.get_future().wait();
#if defined(_MSC_VER)
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
#endif
        }
        else {
#if defined(__linux__) || defined(__APPLE__)
            boost::asio::thread_pool pool;
            auto sig_set = std::make_shared<boost::asio::signal_set>(pool.get_executor());
            sig_set->add(SIGINT);
            sig_set->add(SIGTERM);
            sig_set->add(SIGHUP);

            auto handler = [sig_set](const boost::system::error_code& err, int signal) {
                exitPromise.set_value(signal);
                sig_set->cancel();
                };
            sig_set->async_wait(handler);
#endif

            int signal = exitPromise.get_future().get();
            std::cout << "Exiting with signal " << signal << std::endl;
        }

        service.close();
        return EXIT_SUCCESS;
    } catch( const std::exception& e ) {
        std::cerr << e.what() << std::endl;
    } catch( ... ) {
        std::cerr << "Unknown exception caught" << std::endl;
    }

    return EXIT_FAILURE;
}

#if defined(_MSC_VER)
char cRegisterServiceCtrlHandlerEx[] = "cRegisterServiceCtrlHandlerEx";
char cStartServiceCtrlDispatcher[] = "StartServiceCtrlDispatcher";

void service_main(int argc, char** argv)
{
   bool is_win_service = IsRunningAsSystemService();
   if (is_win_service) {
      svcStatusHandle = RegisterServiceCtrlHandlerEx(SVCNAME, SvcCtrlHandler, NULL);
      if (!svcStatusHandle) {
         SvcReportEvent(cRegisterServiceCtrlHandlerEx);
         return;
      }

      // These SERVICE_STATUS members remain as set here
      svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
      svcStatus.dwServiceSpecificExitCode = 0;

      // Report initial status to the SCM
      ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
   }

   int exit_code = main_internal(argc, argv, is_win_service);
   if (is_win_service && exit_code != EXIT_SUCCESS) {
       ReportSvcStatus(SERVICE_STOPPED, static_cast<DWORD>(exit_code), 0);
   }
}

SERVICE_TABLE_ENTRY DispatchTable[] = {
   { (LPSTR)SVCNAME, (LPSERVICE_MAIN_FUNCTION)service_main },
   { NULL, NULL }
};
#endif

int main(int argc, char** argv)
{
#if defined(_MSC_VER)
    bool is_win_service = IsRunningAsSystemService();
    if (is_win_service) {
        if (!StartServiceCtrlDispatcher(DispatchTable)) {
            SvcReportEvent(cStartServiceCtrlDispatcher);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    else {
        SetConsoleCtrlHandler(HandlerRoutine, TRUE);
    }
    return main_internal(argc, argv, is_win_service);
#else
    return main_internal(argc, argv);
#endif
}
