#include "game/name_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using ghostbusters::game::NameEntry;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Function>
void expectOutOfRange(Function&& function, const std::string& message)
{
    try {
        function();
        check(false, message + " (no exception)");
    } catch (const std::out_of_range&) {
    } catch (const std::exception& error) {
        check(false, message + " (wrong exception: " + error.what() + ")");
    }
}

void testConfiguredCapacity()
{
    NameEntry account(12);
    for (std::uint8_t value = 0x41; value < 0x41 + 12; ++value) {
        check(account.key(value) == NameEntry::Result::inserted,
              "configured account capacity accepts twelve bytes");
    }
    const auto beforeFull = account.bytes();
    check(account.size() == 12 && account.key(0x5A) == NameEntry::Result::ignored,
          "configured account capacity ignores the thirteenth byte");
    check(account.bytes() == beforeFull, "configured capacity does not alter its full buffer");

    NameEntry empty(0);
    check(empty.key(0x41) == NameEntry::Result::ignored && empty.size() == 0,
          "zero configured capacity accepts no printable bytes");
    expectOutOfRange([&] { NameEntry invalid(20); },
                     "configured capacity rejects values above nineteen");
}

void testInitialAndTranslatedKeys()
{
    NameEntry entry;
    check(entry.size() == 0 && !entry.submitted(), "name entry starts empty and open");
    for (const auto value : entry.bytes()) {
        check(value == 0, "name buffer starts zero-filled");
    }

    check(entry.key(0x00) == NameEntry::Result::ignored, "null is ignored");
    check(entry.key(0x1F) == NameEntry::Result::ignored, "non-printable control is ignored");
    check(entry.key(0x80) == NameEntry::Result::ignored, "negative translated key is ignored");
    check(entry.key(0x41) == NameEntry::Result::inserted, "printable key is inserted");
    check(entry.key(0x20) == NameEntry::Result::inserted, "space is inserted");
    check(entry.key(0x2F) == NameEntry::Result::inserted,
          "slash is accepted after upstream function-key filtering");
    check(entry.size() == 3 && entry.bytes()[0] == 0x41 && entry.bytes()[1] == 0x20 &&
              entry.bytes()[2] == 0x2F,
          "translated bytes are retained verbatim");
}

void testCapacityAndDelete()
{
    NameEntry entry;
    check(entry.key(0x7F) == NameEntry::Result::ignored, "delete is ignored while empty");
    for (std::uint8_t value = 0x41; value < 0x41 + 18; ++value) {
        check(entry.key(value) == NameEntry::Result::inserted,
              "each of the 18 name slots accepts one byte");
    }
    check(entry.size() == 18, "name capacity is 18 bytes");
    const auto beforeFull = entry.bytes();
    check(entry.key(0x5A) == NameEntry::Result::ignored, "the 19th byte is ignored");
    check(entry.bytes() == beforeFull, "capacity overflow does not alter the buffer");

    check(entry.key(0x7F) == NameEntry::Result::erased && entry.size() == 17,
          "delete decrements the length");
    check(entry.bytes()[17] == 0x20, "delete leaves a space in the removed tail slot");
    check(entry.key(0x59) == NameEntry::Result::inserted && entry.size() == 18 &&
              entry.bytes()[17] == 0x59,
          "a reinsert overwrites the deleted space");
}

void testSubmitAndTail()
{
    NameEntry entry;
    check(entry.key(0x41) == NameEntry::Result::inserted, "first byte is inserted");
    check(entry.key(0x42) == NameEntry::Result::inserted, "second byte is inserted");
    check(entry.key(0x43) == NameEntry::Result::inserted, "third byte is inserted");
    check(entry.key(0x7F) == NameEntry::Result::erased && entry.size() == 2,
          "delete leaves the shortened name open");
    check(entry.bytes()[2] == 0x20, "deleted final byte remains a space before Return");
    check(entry.key(0x0D) == NameEntry::Result::submitted && entry.submitted(),
          "Return submits the name");
    check(entry.size() == 2 && entry.bytes()[2] == 0,
          "Return writes the terminator at the current length");
    check(entry.bytes()[3] == 0, "untouched zero tail remains zero");
    const auto before = entry.bytes();
    check(entry.key(0x44) == NameEntry::Result::ignored && entry.bytes() == before,
          "keys after submission are ignored");

    NameEntry empty;
    check(empty.key(0x0D) == NameEntry::Result::submitted && empty.submitted() &&
              empty.size() == 0 && empty.bytes()[0] == 0,
          "an empty name can be submitted with a terminator");
}

} // namespace

int main()
{
    testInitialAndTranslatedKeys();
    testCapacityAndDelete();
    testSubmitAndTail();
    testConfiguredCapacity();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "name entry tests passed\n";
    return 0;
}
