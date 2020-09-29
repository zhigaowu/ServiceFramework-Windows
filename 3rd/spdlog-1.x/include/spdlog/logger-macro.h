#ifndef _LOGGER_MACRO_HEADER_H_
#define _LOGGER_MACRO_HEADER_H_

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/daily_file_sink.h"

#define DECLARE_SPDLOG(name) \
class name \
{ \
public: \
    static void initialize_daily(const std::string& name, int hour, int minite, int maxfiles, spdlog::level::level_enum log_level) \
    { \
        auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(name, hour, minite, false, maxfiles); \
        daily_sink->set_level(log_level); \
        daily_sink->set_pattern("[%H:%M:%S.%e %t %^%L%$] %v"); \
 \
        spdlog::sinks_init_list sink_list{ daily_sink }; \
        _logger = std::make_shared<spdlog::logger>("multi_sink", sink_list); \
 \
        _logger->set_level(log_level); \
        _logger->flush_on(log_level); \
    } \
 \
    static void initialize_rotate(const std::string& name, std::size_t mb, int maxfiles, spdlog::level::level_enum log_level) \
    { \
        auto rotate_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(name, mb << 20, maxfiles); \
        rotate_sink->set_level(log_level); \
        rotate_sink->set_pattern("[%H:%M:%S.%e %t %^%L%$] %v"); \
 \
        spdlog::sinks_init_list sink_list{ rotate_sink }; \
        _logger = std::make_shared<spdlog::logger>("multi_sink", sink_list); \
 \
        _logger->set_level(log_level); \
        _logger->flush_on(log_level); \
    } \
 \
    static std::shared_ptr<spdlog::logger> logger() \
    { \
        return _logger; \
    } \
 \
    static int change_level(int level) \
    { \
        const static char* level_name[8] = {"trace", "debug", "info", "warn", "error", "fatal", "off"}; \
        if (level >= spdlog::level::trace && level <= spdlog::level::off) \
        { \
            spdlog::level::level_enum to = (spdlog::level::level_enum)level; \
            spdlog::level::level_enum from = _logger->level(); \
            _logger->info("change log level from {} to {} success", level_name[from], level_name[to]); \
            _logger->set_level(to); \
            return 0; \
        }  \
        else \
        { \
            _logger->critical("change log level failed: invalid level({})", level); \
            return -400; \
        } \
    } \
 \
    static void deinitialize() \
    { \
        _logger = nullptr; \
    } \
 \
private: \
    static std::shared_ptr<spdlog::logger> _logger; \
 \
private: \
    name(); \
    name(const name&); \
    name& operator=(const name&); \
}

#define IMPLEMENT_SPDLOG(name) std::shared_ptr<spdlog::logger> name::_logger

/********************************************************************************************************************************************/
/* logging::logger()->log(spdlog::source_loc{ __FILE__, __LINE__, __FUNCTION__ }, spdlog::level::info, "BusinessServer startup success");   */
/********************************************************************************************************************************************/

#endif

