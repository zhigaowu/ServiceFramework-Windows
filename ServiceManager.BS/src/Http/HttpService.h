
#ifndef _HTTPSERVICE_HEADER_H_
#define _HTTPSERVICE_HEADER_H_

extern "C" {
#include "mongoose/mongoose.h"
}

#include "spdlog/logging.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include <unordered_set>
#include <unordered_map>

#include <list>

class HttpService
{
private:
    struct library_t 
    {
        std::string path;
        std::string config;
        std::string ver;
    };

    struct log_t
    {
        std::string name;
        std::string type;
        std::string level;

        int keep = 0;
        int size = 0;
    };

    struct Service_t 
    {
        std::string id;
        std::string name;
        std::string description;

        library_t library;

        log_t log;

        int status_code = 0;
        std::string status_txt;
    };

    typedef std::list<Service_t> Services_t;

public:
    HttpService();
    ~HttpService();

    int Create(const std::string& address, int port, const std::string& directory, const std::string& service_definition_config, const std::string& service_active_config, const std::string& root, std::shared_ptr<spdlog::logger>& log);
    void Serve();
    void Destroy();

    inline struct mg_serve_http_opts& Opts()
    {
        return _opts;
    }

    static std::shared_ptr<spdlog::logger>& logger();

private:
    void handle_service_list(struct mg_connection *nc, struct http_message *hm);
    void handle_definition_list(struct mg_connection *nc, struct http_message *hm);
    void handle_service_detail_get(struct mg_connection *nc, struct http_message *hm);
    void handle_service_detail_set(struct mg_connection *nc, struct http_message *hm);
    void handle_service_create(struct mg_connection *nc, struct http_message *hm);
    void handle_service_delete(struct mg_connection *nc, struct http_message *hm);
    void handle_service_control(struct mg_connection *nc, struct http_message *hm);
    void handle_service_upgrade(struct mg_connection *nc, struct http_message *hm);

    void handle_html(struct mg_connection *nc, struct http_message *hm);

private:
    bool fresh_services_json(const Services_t& services, const std::string& json_file);
    bool parse_option(Service_t& serv, const std::string& option, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);
    bool fresh_service_configuration(const Service_t& serv, const std::vector<char>& configuration, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);

private:
    friend void ev_handler(struct mg_connection *nc, int ev, void *ev_data);

private:
    struct mg_mgr _mgr;
    struct mg_serve_http_opts _opts;

private:
    Services_t _service_definitions;

    std::string _services_path;
    Services_t _service_instances;

private:
    std::string _root;

private:
    static std::shared_ptr<spdlog::logger> _logger;

private:
    HttpService(const HttpService&);
    HttpService& operator=(const HttpService&);
};


#endif
