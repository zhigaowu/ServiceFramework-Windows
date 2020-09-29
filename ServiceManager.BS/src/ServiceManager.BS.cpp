// ServiceManager.BS.cpp : 定义 DLL 应用程序的导出函数。
//

#include "Http/HttpService.h"

#include "ServiceInterface.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include <algorithm>

#include <fstream>

HttpService http;

#define SERVICE_DEFINITION_FILE_KEY "service.definition.file"
#define SERVICES_FILE_KEY "services.file"

#define SERVICE_DEFINITION_FILE_VALUE "data\\ServiceDefinition.json"
#define SERVICES_FILE_VALUE "conf\\Services.json"

SERVICE_MODULE_C_API int __cdecl Service_Create(std::shared_ptr<spdlog::logger>& log, const std::string& root, const std::string& config_file_path)
{
    do 
    {
        if (config_file_path.empty())
        {
            return http.Create("", 8180, root + "http\\", root + SERVICE_DEFINITION_FILE_VALUE, root + SERVICES_FILE_VALUE, root, log);
        }

        std::ifstream ifs(config_file_path);
        ifs.seekg(0, std::ios::end);
        size_t length = (size_t)ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        std::vector<char> content(length);
        ifs.read(content.data(), length);

        rapidjson::Document doc;
        doc.Parse(content.data());

        std::string address;
        if (doc.HasMember("address"))
        {
            if (!doc["address"].IsString())
            {
                log->error("create http service failed: http serve address should be string");
                break;
            }
            address = doc["address"].GetString();
        }

        if (!doc.HasMember("port"))
        {
            log->error("create http service failed: http serve port not provided");
            break;
        }
        if (!doc["port"].IsInt())
        {
            log->error("create http service failed: http serve port should be integer");
            break;
        }
        int port = doc["port"].GetInt();

        std::string directory = "http\\";
        if (doc.HasMember("directory"))
        {
            if (!doc["directory"].IsString())
            {
                log->error("create http service failed: http serve directory should be string");
                break;
            }
            directory = doc["directory"].GetString();

            std::for_each(directory.begin(), directory.end(), [](char& ch) { ch = ch == '/' ? '\\' : ch; });
        }

        std::string service_def_file = SERVICE_DEFINITION_FILE_VALUE;
        if (doc.HasMember(SERVICE_DEFINITION_FILE_KEY))
        {
            if (!doc[SERVICE_DEFINITION_FILE_KEY].IsString())
            {
                log->error("create http service failed: http service definition file should be string");
                break;
            }
            service_def_file = doc[SERVICE_DEFINITION_FILE_KEY].GetString();

            std::for_each(service_def_file.begin(), service_def_file.end(), [](char& ch) { ch = ch == '/' ? '\\' : ch; });
        }
        else
        {
            log->error("create http service failed: http service definition file does not exist");
            break;
        }

        std::string services_file = SERVICES_FILE_VALUE;
        if (doc.HasMember(SERVICES_FILE_KEY))
        {
            if (!doc[SERVICES_FILE_KEY].IsString())
            {
                log->error("create http service failed: http services file should be string");
                break;
            }
            services_file = doc[SERVICES_FILE_KEY].GetString();

            std::for_each(services_file.begin(), services_file.end(), [](char& ch) { ch = ch == '/' ? '\\' : ch; });
        }

        directory = root + directory;
        if (_access(directory.c_str(), 00) != 0 && _mkdir(directory.c_str()) != 0)
        {
            log->error("create http service failed: http serve directory({}) does not exist", directory.c_str());
            break;
        }

        service_def_file = root + service_def_file;
        if (_access(service_def_file.c_str(), 00) != 0)
        {
            log->error("create http service failed: service definition file({}) does not exist", service_def_file.c_str());
            break;
        }

        services_file = root + services_file;

        return http.Create(address, port, directory, service_def_file, services_file, root, log);
    } while (false);

    return Service_Module_Failed;
}

SERVICE_MODULE_C_API void __cdecl Service_Run(IsServiceRunning Is_Service_Running)
{
    while (Is_Service_Running() != 0)
    {
        http.Serve();
    }
}

SERVICE_MODULE_C_API void __cdecl Service_Destroy()
{
    http.Destroy();
}

