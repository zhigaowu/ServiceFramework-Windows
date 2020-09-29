
#ifndef __SERVICE_INTERFACE_H__
#define __SERVICE_INTERFACE_H__

#include "spdlog/logging.h"

#define SERVICE_MODULE_C_API extern "C" __declspec(dllexport)

#define Service_Module_Failed -1
#define Service_Module_Success 0

typedef int(__cdecl *IsServiceRunning)();

SERVICE_MODULE_C_API int __cdecl Service_Create(std::shared_ptr<spdlog::logger>& log, const std::string& root, const std::string& config_file_path);

SERVICE_MODULE_C_API void __cdecl Service_Run(IsServiceRunning Is_Service_Running);

SERVICE_MODULE_C_API void __cdecl Service_Destroy();

#endif
