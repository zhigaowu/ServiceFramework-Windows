
#include <windows.h>

#include "spdlog/logging.h"

#include <string>
#include <algorithm>

#include <unordered_map>

IMPLEMENT_SPDLOG(logging);

typedef std::unordered_map<std::string, std::string> ServiceParameters;

static struct  
{
    std::string        path;
    std::string        exec;
    ServiceParameters params;

    SERVICE_STATUS        status = { 0 };
    SERVICE_STATUS_HANDLE handle = NULL;
    HANDLE                event = INVALID_HANDLE_VALUE;
} service;

#define SERVICE_NAME  ("My Sample Service")  

static std::string GetLastErrorString()
{
    std::string err_str;
    DWORD code = GetLastError();
    LPVOID lpMsgBuf = NULL;
    DWORD bufLen = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);

    if (lpMsgBuf)
    {
        err_str = (LPCSTR)lpMsgBuf;
        LocalFree(lpMsgBuf);
    }
    return err_str;
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:

        if (service.status.dwCurrentState != SERVICE_RUNNING)
            break;

        /*
        * Perform tasks necessary to stop the service here
        */

        logging::logger()->info("stopping service ...");

        service.status.dwControlsAccepted = 0;
        service.status.dwCurrentState = SERVICE_STOP_PENDING;
        service.status.dwWin32ExitCode = 0;
        service.status.dwCheckPoint = 4;

        if (SetServiceStatus(service.handle, &service.status) == FALSE)
        {
            logging::logger()->error("set service status(stopping) failed: {}", GetLastErrorString().c_str());
        }

        // This will signal the worker thread to start shutting down
        SetEvent(service.event);

        break;

    default:
        break;
    }
}

typedef int(__cdecl *IsServiceRunning)();
typedef int(__cdecl *Service_Create)(std::shared_ptr<spdlog::logger>&, const std::string&, const std::string&);
typedef void(__cdecl *Service_Run)(IsServiceRunning);
typedef void(__cdecl *Service_Destroy)();

int __cdecl IsServiceRunning_Impl()
{
    return 1;
}

DWORD WINAPI ServiceWorkerThread(LPVOID)
{
    do
    {
        ServiceParameters::iterator found = service.params.find("service.lib");
        if (found == service.params.end())
        {
            logging::logger()->error("module parameter(--service.lib) is not provided");
            break;
        }

        std::string service_lib = service.path + found->second;
        if (_access(service_lib.c_str(), 00) != 0)
        {
            logging::logger()->error("service({}) does not exist", found->second.c_str());
            break;
        }

        std::string service_config;
        found = service.params.find("service.config");
        if (found != service.params.end())
        {
            service_config = service.path + found->second;
        }

        if (!service_config.empty() && _access(service_config.c_str(), 00) != 0)
        {
            logging::logger()->error("service configuration({}) does not exist", found->second.c_str());
            break;
        }

        HMODULE module = LoadLibrary(service_lib.c_str());
        if (NULL == module)
        {
            logging::logger()->error("load service({}) failed: {}", service_lib.c_str(), GetLastErrorString().c_str());
            break;
        }

        do
        {
            Service_Create Create = (Service_Create)GetProcAddress(module, "Service_Create");
            Service_Run Run = (Service_Run)GetProcAddress(module, "Service_Run");
            Service_Destroy Destroy = (Service_Destroy)GetProcAddress(module, "Service_Destroy");

            if (!Create || !Run || !Destroy)
            {
                logging::logger()->error("service({}) interface is not compelete", service_lib.c_str());
                break;
            }

            if (Create(logging::logger(), service.path, service_config) != 0)
            {
                logging::logger()->info("stopping service ...");
                break;
            }

            /*
            * Perform main service function here
            */
            Run(IsServiceRunning_Impl);

            Destroy();
        } while (false);

        FreeLibrary(module);
    } while (false);

    return ERROR_SUCCESS;
}

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv)
{
    do
    {
        spdlog::level::level_enum log_level = spdlog::level::info;
        static std::unordered_map<std::string, int> log_level_name_to_enum = {
            { "trace", spdlog::level::trace },
            { "debug", spdlog::level::debug },
            { "info", spdlog::level::info },
            { "warn", spdlog::level::warn },
            { "error", spdlog::level::err },
            { "fatal", spdlog::level::critical },
            { "off", spdlog::level::off }
        };
        ServiceParameters::iterator found = service.params.find("log.level");
        if (found != service.params.end())
        {
            std::unordered_map<std::string, int>::iterator lfound = log_level_name_to_enum.find(found->second);
            if (lfound != log_level_name_to_enum.end())
            {
                log_level = (spdlog::level::level_enum)lfound->second;
            }
        }

        if (log_level != spdlog::level::off)
        {
            std::string log_path = service.path + "logs\\";
            if (_access(log_path.c_str(), 00) != 0 && _mkdir(log_path.c_str()) != 0)
            {
                logging::logger()->error("log path(logs\\) does not exist");
                break;
            }

            std::string log_name = argv[0];
            std::string::size_type pos = log_name.find_last_of('/');
            if (std::string::npos == pos)
            {
                pos = log_name.find_last_of('\\');
            }
            if (std::string::npos != pos)
            {
                log_name = log_name.substr(pos + 1);
            }

            found = service.params.find("log.name");
            if (found != service.params.end())
            {
                log_name = found->second;
            }
            // replace invalid char
            std::for_each(log_name.begin(), log_name.end(), [](char& ch) {
                if (!std::isdigit(ch) && !std::isalpha(ch) && '-' != ch && '_' != ch && '.' != ch)
                {
                    ch = '-';
                }
            });

            // process log suffix
            pos = log_name.find_last_of('.');
            if (std::string::npos != pos)
            {
                std::string ext = log_name.substr(pos);
                std::for_each(ext.begin(), ext.end(), [](char& ch) {
                    if (ch >= 'A' && ch <= 'Z')
                    {
                        ch = ch + ('a' - 'A');
                    }
                });

                if (".txt" != ext && ".log" != ext)
                {
                    log_name = log_name.substr(0, pos) + ".log";
                }
            }
            else
            {
                log_name = log_name + ".log";
            }

            std::string log_type = "daily";
            found = service.params.find("log.type");
            if (found != service.params.end())
            {
                log_type = found->second;
            }

            int log_size = 80;
            found = service.params.find("log.size");
            if (found != service.params.end() && !found->second.empty())
            {
                log_size = atoi(found->second.c_str());
                if (log_size <= 0)
                {
                    log_size = 80;
                }
            }

            int log_keep = 7;
            found = service.params.find("log.keep");
            if (found != service.params.end() && !found->second.empty())
            {
                log_keep = atoi(found->second.c_str());
                if (log_keep <= 0)
                {
                    log_keep = 7;
                }
            }

            if ("daily" == log_type)
            {
                logging::initialize_daily(log_path + log_name, 0, 0, log_keep, log_level);
            } 
            else
            {
                logging::initialize_rotate(log_path + log_name, (size_t)log_size, log_keep, log_level);
            }
        }

        logging::logger()->info("**************** parameters ******************");
        found = service.params.begin();
        while (found != service.params.end())
        {
            logging::logger()->info("{}={}", found->first.c_str(), found->second.c_str());
            ++found;
        }
        logging::logger()->info("***********************************************");

        // Register our service control handler with the SCM
        /*service.handle = RegisterServiceCtrlHandler(argv[0], ServiceCtrlHandler);

        if (service.handle == NULL)
        {
            logging::logger()->error("register service failed: {}", GetLastErrorString().c_str());
            break;
        }

        // Tell the service controller we are starting
        ZeroMemory(&service.status, sizeof(service.status));
        service.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        service.status.dwControlsAccepted = 0;
        service.status.dwCurrentState = SERVICE_START_PENDING;
        service.status.dwWin32ExitCode = 0;
        service.status.dwServiceSpecificExitCode = 0;
        service.status.dwCheckPoint = 0;

        if (SetServiceStatus(service.handle, &service.status) == FALSE)
        {
            logging::logger()->error("set service status(starting) failed: {}", GetLastErrorString().c_str());
            break;
        }*/

        /*
        * Perform tasks necessary to start the service here
        */

        // Create a service stop event to wait on later
        service.event = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (service.event == NULL)
        {
            // Error creating event
            // Tell service controller we are stopped and exit
            service.status.dwControlsAccepted = 0;
            service.status.dwCurrentState = SERVICE_STOPPED;
            service.status.dwWin32ExitCode = GetLastError();
            service.status.dwCheckPoint = 1;

            logging::logger()->error("create service event failed: {}", GetLastErrorString().c_str());

            /*if (SetServiceStatus(service.handle, &service.status) == FALSE)
            {
                logging::logger()->error("set service status(stopped) failed: {}", GetLastErrorString().c_str());
            }*/
            break;
        }

        // Tell the service controller we are started
        service.status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
        service.status.dwCurrentState = SERVICE_RUNNING;
        service.status.dwWin32ExitCode = 0;
        service.status.dwCheckPoint = 0;

        /*if (SetServiceStatus(service.handle, &service.status) == FALSE)
        {
            logging::logger()->error("set service status(running) failed: {}", GetLastErrorString().c_str());
            break;
        }*/
        logging::logger()->info("starting service ...");

        // Start a thread that will perform the main task of the service
        HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

        // Wait until our worker thread exits signaling that the service needs to stop
        WaitForSingleObject(hThread, INFINITE);

        /*
        * Perform any cleanup tasks
        */
        CloseHandle(service.event);

        // Tell the service controller we are stopped
        service.status.dwControlsAccepted = 0;
        service.status.dwCurrentState = SERVICE_STOPPED;
        service.status.dwWin32ExitCode = 0;
        service.status.dwCheckPoint = 3;

        /*if (SetServiceStatus(service.handle, &service.status) == FALSE)
        {
            logging::logger()->error("set service status(stopped) failed: {}", GetLastErrorString().c_str());
        }*/
        logging::logger()->info("service stopped");
    } while (false);

    logging::deinitialize();
}

int main(int argc, TCHAR *argv[])
{
    // parse the command
    {
        service.path = argv[1];
        service.path += "bin\\";
        service.exec = argv[0];
    }

    // parse the parameter
    for (int i = 2; i < argc; ++i)
    {
        char* dict = argv[i];
        int spos = 0;
        int epos = (int)strlen(dict);
        while (epos >= spos)
        {
            if (dict[epos] == '=')
            {
                break;
            }
            --epos;
        }
     
        while (spos <= epos && dict[spos] == '-')
        {
            ++spos;
        }

        if (spos < epos)
        {
            service.params[std::string(dict + spos, dict + epos)] = std::string(dict + epos + 1);
        }
    }

    ServiceMain(0, argv);
    return 0;
}





