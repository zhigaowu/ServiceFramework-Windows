
#include "HttpService.h"

#include "ServiceInterface.h"

#include "StringUtil.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <algorithm>
#include <sstream>
#include <regex>

static const char* Service_Prototype_File = "prototype.json";
static const char* Service_Upgrade_File = "upgrade.zip";
static const char* Service_Tmp_Dir = "tmp\\";

static char Service_Status_Name[][16] = {
    "Unknown",
    "Stopped",
    "Starting",
    "Stopping",
    "Running"
};

static const int Service_Option_Lib       = 0;
static const int Service_Option_Config    = 1;
static const int Service_Option_Log_Path  = 2;
static const int Service_Option_Log_Name  = 3;
static const int Service_Option_Log_Level = 4;
static const int Service_Option_Log_Type  = 5;
static const int Service_Option_Log_Size  = 6;
static const int Service_Option_Log_Keep  = 7;

typedef std::unordered_map<std::string, int> Service_Options_Type;

static Service_Options_Type Service_Options = {
    { "--service.lib", Service_Option_Lib },
    { "--service.config", Service_Option_Config },
    { "--log.name", Service_Option_Log_Name },
    { "--log.level", Service_Option_Log_Level },
    { "--log.type", Service_Option_Log_Type },
    { "--log.size", Service_Option_Log_Size },
    { "--log.keep", Service_Option_Log_Keep }
};

std::shared_ptr<spdlog::logger> HttpService::_logger;

std::shared_ptr<spdlog::logger>& HttpService::logger()
{
    return HttpService::_logger;
}

HttpService::HttpService()
    : _mgr(), _opts()
    , _definitions_path(), _service_definitions()
    , _services_path(), _service_instances()
    , _root()
    , _package()
{
    _opts.document_root = nullptr;
    _opts.enable_directory_listing = "no";
}

HttpService::~HttpService()
{
}

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

static void GetLastErrorString(rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    DWORD code = GetLastError();
#ifdef UTF8_SUPPORTED
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
        LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
        err.SetString(lpMsgStr, allo);

        LocalFree(lpMsgBuf);
    }
#else
    switch (code)
    {
    case ERROR_SERVICE_REQUEST_TIMEOUT:
    {
        err.SetString("Service operation timeout", allo);
        break;
    }
    case ERROR_SERVICE_NO_THREAD:
    {
        err.SetString("Service thread creation failed", allo);
        break;
    }
    case ERROR_SERVICE_DATABASE_LOCKED:
    {
        err.SetString("Service database locked", allo);
        break;
    }
    case ERROR_INVALID_SERVICE_LOCK:
    {
        err.SetString("Service database lock is invalid", allo);
        break;
    }
    case ERROR_EXCEPTION_IN_SERVICE:
    {
        err.SetString("Service unknown exception", allo);
        break;
    }
    case ERROR_CIRCULAR_DEPENDENCY:
    {
        err.SetString("Service dependency has circular", allo);
        break;
    }
    case ERROR_DATABASE_DOES_NOT_EXIST:
    {
        err.SetString("Service database does not exist", allo);
        break;
    }
    case ERROR_ALREADY_RUNNING_LKG:
    {
        err.SetString("Service operation failed due to the system is currently running with the last-known-good configuration", allo);
        break;
    }
    case ERROR_SERVICE_DEPENDENCY_DELETED:
    {
        err.SetString("Service operation failed due to the dependency service does not exist or has been marked for deletion", allo);
        break;
    }
    case ERROR_BOOT_ALREADY_ACCEPTED:
    {
        err.SetString("Service operation failed due to the current boot has already been accepted for use as the last-known-good control set", allo);
        break;
    }
    case ERROR_SERVICE_NEVER_STARTED:
    {
        err.SetString("Service operation failed due to no attempts to start the service have been made since the last boot", allo);
        break;
    }
    case ERROR_PROCESS_ABORTED:
    {
        err.SetString("Service terminated unexpectedly", allo);
        break;
    }
    case ERROR_SERVICE_DEPENDENCY_FAIL:
    {
        err.SetString("Service dependency failed to start", allo);
        break;
    }
    case ERROR_SERVICE_LOGON_FAILED:
    {
        err.SetString("Service failed due to a logon failure", allo);
        break;
    }
    case ERROR_SERVICE_DOES_NOT_EXIST:
    {
        err.SetString("Service does not exist", allo);
        break;
    }
    case ERROR_SERVICE_START_HANG:
    {
        err.SetString("Service hung in a start-pending state", allo);
        break;
    }
    case ERROR_SERVICE_EXISTS:
    {
        err.SetString("Service exists already", allo);
        break;
    }
    case ERROR_SERVICE_ALREADY_RUNNING:
    {
        err.SetString("Service is started already", allo);
        break;
    }
    case ERROR_SERVICE_DISABLED:
    {
        err.SetString("Service is disabled", allo);
        break;
    }
    case ERROR_SERVICE_MARKED_FOR_DELETE:
    {
        err.SetString("Service has been marked for deletion", allo);
    break;
    }
    case ERROR_SERVICE_NOT_ACTIVE:
    {
        err.SetString("Service is not started yet", allo);
        break;
    }
    case ERROR_SERVICE_CANNOT_ACCEPT_CTRL:
    {
        err.SetString("Service can not accept control message this time", allo);
        break;
    }
    default:
        char buff[256] = { 0 };
        snprintf(buff, sizeof(buff), "Service failed due to unknown error(%ld)", code);
        err.SetString(buff, allo);
        break;
    }
#endif
}

struct file_writer_data {
    FILE *fp;
    size_t bytes_written;
};

static void ev_upload(struct mg_connection *nc, int ev, void *p) 
{
    HttpService* http = (HttpService*) nc->user_data;
    struct mg_http_multipart_part *mp = (struct mg_http_multipart_part *) p;

    switch (ev) {
    case MG_EV_HTTP_PART_BEGIN: 
    {
        if (!http->begin_upload_package()) 
        {
            mg_printf(nc, "%s",
                "HTTP/1.1 500 Failed to open a file\r\n"
                "Content-Length: 0\r\n\r\n");
            nc->flags |= MG_F_SEND_AND_CLOSE;
        }
        break;
    }
    case MG_EV_HTTP_PART_DATA: 
    {
        if (!http->write_upload_package(mp->data.p, mp->data.len))
        {
            mg_printf(nc, "%s",
                "HTTP/1.1 500 Failed to write to a file\r\n"
                "Content-Length: 0\r\n\r\n");
            nc->flags |= MG_F_SEND_AND_CLOSE;
        }
        break;
    }
    case MG_EV_HTTP_PART_END: 
    {
        mg_printf(nc,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n\r\n"
            "Uploaded %ld of package(done)\n\n",
            http->end_upload_package());
        nc->flags |= MG_F_SEND_AND_CLOSE;
        break;
    }
    }
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
    struct http_message *hm = (struct http_message *)ev_data;
    HttpService* http = (HttpService*)nc->user_data;

    switch (ev)
    {
    case MG_EV_HTTP_REQUEST:
    {
        if (mg_vcmp(&hm->uri, "/service/list") == 0)
        {
            http->handle_service_list(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/definition/list") == 0)
        {
            http->handle_definition_list(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/service/detail/get") == 0)
        {
            http->handle_service_detail_get(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/service/detail/set") == 0)
        {
            http->handle_service_detail_set(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/service/create") == 0)
        {
            http->handle_service_create(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/service/delete") == 0)
        {
            http->handle_service_delete(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/service/control") == 0)
        {
            http->handle_service_control(nc, hm); /* Handle RESTful call */
        }
        else if (mg_vcmp(&hm->uri, "/service/upgrade") == 0)
        {
            http->handle_service_upgrade(nc, hm); /* Handle RESTful call */
        }
        else
        {
            http->handle_html(nc, hm);
        }
        break;
    }
    default:
        break;
    }
}

int HttpService::Create(const std::string& address, int port, const std::string& directory, const std::string& service_definition_config, const std::string& service_active_config, const std::string& root, std::shared_ptr<spdlog::logger>& log)
{
    HttpService::_logger = log;
    _root = root;
    _definitions_path = service_definition_config;

    if (Service_Module_Failed == load_service_definitions())
    {
        return Service_Module_Failed;
    }

    _services_path = service_active_config;
    if (Service_Module_Failed == load_service_instances())
    {
        return Service_Module_Failed;
    }

    struct mg_bind_opts bind_opts;
    memset(&bind_opts, 0, sizeof(bind_opts));

    const char* err_str = nullptr;
    bind_opts.error_string = &err_str;

    mg_mgr_init(&_mgr, nullptr);

    char endpoint[256] = { 0 };
    snprintf(endpoint, sizeof(endpoint), "%s:%d", address.c_str(), port);

    // configure http service
    struct mg_connection *nc = mg_bind_opt(&_mgr, endpoint, ev_handler, bind_opts);
    if (nc == NULL)
    {
        _logger->error("create http service({}) failed: {}", endpoint, *bind_opts.error_string);
        return Service_Module_Failed;
    }
    nc->user_data = this;

    _opts.document_root = directory.c_str();
    mg_register_http_endpoint(nc, "/service/upload", ev_upload MG_UD_ARG(NULL));

    // Set up HTTP server parameters
    mg_set_protocol_http_websocket(nc);

    _logger->info("create http service({}) on {} success", endpoint, directory.c_str());

    return Service_Module_Success;
}

void HttpService::Serve()
{
    mg_mgr_poll(&_mgr, 1000);
}

void HttpService::Destroy()
{
    mg_mgr_free(&_mgr);

    _logger->info("destroy http service success");
}

int HttpService::load_service_definitions()
{
    WIN32_FIND_DATA ffd;

    std::string pattern_all = _definitions_path + "*.*";
    HANDLE hFind = FindFirstFile(pattern_all.c_str(), &ffd);
    if (NULL == hFind)
    {
        _logger->error("create http service failed: find service definitions failed({})", GetLastErrorString().c_str());
        return Service_Module_Failed;
    }

    do
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (ffd.cFileName[0] == '.' && (ffd.cFileName[1] == 0 || ffd.cFileName[1] == '.'))
            {
                continue;
            }

            Service_t serv;
            serv.id = ffd.cFileName;
            if ("ServiceManager.BS" == serv.id)
            {
                continue;
            }

            // check service definition file
            std::ifstream ifs(_definitions_path + serv.id + "\\" + Service_Prototype_File);
            if (ifs.is_open())
            {
                ifs.seekg(0, std::ios::end);
                size_t length = (size_t)ifs.tellg();
                ifs.seekg(0, std::ios::beg);

                std::vector<char> content(length);
                ifs.read(content.data(), length);
                ifs.close();

                rapidjson::Document serv_json;
                serv_json.Parse(content.data());
                if (!serv_json.IsObject())
                {
                    _logger->warn("service prototype of definition({}) is invalid", ffd.cFileName);
                    continue;
                }
                
                if (!serv_json.HasMember("name"))
                {
                    _logger->warn("service prototype of definition({}) is invalid: name is not provided", ffd.cFileName);
                    continue;
                }
                if (!serv_json["name"].IsString())
                {
                    _logger->warn("service prototype of definition({}) is invalid: name is not string", ffd.cFileName);
                    continue;
                }
                serv.name = util::string::trim(serv_json["name"].GetString());

                if (serv.name.empty())
                {
                    _logger->warn("service prototype of definition({}) is invalid: name is empty", ffd.cFileName);
                    continue;
                }

                if (serv_json.HasMember("description"))
                {
                    if (serv_json["description"].IsString())
                    {
                        serv.description = util::string::trim(serv_json["description"].GetString());
                    }
                    else
                    {
                        _logger->warn("service prototype of definition({}) is invalid: description is not string", ffd.cFileName);
                        continue;
                    }
                }

                serv.library.name = std::string(ffd.cFileName) + std::string(".dll");
                if (serv_json.HasMember("library"))
                {
                    if (!serv_json["library"].IsString())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: library is not string", ffd.cFileName);
                        continue;
                    }
                    serv.library.name = util::string::trim(serv_json["library"].GetString());

                    if (serv.library.name.empty())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: library is empty", ffd.cFileName);
                        continue;
                    }
                }

                if (serv_json.HasMember("configuration"))
                {
                    if (!serv_json["configuration"].IsString())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: configuration is not string", ffd.cFileName);
                        continue;
                    }
                    serv.library.config = util::string::trim(serv_json["configuration"].GetString());
                }

                if (serv_json.HasMember("version"))
                {
                    if (!serv_json["version"].IsString())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: version is not string", ffd.cFileName);
                        continue;
                    }
                    serv.library.ver = util::string::trim(serv_json["version"].GetString());

                    if (serv.library.ver.empty())
                    {
                        serv.library.ver = "unknown";
                    }
                }

                rapidjson::Value& log_json = serv_json["log"];
                if (log_json.HasMember("name"))
                {
                    if (!log_json["name"].IsString())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: log.name is not string", ffd.cFileName);
                        continue;
                    }
                    serv.log.name = util::string::trim(log_json["name"].GetString());
                }

                if (!log_json.HasMember("type"))
                {
                    _logger->warn("service prototype of definition({}) is invalid: log.type is not provided", ffd.cFileName);
                    continue;
                }
                if (!log_json["type"].IsString())
                {
                    _logger->warn("service prototype of definition({}) is invalid: log.type is not string", ffd.cFileName);
                    continue;
                }
                serv.log.type = util::string::trim(log_json["type"].GetString());

                serv.log.level = "info";
                if (log_json.HasMember("level"))
                {
                    if (!log_json["level"].IsString())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: log.level is not string", ffd.cFileName);
                        continue;
                    }
                    serv.log.level = util::string::trim(log_json["level"].GetString());
                }

                serv.log.keep = 7;
                if (log_json.HasMember("keep"))
                {
                    if (!log_json["keep"].IsNumber())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: log.keep is not number", ffd.cFileName);
                        continue;
                    }
                    serv.log.keep = log_json["keep"].GetInt();
                }
                
                serv.log.size = 7;
                if (log_json.HasMember("size"))
                {
                    if (!log_json["size"].IsNumber())
                    {
                        _logger->warn("service prototype of definition({}) is invalid: log.size is not number", ffd.cFileName);
                        continue;
                    }
                    serv.log.size = log_json["size"].GetInt();
                }

                _service_definitions.emplace_back(serv);
            }
            else
            {
                _logger->warn("service prototype of definition({}) does not exist", ffd.cFileName);
            }
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    FindClose(hFind);

    return Service_Module_Success;
}

int HttpService::load_service_instances()
{
    do 
    {
        std::ifstream ifs(_services_path);
        if (!ifs.is_open())
        {
            break;
        }
        ifs.seekg(0, std::ios::end);
        size_t length = (size_t)ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        std::vector<char> content(length);
        ifs.read(content.data(), length);
        ifs.close();

        rapidjson::Document instances_json;
        instances_json.Parse(content.data());
        rapidjson::Type jt = instances_json.GetType();
        if (rapidjson::kNullType == jt)
        {
            break;
        }

        if (rapidjson::kObjectType != jt)
        {
            _logger->error("create http service failed: invalid services file({})", _services_path.c_str());
            return Service_Module_Failed;
        }

        if (instances_json.Empty())
        {
            break;
        }

        if (!instances_json.HasMember("services"))
        {
            _logger->error("create http service failed: invalid services file({}), services field is not provided", _services_path.c_str());
            return Service_Module_Failed;
        }

        rapidjson::Value json = instances_json["services"].GetArray();
        jt = json.GetType();
        if (rapidjson::kArrayType != jt)
        {
            _logger->error("create http service failed: invalid services file({}), services field is not array", _services_path.c_str());
            return Service_Module_Failed;
        }

        for (rapidjson::Value& serv_json : json.GetArray())
        {
            if (serv_json.IsObject() && serv_json.Empty())
            {
                continue;
            }

            Service_t serv;
            if (!serv_json.HasMember("id"))
            {
                _logger->warn("service is invalid: id is not provided");
                continue;
            }
            if (!serv_json["id"].IsString())
            {
                _logger->warn("service is invalid: id is not string");
                continue;
            }
            serv.id = util::string::trim(serv_json["id"].GetString());

            if (!serv_json.HasMember("name"))
            {
                _logger->warn("service({}) is invalid: name is not provided", serv.id.c_str());
                continue;
            }
            if (!serv_json["name"].IsString())
            {
                _logger->warn("service({}) is invalid: name is not string", serv.id.c_str());
                continue;
            }
            serv.name = util::string::trim(serv_json["name"].GetString());

            if (serv.name.empty())
            {
                _logger->warn("service({}) is invalid: name is empty", serv.id.c_str());
                continue;
            }

            if (serv_json.HasMember("description"))
            {
                if (serv_json["description"].IsString())
                {
                    serv.description = util::string::trim(serv_json["description"].GetString());
                }
                else
                {
                    _logger->warn("service({}) is invalid: description is not string", serv.name.c_str());
                    continue;
                }
            }

            if (!serv_json.HasMember("library"))
            {
                _logger->warn("service({}) is invalid: library is not provided", serv.name.c_str());
                continue;
            }

            if (!serv_json["library"].IsString())
            {
                _logger->warn("service({}) is invalid: library is not string", serv.name.c_str());
                continue;
            }
            serv.library.name = util::string::trim(serv_json["library"].GetString());

            if (serv.library.name.empty())
            {
                _logger->warn("service({}) is invalid: library is empty", serv.name.c_str());
                continue;
            }

            if (serv_json.HasMember("configuration"))
            {
                if (!serv_json["configuration"].IsString())
                {
                    _logger->warn("service({}) is invalid: configuration is not string", serv.name.c_str());
                    continue;
                }
                serv.library.config = util::string::trim(serv_json["configuration"].GetString());
            }

            if (serv_json.HasMember("version"))
            {
                if (!serv_json["version"].IsString())
                {
                    _logger->warn("service({}) is invalid: version is not string", serv.name.c_str());
                    continue;
                }
                serv.library.ver = util::string::trim(serv_json["version"].GetString());

                if (serv.library.ver.empty())
                {
                    serv.library.ver = "unknown";
                }
            }

            rapidjson::Value& log_json = serv_json["log"];
            if (log_json.HasMember("name"))
            {
                if (!log_json["name"].IsString())
                {
                    _logger->warn("service({}) is invalid: log.name is not string", serv.name.c_str());
                    continue;
                }
                serv.log.name = util::string::trim(log_json["name"].GetString());
            }

            if (!log_json.HasMember("type"))
            {
                _logger->warn("service({}) is invalid: log.type is not provided", serv.name.c_str());
                continue;
            }
            if (!log_json["type"].IsString())
            {
                _logger->warn("service({}) is invalid: log.type is not string", serv.name.c_str());
                continue;
            }
            serv.log.type = util::string::trim(log_json["type"].GetString());

            serv.log.level = "info";
            if (log_json.HasMember("level"))
            {
                if (!log_json["level"].IsString())
                {
                    _logger->warn("service({}) is invalid: log.level is not string", serv.name.c_str());
                    continue;
                }
                serv.log.level = util::string::trim(log_json["level"].GetString());
            }

            serv.log.keep = 7;
            if (log_json.HasMember("keep"))
            {
                if (!log_json["keep"].IsNumber())
                {
                    _logger->warn("service({}) is invalid: log.keep is not number", serv.name.c_str());
                    continue;
                }
                serv.log.keep = log_json["keep"].GetInt();
            }

            serv.log.size = 7;
            if (log_json.HasMember("size"))
            {
                if (!log_json["size"].IsNumber())
                {
                    _logger->warn("service({}) is invalid: log.size is not number", serv.name.c_str());
                    continue;
                }
                serv.log.size = log_json["size"].GetInt();
            }

            serv.status_code = 0;
            serv.status_txt = Service_Status_Name[0];

            _service_instances.emplace_back(serv);
        }
    } while (false);

    return Service_Module_Success;
}

void HttpService::handle_service_list(struct mg_connection *nc, struct http_message *hm)
{
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value array(rapidjson::kArrayType);
    rapidjson::Document doc(rapidjson::kObjectType);

    code.SetInt(Service_Module_Success);

    SC_HANDLE schSCManager = OpenSCManager(
        NULL,                    // local computer
        NULL,                    // ServicesActive database 
        SC_MANAGER_ALL_ACCESS);  // full access rights 

    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwBytesNeeded;

    rapidjson::Value str_json(rapidjson::kStringType);
    rapidjson::Value num_json(rapidjson::kNumberType);
    for (const Service_t& serv : _service_instances)
    {
        rapidjson::Value serv_json(rapidjson::kObjectType);

        str_json.SetString(serv.id.c_str(), doc.GetAllocator());
        serv_json.AddMember("id", str_json, doc.GetAllocator());

        str_json.SetString(serv.name.c_str(), doc.GetAllocator());
        serv_json.AddMember("name", str_json, doc.GetAllocator());

        str_json.SetString(serv.description.c_str(), doc.GetAllocator());
        serv_json.AddMember("description", str_json, doc.GetAllocator());

        rapidjson::Value lib_json(rapidjson::kObjectType);

        str_json.SetString(serv.library.name.c_str(), doc.GetAllocator());
        lib_json.AddMember("path", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
        lib_json.AddMember("configuration", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
        lib_json.AddMember("version", str_json, doc.GetAllocator());

        serv_json.AddMember("library", lib_json, doc.GetAllocator());

        rapidjson::Value log_json(rapidjson::kObjectType);
        str_json.SetString(serv.log.name.c_str(), doc.GetAllocator());
        log_json.AddMember("name", str_json, doc.GetAllocator());

        str_json.SetString(serv.log.type.c_str(), doc.GetAllocator());
        log_json.AddMember("type", str_json, doc.GetAllocator());

        str_json.SetString(serv.log.level.c_str(), doc.GetAllocator());
        log_json.AddMember("level", str_json, doc.GetAllocator());

        num_json.SetInt(serv.log.keep);
        log_json.AddMember("keep", num_json, doc.GetAllocator());

        num_json.SetInt(serv.log.size);
        log_json.AddMember("size", num_json, doc.GetAllocator());

        serv_json.AddMember("log", log_json, doc.GetAllocator());

        if (schSCManager)
        {
            rapidjson::Value res(rapidjson::kStringType);
            do 
            {
                ssStatus.dwCurrentState = 0;

                SC_HANDLE schService = OpenService(
                    schSCManager,               // SCM database 
                    serv.name.c_str(),          // name of service 
                    SERVICE_ALL_ACCESS);        // desired access

                if (schService == NULL)
                {
                    GetLastErrorString(res, doc.GetAllocator());
                    break;
                }

                if (!QueryServiceStatusEx(
                    schService,                     // handle to service 
                    SC_STATUS_PROCESS_INFO,         // information level
                    (LPBYTE)&ssStatus,             // address of structure
                    sizeof(SERVICE_STATUS_PROCESS), // size of structure
                    &dwBytesNeeded))              // size needed if buffer is too small
                {
                    GetLastErrorString(res, doc.GetAllocator());
                    CloseServiceHandle(schService);
                    break;
                }

                res.SetString(Service_Status_Name[ssStatus.dwCurrentState], doc.GetAllocator());

                CloseServiceHandle(schService);
            } while (false);

            num_json.SetInt(ssStatus.dwCurrentState);
            serv_json.AddMember("status_code", num_json, doc.GetAllocator());
            serv_json.AddMember("status_txt", res, doc.GetAllocator());
        }
        else
        {
            num_json.SetInt(0);
            serv_json.AddMember("status_code", num_json, doc.GetAllocator());
            str_json.SetString(Service_Status_Name[0], doc.GetAllocator());
            serv_json.AddMember("status_txt", str_json, doc.GetAllocator());
        }

        array.PushBack(serv_json, doc.GetAllocator());
    }

    if (schSCManager)
    {
        CloseServiceHandle(schSCManager);
    }

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", array, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_definition_list(struct mg_connection *nc, struct http_message *hm)
{
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value array(rapidjson::kArrayType);
    rapidjson::Document doc(rapidjson::kObjectType);

    code.SetInt(Service_Module_Success);

    rapidjson::Value str_json(rapidjson::kStringType);
    rapidjson::Value num_json(rapidjson::kNumberType);
    for (const Service_t& serv : _service_definitions)
    {
        rapidjson::Value serv_json(rapidjson::kObjectType);

        str_json.SetString(serv.id.c_str(), doc.GetAllocator());
        serv_json.AddMember("id", str_json, doc.GetAllocator());

        str_json.SetString(serv.name.c_str(), doc.GetAllocator());
        serv_json.AddMember("name", str_json, doc.GetAllocator());

        str_json.SetString(serv.description.c_str(), doc.GetAllocator());
        serv_json.AddMember("description", str_json, doc.GetAllocator());

        rapidjson::Value lib_json(rapidjson::kObjectType);

        str_json.SetString(serv.library.name.c_str(), doc.GetAllocator());
        lib_json.AddMember("path", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
        lib_json.AddMember("configuration", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
        lib_json.AddMember("version", str_json, doc.GetAllocator());

        serv_json.AddMember("library", lib_json, doc.GetAllocator());

        rapidjson::Value log_json(rapidjson::kObjectType);
        str_json.SetString(serv.log.name.c_str(), doc.GetAllocator());
        log_json.AddMember("name", str_json, doc.GetAllocator());

        str_json.SetString(serv.log.type.c_str(), doc.GetAllocator());
        log_json.AddMember("type", str_json, doc.GetAllocator());

        str_json.SetString(serv.log.level.c_str(), doc.GetAllocator());
        log_json.AddMember("level", str_json, doc.GetAllocator());

        num_json.SetInt(serv.log.keep);
        log_json.AddMember("keep", num_json, doc.GetAllocator());

        num_json.SetInt(serv.log.size);
        log_json.AddMember("size", num_json, doc.GetAllocator());

        serv_json.AddMember("log", log_json, doc.GetAllocator());

        array.PushBack(serv_json, doc.GetAllocator());
    }

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", array, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_service_detail_get(struct mg_connection *nc, struct http_message *hm)
{
    char stype[32] = { 0 }, index[128] = { 0 };
    /* Get form variables */
    mg_get_http_var(&hm->body, "type", stype, sizeof(stype));
    mg_get_http_var(&hm->body, "index", index, sizeof(index));
    
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value serv_json(rapidjson::kObjectType);
    rapidjson::Document doc(rapidjson::kObjectType);

    code.SetInt(Service_Module_Success);

    rapidjson::Value str_json(rapidjson::kStringType);
    rapidjson::Value num_json(rapidjson::kNumberType);
    if (strcmp(stype, "definition") == 0)
    {
        for (const Service_t& serv : _service_definitions)
        {
            if (serv.id == index)
            {
                str_json.SetString(serv.id.c_str(), doc.GetAllocator());
                serv_json.AddMember("id", str_json, doc.GetAllocator());

                str_json.SetString(serv.name.c_str(), doc.GetAllocator());
                serv_json.AddMember("name", str_json, doc.GetAllocator());

                str_json.SetString(serv.description.c_str(), doc.GetAllocator());
                serv_json.AddMember("description", str_json, doc.GetAllocator());

                rapidjson::Value lib_json(rapidjson::kObjectType);
                str_json.SetString(serv.library.name.c_str(), doc.GetAllocator());
                lib_json.AddMember("name", str_json, doc.GetAllocator());

                str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
                lib_json.AddMember("configuration", str_json, doc.GetAllocator());

                str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
                lib_json.AddMember("version", str_json, doc.GetAllocator());

                serv_json.AddMember("library", lib_json, doc.GetAllocator());

                str_json.SetString(make_option(serv, false).c_str(), doc.GetAllocator());
                serv_json.AddMember("option", str_json, doc.GetAllocator());

                std::ifstream ifs(_definitions_path + serv.id + "\\" + serv.library.config);
                if (ifs.is_open())
                {
                    std::string str;
                    ifs.seekg(0, std::ios::end);
                    size_t length = (size_t)ifs.tellg();
                    ifs.seekg(0, std::ios::beg);
                    str.resize(length);

                    ifs.read((char*)str.data(), length);
                    ifs.close();

                    str_json.SetString(str.c_str(), doc.GetAllocator());
                }
                else
                {
                    str_json.SetString("", doc.GetAllocator());
                }
                serv_json.AddMember("configuration", str_json, doc.GetAllocator());

                break;
            }
        }

        if (serv_json.Empty())
        {
            std::stringstream ss;
            ss << "Service definition(" << index << ") does not exist";
            str_json.SetString(ss.str().c_str(), doc.GetAllocator());
        }
    } 
    else
    {
        for (const Service_t& serv : _service_instances)
        {
            if (serv.name == index)
            {
                str_json.SetString(serv.id.c_str(), doc.GetAllocator());
                serv_json.AddMember("id", str_json, doc.GetAllocator());

                str_json.SetString(serv.name.c_str(), doc.GetAllocator());
                serv_json.AddMember("name", str_json, doc.GetAllocator());

                str_json.SetString(serv.description.c_str(), doc.GetAllocator());
                serv_json.AddMember("description", str_json, doc.GetAllocator());

                rapidjson::Value lib_json(rapidjson::kObjectType);
                str_json.SetString(serv.library.name.c_str(), doc.GetAllocator());
                lib_json.AddMember("name", str_json, doc.GetAllocator());

                str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
                lib_json.AddMember("configuration", str_json, doc.GetAllocator());

                str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
                lib_json.AddMember("version", str_json, doc.GetAllocator());

                serv_json.AddMember("library", lib_json, doc.GetAllocator());

                str_json.SetString(make_option(serv, false).c_str(), doc.GetAllocator());
                serv_json.AddMember("option", str_json, doc.GetAllocator());
                
                std::ifstream ifs(_root + "conf\\" + serv.library.config);
                if (ifs.is_open())
                {
                    std::string str;
                    ifs.seekg(0, std::ios::end);
                    size_t length = (size_t)ifs.tellg();
                    ifs.seekg(0, std::ios::beg);
                    str.resize(length);

                    ifs.read((char*)str.data(), length);
                    ifs.close();

                    str_json.SetString(str.c_str(), doc.GetAllocator());
                }
                else
                {
                    str_json.SetString("", doc.GetAllocator());
                }
                serv_json.AddMember("configuration", str_json, doc.GetAllocator());

                num_json.SetInt(serv.status_code);
                serv_json.AddMember("status_code", num_json, doc.GetAllocator());
                str_json.SetString(serv.status_txt.c_str(), doc.GetAllocator());
                serv_json.AddMember("status_txt", str_json, doc.GetAllocator());
                break;
            }
        }

        if (serv_json.Empty())
        {
            std::stringstream ss;
            ss << "Service(" << index << ") does not exist";
            str_json.SetString(ss.str().c_str(), doc.GetAllocator());
        }
    }

    doc.AddMember("code", code, doc.GetAllocator());
    if (serv_json.Empty())
    {
        doc.AddMember("result", str_json, doc.GetAllocator());
    } 
    else
    {
        doc.AddMember("result", serv_json, doc.GetAllocator());
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_service_detail_set(struct mg_connection *nc, struct http_message *hm)
{
    char stype[32] = { 0 }, index[128] = { 0 };
    char name[128] = { 0 };
    char option[512] = { 0 };

    std::vector<char> configuration(20 << 4);
    /* Get form variables */
    mg_get_http_var(&hm->body, "type", stype, sizeof(stype));
    mg_get_http_var(&hm->body, "index", index, sizeof(index));
    mg_get_http_var(&hm->body, "name", name, sizeof(name));
    mg_get_http_var(&hm->body, "option", option, sizeof(option));
    mg_get_http_var(&hm->body, "configuration", configuration.data(), configuration.size());

    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value res(rapidjson::kStringType);

    do
    {
        std::stringstream ss;

        if (strcmp(stype, "service") != 0)
        {
            ss << "Service operation target(" << stype << ") is not recognized";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }

        // find service definition
        Service_t* serv;
        for (Service_t& servd : _service_definitions)
        {
            if (servd.id == index)
            {
                serv = &servd;
                break;
            }
        }
        if (!serv)
        {
            ss << "Service definition(" << index << ") does not exist";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }

        serv = nullptr;
        // find existed service
        for (Service_t& servs : _service_instances)
        {
            if (servs.name == name)
            {
                serv = &servs;
                break;
            }
        }
        if (!serv)
        {
            ss << "Service(" << name << ") does not exist";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }

        Service_t tmp = *serv;
        if (!parse_option(tmp, option, res, doc.GetAllocator()))
        {
            break;
        }

        if (!create_service_configuration(tmp, configuration, res, doc.GetAllocator()))
        {
            break;
        }

        *serv = tmp;

        code.SetInt(Service_Module_Success);
        res.SetString("success", doc.GetAllocator());
    } while (false);

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", res, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_service_create(struct mg_connection *nc, struct http_message *hm)
{
    char stype[32] = { 0 }, index[128] = { 0 };
    char name[128] = { 0 };
    char option[512] = { 0 };
    
    std::vector<char> configuration(20 << 4);
    /* Get form variables */
    mg_get_http_var(&hm->body, "type", stype, sizeof(stype));
    mg_get_http_var(&hm->body, "index", index, sizeof(index));
    mg_get_http_var(&hm->body, "name", name, sizeof(name));
    mg_get_http_var(&hm->body, "option", option, sizeof(option));
    mg_get_http_var(&hm->body, "configuration", configuration.data(), configuration.size());
    
    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value res(rapidjson::kStringType);

    do 
    {
        std::stringstream ss;

        if (strcmp(stype, "definition") != 0)
        {
            ss << "Service operation target(" << stype << ") is not recognized";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }

        // find service definition
        Service_t serv;
        for (const Service_t& servd : _service_definitions)
        {
            if (servd.id == index)
            {
                serv = servd;
                break;
            }
        }
        if (serv.id.empty())
        {
            ss << "Service definition(" << index << ") does not exist";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }
        
        bool existed = false;
        // find existed service
        for (const Service_t& servs : _service_instances)
        {
            if (servs.name == name)
            {
                ss << "Service(" << name << ") has already exist";
                res.SetString(ss.str().c_str(), doc.GetAllocator());
                code.SetInt(Service_Module_Failed);
                existed = true;
                break;
            }
        }
        if (existed)
        {
            break;
        }
        serv.name = name;

        if (!parse_option(serv, option, res, doc.GetAllocator()))
        {
            break;
        }

        if (!create_service_configuration(serv, configuration, res, doc.GetAllocator()))
        {
            break;
        }

        SC_HANDLE schSCManager = OpenSCManager(
            NULL,                    // local computer
            NULL,                    // ServicesActive database 
            SC_MANAGER_ALL_ACCESS);  // full access rights 
        if (NULL == schSCManager)
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(res, doc.GetAllocator());
            break;
        }

        std::vector<char> binary(1024);
        snprintf(binary.data(), binary.size(), "%sServiceLoader.exe %s", _root.c_str(), make_option(serv, true).c_str());
        SC_HANDLE schService = CreateService(
            schSCManager,              // SCM database 
            name,                      // name of service 
            name,                      // service name to display 
            SERVICE_ALL_ACCESS,        // desired access 
            SERVICE_WIN32_OWN_PROCESS, // service type 
            SERVICE_AUTO_START,        // start type 
            SERVICE_ERROR_NORMAL,      // error control type 
            binary.data(),             // path to service's binary 
            NULL,                      // no load ordering group 
            NULL,                      // no tag identifier 
            NULL,                      // no dependencies 
            NULL,                      // LocalSystem account 
            NULL);                     // no password 

        if (schService == NULL)
        {
            GetLastErrorString(res, doc.GetAllocator());

            CloseServiceHandle(schSCManager);
            code.SetInt(Service_Module_Failed);
            break;
        }

        SERVICE_DESCRIPTION desc;
        desc.lpDescription = (char*)serv.description.c_str();
        if (!ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &desc))
        {
            GetLastErrorString(res, doc.GetAllocator());
            code.SetInt(Service_Module_Failed);

            // delete service
            DeleteService(schService);

            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            break;
        }

        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);

        code.SetInt(Service_Module_Success);
        res.SetString("success", doc.GetAllocator());

        _service_instances.push_back(serv);

        fresh_services_json(_service_instances, _services_path);
    } while (false);

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", res, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_service_delete(struct mg_connection *nc, struct http_message *hm)
{
    char name[128] = { 0 };
    mg_get_http_var(&hm->body, "name", name, sizeof(name));

    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value res(rapidjson::kStringType);

    do
    {
        bool found = false;
        for (const Service_t& servs : _service_instances)
        {
            if (servs.name == name)
            {
                found = true;
                break;
            }
        }

        std::stringstream ss;
        if (!found)
        {
            ss << "Service(" << name << ") does not exist";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }

        SC_HANDLE schSCManager = OpenSCManager(
            NULL,                    // local computer
            NULL,                    // ServicesActive database 
            SC_MANAGER_ALL_ACCESS);  // full access rights 
        if (NULL == schSCManager)
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(res, doc.GetAllocator());
            break;
        }

        SC_HANDLE schService = OpenService(
            schSCManager,              // SCM database 
            name,                      // name of service 
            SERVICE_ALL_ACCESS);       // desired access

        if (schService == NULL)
        {
            GetLastErrorString(res, doc.GetAllocator());
            code.SetInt(Service_Module_Failed);

            CloseServiceHandle(schSCManager);
            break;
        }

        SERVICE_STATUS_PROCESS ssStatus;
        DWORD dwBytesNeeded;
        if (!QueryServiceStatusEx(
            schService,                     // handle to service 
            SC_STATUS_PROCESS_INFO,         // information level
            (LPBYTE)&ssStatus,             // address of structure
            sizeof(SERVICE_STATUS_PROCESS), // size of structure
            &dwBytesNeeded))              // size needed if buffer is too small
        {
            GetLastErrorString(res, doc.GetAllocator());
            code.SetInt(Service_Module_Failed);

            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            break;
        }

        if (ssStatus.dwCurrentState != SERVICE_STOPPED)
        {
            ss << "Service state(" << Service_Status_Name[ssStatus.dwCurrentState] << ") is not stopped";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);

            CloseServiceHandle(schService);
            CloseServiceHandle(schSCManager);
            break;
        }

        if (!DeleteService(schService))
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(res, doc.GetAllocator());
        } 
        else
        {
            code.SetInt(Service_Module_Success);
            res.SetString("success", doc.GetAllocator());

            std::string lame = name;
            _service_instances.erase(std::remove_if(_service_instances.begin(), _service_instances.end(), [&lame, this](const Service_t& el) 
            { 
                if (el.name == lame)
                {
                    if (!el.library.config.empty())
                    {
                        std::string config_path = _root + "conf\\" + el.library.config;
                        if (_access(config_path.c_str(), 00) == 0)
                        {
                            if (remove(config_path.c_str()) != 0)
                            {
                                _logger->warn("delete service({}) configuration file({}) failed: {}", el.name.c_str(), el.library.config.c_str(), GetLastErrorString().c_str());
                            }
                        }
                    }
                    return true;
                }
                return false;
            }));
            fresh_services_json(_service_instances, _services_path);
        }

        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
    } while (false);

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", res, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_service_control(struct mg_connection *nc, struct http_message *hm)
{
    char action[128] = { 0 }, name[128] = { 0 };

    mg_get_http_var(&hm->body, "action", action, sizeof(action));
    mg_get_http_var(&hm->body, "name", name, sizeof(name));

    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value res(rapidjson::kStringType);

    do
    {
        bool found = false;
        for (const Service_t& servs : _service_instances)
        {
            if (servs.name == name)
            {
                found = true;
                break;
            }
        }

        std::stringstream ss;
        if (!found)
        {
            ss << "Service(" << name << ") does not exist";
            res.SetString(ss.str().c_str(), doc.GetAllocator());
            code.SetInt(Service_Module_Failed);
            break;
        }

        SC_HANDLE schSCManager = OpenSCManager(
            NULL,                    // local computer
            NULL,                    // ServicesActive database 
            SC_MANAGER_ALL_ACCESS);  // full access rights 
        if (NULL == schSCManager)
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(res, doc.GetAllocator());
            break;
        }

        SC_HANDLE schService = OpenService(
            schSCManager,              // SCM database 
            name,                   // name of service 
            SERVICE_ALL_ACCESS);        // desired access

        if (schService == NULL)
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(res, doc.GetAllocator());

            CloseServiceHandle(schSCManager);
            break;
        }

        if (strcmp(action, "Start") == 0)
        {
            if (!StartService(
                schService,
                0,
                NULL))
            {
                code.SetInt(Service_Module_Failed);
                GetLastErrorString(res, doc.GetAllocator());
            }
            else
            {
                code.SetInt(Service_Module_Success);
                res.SetString("success", doc.GetAllocator());
            }
        } 
        else
        {
            SERVICE_STATUS_PROCESS ssp;
            if (!ControlService(
                schService,
                SERVICE_CONTROL_STOP,
                (LPSERVICE_STATUS)&ssp))
            {
                code.SetInt(Service_Module_Failed);
                GetLastErrorString(res, doc.GetAllocator());
            }
            else
            {
                code.SetInt(Service_Module_Success);
                res.SetString("success", doc.GetAllocator());
            }
        }

        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
    } while (false);

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", res, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_service_upgrade(struct mg_connection *nc, struct http_message *hm)
{
    rapidjson::Document doc(rapidjson::kObjectType);
    rapidjson::Value code(rapidjson::kNumberType);
    rapidjson::Value res(rapidjson::kStringType);

    do
    {
        struct archive *ar = open_package(code, res, doc.GetAllocator());
        if (!ar)
        {
            break;
        }

        std::list<std::string> related_definitions = parse_dependent_service_definitions(ar);
        if (!stop_dependent_services(related_definitions, code, res, doc.GetAllocator()) ||
            !upgrade_dependent_services(ar, code, res, doc.GetAllocator()) ||
            !start_dependent_services(related_definitions, code, res, doc.GetAllocator()))
        {
            close_package(ar);
            break;
        }

        code.SetInt(Service_Module_Success);
        res.SetString("success", doc.GetAllocator());

        close_package(ar);
    } while (false);

    doc.AddMember("code", code, doc.GetAllocator());
    doc.AddMember("result", res, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    mg_send_http_chunk(nc, json, strlen(json));
    mg_send_http_chunk(nc, "", 0); /* Send empty chunk, the end of response */
}

void HttpService::handle_html(struct mg_connection *nc, struct http_message *hm)
{
    if (mg_vcmp(&hm->uri, "/") == 0)
    {
        mg_http_send_redirect(nc, 302, mg_mk_str("/index.html"), mg_mk_str(NULL));
    }
    else
    {
        mg_serve_http(nc, hm, _opts); /* Serve static content */
    }
}

bool HttpService::fresh_services_json(const Services_t& services, const std::string& json_file)
{
    rapidjson::Document doc(rapidjson::kObjectType);

    rapidjson::Value arr_json(rapidjson::kArrayType);

    rapidjson::Value str_json(rapidjson::kStringType);
    rapidjson::Value num_json(rapidjson::kNumberType);    
    for (const Service_t& serv : services)
    {
        rapidjson::Value serv_json(rapidjson::kObjectType);

        str_json.SetString(serv.id.c_str(), doc.GetAllocator());
        serv_json.AddMember("id", str_json, doc.GetAllocator());

        str_json.SetString(serv.name.c_str(), doc.GetAllocator());
        serv_json.AddMember("name", str_json, doc.GetAllocator());

        str_json.SetString(serv.description.c_str(), doc.GetAllocator());
        serv_json.AddMember("description", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.name.c_str(), doc.GetAllocator());
        serv_json.AddMember("library", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
        serv_json.AddMember("configuration", str_json, doc.GetAllocator());

        str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
        serv_json.AddMember("version", str_json, doc.GetAllocator());

        rapidjson::Value log_json(rapidjson::kObjectType);
        str_json.SetString(serv.log.name.c_str(), doc.GetAllocator());
        log_json.AddMember("name", str_json, doc.GetAllocator());

        str_json.SetString(serv.log.type.c_str(), doc.GetAllocator());
        log_json.AddMember("type", str_json, doc.GetAllocator());

        str_json.SetString(serv.log.level.c_str(), doc.GetAllocator());
        log_json.AddMember("level", str_json, doc.GetAllocator());

        num_json.SetInt(serv.log.keep);
        log_json.AddMember("keep", num_json, doc.GetAllocator());

        num_json.SetInt(serv.log.size);
        log_json.AddMember("size", num_json, doc.GetAllocator());

        serv_json.AddMember("log", log_json, doc.GetAllocator());

        arr_json.PushBack(serv_json, doc.GetAllocator());
    }
    
    if (!arr_json.Empty())
    {
        doc.AddMember("services", arr_json, doc.GetAllocator());
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    const char* json = buffer.GetString();

    std::ofstream ofs(json_file);
    if (ofs.is_open())
    {
        ofs.write(json, strlen(json));
        ofs.close();
        return true;
    }
    else
    {
        return false;
    }
}

bool HttpService::parse_option(Service_t& serv, const std::string& options, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    bool everything_is_fine = true;

    std::regex options_rgx("\\s+");
    std::sregex_token_iterator sregex_token_end;
    for (std::sregex_token_iterator iter(options.begin(), options.end(), options_rgx, -1); everything_is_fine && iter != sregex_token_end; ++iter)
    {
        const std::string& opt = *iter;

        std::regex option_rgx("=");
        std::sregex_token_iterator field_itr(opt.begin(), opt.end(), option_rgx, -1);

        std::string key = *field_itr;
        if (key.empty())
        {
            continue;
        }

        ++field_itr;
        if (field_itr == sregex_token_end)
        {
            continue;
        }
        std::string value = *field_itr;

        Service_Options_Type::const_iterator found = Service_Options.find(key);
        if (found != Service_Options.cend())
        {
            switch (found->second)
            {
            case Service_Option_Config:
            {
                serv.library.config = value;
                break;
            }
            case Service_Option_Log_Name:
            {
                serv.log.name = value;
                break;
            }
            case Service_Option_Log_Type:
            {
                serv.log.type = value;
                break;
            }
            case Service_Option_Log_Level:
            {
                serv.log.level = value;
                break;
            }
            case Service_Option_Log_Size:
            {
                serv.log.size = atoi(value.c_str());
                if (serv.log.size <= 0)
                {
                    everything_is_fine = false;
                    err.SetString("Service log size is invalid", allo);
                }
                break;
            }
            case Service_Option_Log_Keep:
            {
                serv.log.keep = atoi(value.c_str());
                if (serv.log.keep <= 0)
                {
                    everything_is_fine = false;
                    err.SetString("Service log keep(days) is invalid", allo);
                }
                break;
            }
            default:
            {

            }
            }
        }
    }

    return everything_is_fine;
}

std::string HttpService::make_option(const Service_t& serv, bool make_service_opt)
{
    std::stringstream ss;
    if (make_service_opt)
    {
        ss << " --service.lib=services\\" << serv.id << "\\" << serv.library.name;
        ss << " --service.config=conf\\" << serv.library.config;
    }
    else
    {
        ss << " --service.config=" << serv.library.config;
    }

    if (!serv.log.name.empty())
    {
        ss << " --log.name=" << serv.log.name;
    }

    if (!serv.log.type.empty())
    {
        ss << " --log.type=" << serv.log.type;
    }
    else
    {
        ss << " --log.type=daily";
    }

    if (!serv.log.level.empty())
    {
        ss << " --log.level=" << serv.log.level;
    }
    else
    {
        ss << " --log.level=info";
    }

    if (serv.log.keep > 0)
    {
        ss << " --log.keep=" << serv.log.keep;
    }
    else
    {
        ss << " --log.keep=7";
    }

    if (serv.log.size > 0)
    {
        ss << " --log.size=" << serv.log.size;
    }
    else
    {
        ss << " --log.size=80";
    }

    return ss.str();
}

bool HttpService::create_service_configuration(const Service_t& serv, const std::vector<char>& configuration, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    if (!serv.library.config.empty())
    {
        std::ofstream ofs(_root + "conf\\" + serv.library.config);
        if (ofs.is_open())
        {
            ofs.write(configuration.data(), configuration.size());
            ofs.close();
        }
        else
        {
            std::stringstream ss;
            ss << "Write service configuration(" << serv.library.config << ") failed";
            err.SetString(ss.str().c_str(), allo);
            return false;
        }
    }
    return true;
}

bool HttpService::begin_upload_package()
{
    if (_package.is_open())
    {
        _package.close();
    }

    std::string package_path(_root + Service_Tmp_Dir);
    if (_access(package_path.c_str(), 00) != 0 && _mkdir(package_path.c_str()) != 0)
    {
        _logger->error("upload package failed: upload directory({}) does not exist", Service_Tmp_Dir);
        return false;
    }

    _package.open(package_path + Service_Upgrade_File, std::ios::binary);
    if (!_package.is_open())
    {
        _logger->error("upload package failed: open upgrade package({}{}) failed", Service_Tmp_Dir, Service_Upgrade_File);
        return false;
    }
    return true;
}

bool HttpService::write_upload_package(const char* data, size_t len)
{
    if (_package.is_open())
    {
        _package.write(data, len);
        return true;
    }
    return false;
}

long HttpService::end_upload_package()
{
    long len = 0L;
    if (_package.is_open())
    {
        len = (long)_package.tellp();
        _package.close();
    }
    return len;
}

struct archive* HttpService::open_package(rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    struct archive *ar = nullptr;
    do 
    {
        std::string package_path(_root + Service_Tmp_Dir + Service_Upgrade_File);
        if (_access(package_path.c_str(), 00) != 0)
        {
            code.SetInt(Service_Module_Failed);
            err.SetString("Upgrade package not found", allo);
            break;
        }

        ar = archive_read_new();
        if (!ar)
        {
            code.SetInt(Service_Module_Failed);
            err.SetString("Unsupported package type", allo);
            break;
        }

        archive_read_support_filter_all(ar);
        archive_read_support_format_all(ar);

        int r = archive_read_open_filename(ar, package_path.c_str(), 10240); // Note 1
        if (r != ARCHIVE_OK)
        {
            code.SetInt(Service_Module_Failed);
            err.SetString("Unsupported package format", allo);
            break;
        }
    } while (false);

    return ar;
}

std::list<std::string> HttpService::parse_dependent_service_definitions(struct archive* ar)
{
    std::list<std::string> related_definitions;
    struct archive_entry *entry = nullptr;
    while (archive_read_next_header(ar, &entry) == ARCHIVE_OK) 
    {
        related_definitions.emplace_back(archive_entry_pathname(entry));
        archive_read_data_skip(ar);  // Note 2
    }
    return related_definitions;
}

bool HttpService::stop_dependent_services(const std::list<std::string>& related_definitions, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    do
    {
        code.SetInt(Service_Module_Success);

        SC_HANDLE schSCManager = OpenSCManager(
            NULL,                    // local computer
            NULL,                    // ServicesActive database
            SC_MANAGER_ALL_ACCESS);  // full access rights
        if (NULL == schSCManager)
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(err, allo);
            break;
        }

        SERVICE_STATUS_PROCESS ssStatus;
        DWORD dwBytesNeeded;
        for (const Service_t& serv : _service_instances)
        {
            std::list<std::string>::const_iterator found = std::find_if(related_definitions.begin(), related_definitions.end(), [&serv](const std::string& id) { return serv.id == id; });
            if (related_definitions.end() == found)
            {
                continue;
            }

            SC_HANDLE schService = OpenService(
                schSCManager,              // SCM database
                serv.name.c_str(),         // name of service
                SERVICE_ALL_ACCESS);       // desired access

            if (schService == NULL)
            {
                code.SetInt(Service_Module_Failed);
                GetLastErrorString(err, allo);

                break;
            }
            
            if (!QueryServiceStatusEx(
                schService,                     // handle to service 
                SC_STATUS_PROCESS_INFO,         // information level
                (LPBYTE)&ssStatus,             // address of structure
                sizeof(SERVICE_STATUS_PROCESS), // size of structure
                &dwBytesNeeded))              // size needed if buffer is too small
            {
                code.SetInt(Service_Module_Failed);
                GetLastErrorString(err, allo);

                CloseServiceHandle(schService);
                break;
            }

            if (ssStatus.dwCurrentState != SERVICE_STOPPED && ssStatus.dwCurrentState != SERVICE_STOP_PENDING)
            {
                SERVICE_STATUS_PROCESS ssp;
                if (!ControlService(
                    schService,
                    SERVICE_CONTROL_STOP,
                    (LPSERVICE_STATUS)&ssp))
                {
                    code.SetInt(Service_Module_Failed);
                    GetLastErrorString(err, allo);
                }
            }
            
            CloseServiceHandle(schService);
        }

        CloseServiceHandle(schSCManager);

    } while (false);

    return code.GetInt() == Service_Module_Success;
}

bool HttpService::upgrade_dependent_services(struct archive* ar, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    return true;
}

bool HttpService::start_dependent_services(const std::list<std::string>& related_definitions, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    do
    {
        code.SetInt(Service_Module_Success);

        SC_HANDLE schSCManager = OpenSCManager(
            NULL,                    // local computer
            NULL,                    // ServicesActive database
            SC_MANAGER_ALL_ACCESS);  // full access rights
        if (NULL == schSCManager)
        {
            code.SetInt(Service_Module_Failed);
            GetLastErrorString(err, allo);
            break;
        }

        SERVICE_STATUS_PROCESS ssStatus;
        DWORD dwBytesNeeded;
        for (const Service_t& serv : _service_instances)
        {
            std::list<std::string>::const_iterator found = std::find_if(related_definitions.begin(), related_definitions.end(), [&serv](const std::string& id) { return serv.id == id; });
            if (related_definitions.end() == found)
            {
                continue;
            }

            SC_HANDLE schService = OpenService(
                schSCManager,              // SCM database
                serv.name.c_str(),         // name of service
                SERVICE_ALL_ACCESS);       // desired access

            if (schService == NULL)
            {
                code.SetInt(Service_Module_Failed);
                GetLastErrorString(err, allo);

                break;
            }

            if (!QueryServiceStatusEx(
                schService,                     // handle to service 
                SC_STATUS_PROCESS_INFO,         // information level
                (LPBYTE)&ssStatus,             // address of structure
                sizeof(SERVICE_STATUS_PROCESS), // size of structure
                &dwBytesNeeded))              // size needed if buffer is too small
            {
                code.SetInt(Service_Module_Failed);
                GetLastErrorString(err, allo);

                CloseServiceHandle(schService);
                break;
            }

            if (ssStatus.dwCurrentState != SERVICE_START && ssStatus.dwCurrentState != SERVICE_START_PENDING)
            {
                if (!StartService(
                    schService,
                    0,
                    NULL))
                {
                    code.SetInt(Service_Module_Failed);
                    GetLastErrorString(err, allo);
                }
            }

            CloseServiceHandle(schService);
        }

        CloseServiceHandle(schSCManager);

    } while (false);

    return code.GetInt() == Service_Module_Success;
}

void HttpService::close_package(struct archive* ar)
{
    if (ar)
    {
        int r = archive_read_free(ar);  // Note 3
        if (ARCHIVE_OK != r)
        {
            _logger->warn("free archive failed: {}", archive_error_string(ar));
        }
    }
}

