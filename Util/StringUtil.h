#ifndef _STRING_UTIL_HEADER_H_
#define _STRING_UTIL_HEADER_H_

#include <string>
#include <algorithm>

namespace util {
    namespace string {

        inline std::string ltrim(const std::string &str) {
            std::string::const_iterator p = std::find_if(str.cbegin(), str.cend(), std::not1(std::ptr_fun<int, int>(std::isspace)));
            return std::string(p, str.end());
        }

        inline std::string rtrim(const std::string &str) {
            std::string result = str;
            std::string::reverse_iterator p = std::find_if(result.rbegin(), result.rend(), std::not1(std::ptr_fun<int, int>(std::isspace)));
            result.erase(p.base(), result.end());
            return result;
        }

        inline std::string trim(const std::string &str) {
            return ltrim(rtrim(str));
        }
    };
};

#endif
