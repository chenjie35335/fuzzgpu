#pragma once

#include <string>

namespace difftest {

namespace TermColor {
constexpr std::string RESET = "\033[0m";
constexpr std::string BLACK = "\033[30m";              /* Black */
constexpr std::string RED = "\033[31m";                /* Red */
constexpr std::string GREEN = "\033[32m";              /* Green */
constexpr std::string YELLOW = "\033[33m";             /* Yellow */
constexpr std::string BLUE = "\033[34m";               /* Blue */
constexpr std::string MAGENTA = "\033[35m";            /* Magenta */
constexpr std::string CYAN = "\033[36m";               /* Cyan */
constexpr std::string WHITE = "\033[37m";              /* White */
constexpr std::string BOLDBLACK = "\033[1m\033[30m";   /* Bold Black */
constexpr std::string BOLDRED = "\033[1m\033[31m";     /* Bold Red */
constexpr std::string BOLDGREEN = "\033[1m\033[32m";   /* Bold Green */
constexpr std::string BOLDYELLOW = "\033[1m\033[33m";  /* Bold Yellow */
constexpr std::string BOLDBLUE = "\033[1m\033[34m";    /* Bold Blue */
constexpr std::string BOLDMAGENTA = "\033[1m\033[35m"; /* Bold Magenta */
constexpr std::string BOLDCYAN = "\033[1m\033[36m";    /* Bold Cyan */
constexpr std::string BOLDWHITE = "\033[1m\033[37m";   /* Bold White */
}; // namespace TermColor

} // namespace difftest