// ServiceSample.cpp : 定义 DLL 应用程序的导出函数。
//

#include "ServiceInterface.h"

#include <thread>
#include <chrono>

static std::shared_ptr<spdlog::logger> _logger(nullptr);

SERVICE_MODULE_C_API int __cdecl Service_Create(std::shared_ptr<spdlog::logger>& log, const std::string& root, const std::string& config_file_path)
{
    _logger = log;
    log->info("configuration file({})", config_file_path.c_str());
    log->info("create service success");

    return Service_Module_Success;
}

SERVICE_MODULE_C_API void __cdecl Service_Run(IsServiceRunning Is_Service_Running)
{
    while (Is_Service_Running() != 0)
    {
        _logger->info("service working ...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

SERVICE_MODULE_C_API void __cdecl Service_Destroy()
{
    _logger->info("destroy service success");
}


