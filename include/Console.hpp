// Console.hpp - small, safe console input/output helpers.
#pragma once

#include <string>

namespace casino {
namespace console {

// Reads one line. Throws InputClosedException when stdin reaches end of file.
std::string readLine(const std::string& prompt);

// Parses a whole-string integer. Throws InvalidInputException on bad text.
long parseLong(const std::string& text);

// Keeps asking until the user types an integer in [lo, hi].
long readLong(const std::string& prompt, long lo, long hi);
// Keeps asking until the first letter typed is one of `allowed` (lower-case).
char readChoice(const std::string& prompt, const std::string& allowed);
bool readYesNo(const std::string& prompt);

void pause(const std::string& message = "Press Enter to continue...");
void clearScreen();
void banner(const std::string& title);
void rule();

// 1234567 -> "1,234,567"
std::string money(long chips);

}  // namespace console
}  // namespace casino
