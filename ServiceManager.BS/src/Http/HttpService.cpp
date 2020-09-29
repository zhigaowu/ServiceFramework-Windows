
#include "HttpService.h"

#include "ServiceInterface.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>

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
    { "--service.config", Service_Option_Lib },
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
    , _service_definitions()
    , _services_path(), _service_instances()
    , _root()
{
    _opts.document_root = nullptr;
    _opts.enable_directory_listing = "no";
}

HttpService::~HttpService()
{
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
    do
    {
        // check service definition file
        std::ifstream ifs(service_definition_config);
        ifs.seekg(0, std::ios::end);
        size_t length = (size_t)ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        std::vector<char> content(length);
        ifs.read(content.data(), length);

        rapidjson::Document json;
        json.Parse(content.data());
        if (!json.IsArray())
        {
            _logger->error("create http service failed: invalid service definition file({})", service_definition_config.c_str());
            return Service_Module_Failed;
        }

        for (rapidjson::Value& serv_json : json.GetArray())
        {
            Service_t serv;
            if (!serv_json.HasMember("id") || !serv_json["id"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): id field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.id = serv_json["id"].GetString();

            if (!serv_json.HasMember("name") || !serv_json["name"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): name field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.name = serv_json["name"].GetString();

            if (serv_json.HasMember("description"))
            {
                if (serv_json["description"].IsString())
                {
                    serv.description = serv_json["description"].GetString();
                } 
                else
                {
                    _logger->error("create http service failed: invalid service definition file({}): description field error", service_definition_config.c_str());
                    return Service_Module_Failed;
                }
            }

            if (!serv_json.HasMember("library") || !serv_json["library"].IsObject())
            {
                _logger->error("create http service failed: invalid service definition file({}): library field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }

            rapidjson::Value& lib_json = serv_json["library"];
            if (!lib_json.HasMember("path") || !lib_json["path"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): library.path field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.library.path = lib_json["path"].GetString();

            if (!lib_json.HasMember("configuration") || !lib_json["configuration"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): library.configuration field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.library.config = lib_json["configuration"].GetString();

            if (!lib_json.HasMember("version") || !lib_json["version"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): library.version field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.library.ver = lib_json["version"].GetString();

            rapidjson::Value& log_json = serv_json["log"];
            if (log_json.HasMember("name"))
            {
                if (log_json["name"].IsString())
                {
                    serv.log.name = log_json["name"].GetString();
                }
                else
                {
                    _logger->error("create http service failed: invalid service definition file({}): log.name field error", service_definition_config.c_str());
                    return Service_Module_Failed;
                }
            }

            if (!log_json.HasMember("type") || !log_json["type"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): log.type field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.log.type = log_json["type"].GetString();

            if (!log_json.HasMember("level") || !log_json["level"].IsString())
            {
                _logger->error("create http service failed: invalid service definition file({}): log.level field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.log.level = log_json["level"].GetString();

            if (!log_json.HasMember("keep") || !log_json["keep"].IsNumber())
            {
                _logger->error("create http service failed: invalid service definition file({}): log.keep field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.log.keep = log_json["keep"].GetInt();

            if (!log_json.HasMember("size") || !log_json["size"].IsNumber())
            {
                _logger->error("create http service failed: invalid service definition file({}): log.size field error", service_definition_config.c_str());
                return Service_Module_Failed;
            }
            serv.log.size = log_json["size"].GetInt();

            _service_definitions.emplace_back(serv);
        }
    } while (false);

    do
    {
        // check services file
        _services_path = service_active_config;
        // check service definition file
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

        rapidjson::Document services_doc;
        services_doc.Parse(content.data());
        rapidjson::Type jt = services_doc.GetType();
        if (rapidjson::kObjectType != jt)
        {
            _logger->error("create http service failed: invalid services file({})", _services_path.c_str());
            return Service_Module_Failed;
        }

        if (services_doc.Empty())
        {
            break;
        }

        if (!services_doc.HasMember("services"))
        {
            _logger->error("create http service failed: invalid services file({})", _services_path.c_str());
            return Service_Module_Failed;
        }

        rapidjson::Value json = services_doc["services"].GetArray();
        jt = json.GetType();
        if (rapidjson::kArrayType != jt)
        {
            _logger->error("create http service failed: invalid services file({})", _services_path.c_str());
            return Service_Module_Failed;
        }

        for (rapidjson::Value& serv_json : json.GetArray())
        {
            if (serv_json.IsObject() && serv_json.Empty())
            {
                continue;
            }
            Service_t serv;
            if (!serv_json.HasMember("id") || !serv_json["id"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): id field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.id = serv_json["id"].GetString();

            if (!serv_json.HasMember("name") || !serv_json["name"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): name field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.name = serv_json["name"].GetString();

            if (serv_json.HasMember("description"))
            {
                if (serv_json["description"].IsString())
                {
                    serv.description = serv_json["description"].GetString();
                }
                else
                {
                    _logger->error("create http service failed: invalid services file({}): description field error", _services_path.c_str());
                    return Service_Module_Failed;
                }
            }

            if (!serv_json.HasMember("library") || !serv_json["library"].IsObject())
            {
                _logger->error("create http service failed: invalid services file({}): library field error", _services_path.c_str());
                return Service_Module_Failed;
            }

            rapidjson::Value& lib_json = serv_json["library"];
            if (!lib_json.HasMember("path") || !lib_json["path"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): library.path field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.library.path = lib_json["path"].GetString();

            if (!lib_json.HasMember("configuration") || !lib_json["configuration"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): library.configuration field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.library.config = lib_json["configuration"].GetString();

            if (!lib_json.HasMember("version") || !lib_json["version"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): library.version field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.library.ver = lib_json["version"].GetString();

            rapidjson::Value& log_json = serv_json["log"];
            if (log_json.HasMember("name"))
            {
                if (log_json["name"].IsString())
                {
                    serv.log.name = log_json["name"].GetString();
                }
                else
                {
                    _logger->error("create http service failed: invalid services file({}): log.name field error", service_definition_config.c_str());
                    return Service_Module_Failed;
                }
            }

            if (!log_json.HasMember("type") || !log_json["type"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): log.type field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.log.type = log_json["type"].GetString();

            if (!log_json.HasMember("level") || !log_json["level"].IsString())
            {
                _logger->error("create http service failed: invalid services file({}): log.level field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.log.level = log_json["level"].GetString();

            if (!log_json.HasMember("keep") || !log_json["keep"].IsNumber())
            {
                _logger->error("create http service failed: invalid services file({}): log.keep field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.log.keep = log_json["keep"].GetInt();

            if (!log_json.HasMember("size") || !log_json["size"].IsNumber())
            {
                _logger->error("create http service failed: invalid service definition file({}): log.size field error", _services_path.c_str());
                return Service_Module_Failed;
            }
            serv.log.size = log_json["size"].GetInt();

            serv.status_code = 0;
            serv.status_txt = Service_Status_Name[0];

            _service_instances.emplace_back(serv);
        }
    } while (false);

    struct mg_bind_opts bind_opts;
    memset(&bind_opts, 0, sizeof(bind_opts));

    const char* err_str = nullptr;
    bind_opts.error_string = &err_str;

    mg_mgr_init(&_mgr, nullptr);

    char endpoint[256] = { 0 };
    snprintf(endpoint, sizeof(endpoint), "%s:%d", address.c_str(), port);

    _opts.document_root = directory.c_str();

    // configure http service
    struct mg_connection *nc = mg_bind_opt(&_mgr, endpoint, ev_handler, bind_opts);
    if (nc == NULL)
    {
        _logger->error("create http service({}) failed: {}", endpoint, *bind_opts.error_string);
        return Service_Module_Failed;
    }
    nc->user_data = this;

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

        str_json.SetString(serv.library.path.c_str(), doc.GetAllocator());
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

        str_json.SetString(serv.library.path.c_str(), doc.GetAllocator());
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
                std::stringstream ss;

                str_json.SetString(serv.library.path.c_str(), doc.GetAllocator());
                lib_json.AddMember("path", str_json, doc.GetAllocator());
                if (!serv.library.path.empty())
                {
                    ss << " --service.lib=" << serv.library.path;
                }

                str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
                lib_json.AddMember("configuration", str_json, doc.GetAllocator());
                if (!serv.library.config.empty())
                {
                    ss << " --service.config=" << serv.library.config;
                }

                str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
                lib_json.AddMember("version", str_json, doc.GetAllocator());

                serv_json.AddMember("library", lib_json, doc.GetAllocator());

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

                str_json.SetString(ss.str().c_str(), doc.GetAllocator());
                serv_json.AddMember("option", str_json, doc.GetAllocator());

                std::string real_path = serv.library.config;
                if (_access(real_path.c_str(), 00) != 0)
                {
                    real_path = _root + real_path;
                }

                std::ifstream ifs(real_path);
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
                std::stringstream ss;

                str_json.SetString(serv.library.path.c_str(), doc.GetAllocator());
                lib_json.AddMember("path", str_json, doc.GetAllocator());
                if (!serv.library.path.empty())
                {
                    ss << " --service.lib=" << serv.library.path;
                }

                str_json.SetString(serv.library.config.c_str(), doc.GetAllocator());
                lib_json.AddMember("configuration", str_json, doc.GetAllocator());
                if (!serv.library.config.empty())
                {
                    ss << " --service.config=" << serv.library.config;
                }

                str_json.SetString(serv.library.ver.c_str(), doc.GetAllocator());
                lib_json.AddMember("version", str_json, doc.GetAllocator());

                serv_json.AddMember("library", lib_json, doc.GetAllocator());

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

                str_json.SetString(ss.str().c_str(), doc.GetAllocator());
                serv_json.AddMember("option", str_json, doc.GetAllocator());

                std::string real_path = serv.library.config;
                if (_access(real_path.c_str(), 00) != 0)
                {
                    real_path = _root + real_path;
                }

                std::ifstream ifs(real_path);
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

        if (!fresh_service_configuration(serv, configuration, res, doc.GetAllocator()))
        {
            break;
        }

        if (!parse_option(serv, option, res, doc.GetAllocator()))
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
        snprintf(binary.data(), binary.size(), "%sServiceLoader.exe %s", _root.c_str(), option);
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

        if (!fresh_service_configuration(serv, configuration, res, doc.GetAllocator()))
        {
            break;
        }
        
        if (!parse_option(serv, option, res, doc.GetAllocator()))
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
        snprintf(binary.data(), binary.size(), "%sServiceLoader.exe %s", _root.c_str(), option);
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
            _service_instances.erase(std::remove_if(_service_instances.begin(), _service_instances.end(), [&lame](const Service_t& el) { return el.name == lame; }));
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

        rapidjson::Value lib_json(rapidjson::kObjectType);

        str_json.SetString(serv.library.path.c_str(), doc.GetAllocator());
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

bool HttpService::fresh_service_configuration(const Service_t& serv, const std::vector<char>& configuration, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo)
{
    if (!serv.library.config.empty())
    {
        std::ofstream ofs(_root + serv.library.config);
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

