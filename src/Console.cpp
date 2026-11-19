#include "Console.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>

#include "Exceptions.hpp"

#if defined(_WIN32)
#include <io.h>
#define CASINO_IS_TTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define CASINO_IS_TTY(fd) isatty(fd)
#endif

namespace casino {
namespace console {

namespace {
std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}
}  // namespace

std::string readLine(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) throw InputClosedException();
    return line;
}

long parseLong(const std::string& text) {
    const std::string t = trim(text);
    if (t.empty()) throw InvalidInputException("Please type a number.");
    std::size_t used = 0;
    long value = 0;
    try {
        value = std::stol(t, &used);
    } catch (const std::exception&) {
        throw InvalidInputException("'" + t + "' is not a valid number.");
    }
    if (used != t.size()) throw InvalidInputException("'" + t + "' is not a valid number.");
    return value;
}

long readLong(const std::string& prompt, long lo, long hi) {
    while (true) {
        const std::string line = readLine(prompt);
        try {
            const long value = parseLong(line);
            if (value < lo || value > hi) {
                throw InvalidInputException("Enter a number between " + std::to_string(lo) + " and " +
                                            std::to_string(hi) + ".");
            }
            return value;
        } catch (const InvalidInputException& e) {
            std::cout << "  ! " << e.what() << '\n';
        }
    }
}

char readChoice(const std::string& prompt, const std::string& allowed) {
    while (true) {
        const std::string line = trim(readLine(prompt));
        if (!line.empty()) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
            if (allowed.find(c) != std::string::npos) return c;
        }
        std::cout << "  ! Please choose one of: " << allowed << '\n';
    }
}

bool readYesNo(const std::string& prompt) { return readChoice(prompt, "yn") == 'y'; }

void pause(const std::string& message) { readLine(message); }

void clearScreen() {
    if (!CASINO_IS_TTY(0)) return;  // piped input (tests): nothing to clear
#if defined(_WIN32)
    std::system("cls");
#else
    std::system("clear");
#endif
}

void rule() { std::cout << std::string(64, '-') << '\n'; }

void banner(const std::string& title) {
    std::cout << '\n' << std::string(64, '=') << '\n';
    const std::size_t pad = title.size() < 62 ? (62 - title.size()) / 2 : 0;
    std::cout << ' ' << std::string(pad, ' ') << title << '\n';
    std::cout << std::string(64, '=') << '\n';
}

std::string money(long chips) {
    const bool negative = chips < 0;
    std::string digits = std::to_string(negative ? -chips : chips);
    std::string out;
    int count = 0;
    for (std::size_t i = digits.size(); i-- > 0;) {
        out.insert(out.begin(), digits[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return negative ? "-" + out : out;
}

}  // namespace console
}  // namespace casino
