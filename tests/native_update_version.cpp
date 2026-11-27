#include "../native/windows/Nanhara.Board.InkDesktopHost/update_version.h"

#include <cassert>
#include <string>

int main()
{
    using board_update::is_newer_preview;
    const auto current = board_update::current_preview;
    assert(is_newer_preview("0.2.0-alpha." + std::to_string(current + 1) + "\n", current));
    assert(is_newer_preview("0.2.0-alpha.100\r\n", current));
    assert(!is_newer_preview("0.2.0-alpha.9\n", current));
    assert(!is_newer_preview("0.2.0-alpha." + std::to_string(current), current));
    assert(!is_newer_preview("0.2.0-alpha.011", current));
    assert(!is_newer_preview("0.2.0-alpha.99999999999999999999999999", current));
    assert(!is_newer_preview("0.2.0-alpha.13\nhttps://example.test/file", current));
    assert(!is_newer_preview("0.2.0", current));
    assert(!is_newer_preview("0.2.0-alpha.", current));
    assert(!is_newer_preview("<html>0.2.0-alpha.11</html>", current));
}
