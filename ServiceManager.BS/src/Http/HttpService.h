
#ifndef _HTTPSERVICE_HEADER_H_
#define _HTTPSERVICE_HEADER_H_

extern "C" {
#include "mongoose/mongoose.h"
}

#include "spdlog/logging.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"

#include "archive.h"
#include "archive_entry.h"

#include <unordered_set>
#include <unordered_map>
#include <list>

#include <fstream>

class HttpService
{
private:
    struct library_t 
    {
        std::string name;
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
    int load_service_definitions();
    int load_service_instances();

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
    std::string make_option(const Service_t& serv, bool make_service_opt);
    bool create_service_configuration(const Service_t& serv, const std::vector<char>& configuration, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);

private:
    bool begin_upload_package();
    bool write_upload_package(const char* data, size_t len);
    long end_upload_package();

private:
    bool parse_dependent_service_definitions(std::list<std::string>& related_definitions, std::string& package_path_name, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);
    bool extract_package(rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);

    bool stop_dependent_services(const std::list<std::string>& related_definitions, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);

    bool copy_package_file(const std::string& src, const std::string& dst, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);
    bool copy_package_directory(const std::string& src_path, const std::string& dst_path, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);
    bool upgrade_service_with_package(const std::string& package_path_name, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);

    bool start_dependent_services(const std::list<std::string>& related_definitions, rapidjson::Value& code, rapidjson::Value& err, rapidjson::Document::AllocatorType& allo);

    void delete_package_file(const std::string& path);
    void delete_package_directory(const std::string& path);
    void delete_package(const std::string& package_path_name);

private:
    friend void ev_handler(struct mg_connection *nc, int ev, void *ev_data);
    friend void ev_upload(struct mg_connection *nc, int ev, void *ev_data);

private:
    struct mg_mgr _mgr;
    struct mg_serve_http_opts _opts;

private:
    std::string _definitions_path;
    Services_t _service_definitions;

    std::string _services_path;
    Services_t _service_instances;

private:
    std::string _root;

private:
    std::ofstream _package;

private:
    static std::shared_ptr<spdlog::logger> _logger;

private:
    HttpService(const HttpService&);
    HttpService& operator=(const HttpService&);
};


#endif
