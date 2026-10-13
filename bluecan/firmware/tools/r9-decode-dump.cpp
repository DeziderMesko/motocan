// Run candump log lines through Bluecan's R9 decoder and print what it makes of them.
// Built and driven by r9-decode-check.py, which compares the output against r9.dbc; not
// meant to be run by hand.
//
//   xzcat capture.log.xz | r9-decode-dump
//
// One line per distinct payload of each id the decoder knows, not per frame: a capture
// is millions of frames and a few thousand different ones, and the decoder is a pure
// function of the payload.
//
//   27C 0F8000080100B0 rpm=1550 gear=1 tcs=4 mode=0
#include "canbus/r9.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>

using namespace canbus;

int main()
{
    std::set<std::pair<uint32_t, std::string>> seen;
    char line[256];

    while (fgets(line, sizeof(line), stdin))
    {
        // (1791712607.571023) can0 27C#00000008000030
        const char* hash = strchr(line, '#');
        if (!hash)
        {
            continue;
        }
        const char* idp = hash;
        while (idp > line && idp[-1] != ' ')
        {
            --idp;
        }
        const uint32_t id = static_cast<uint32_t>(strtoul(idp, nullptr, 16));
        if (id != r9::id_engine && id != r9::id_temps && id != r9::id_throttle && id != r9::id_valve_cmd)
        {
            continue;
        }

        std::string hex(hash + 1);
        while (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r' || hex.back() == ' '))
        {
            hex.pop_back();
        }
        if (!seen.emplace(id, hex).second)
        {
            continue;
        }

        // Zero-padded to eight, as the firmware's own copy of a frame is. A function
        // that read past its frame's length would then show up as a value the DBC
        // does not give, rather than as whatever was on the stack.
        uint8_t d[8] = {0};
        for (size_t i = 0; i < 8 && (2 * i + 1) < hex.size(); i++)
        {
            d[i] = static_cast<uint8_t>(strtoul(hex.substr(2 * i, 2).c_str(), nullptr, 16));
        }

        printf("%03X %s", static_cast<unsigned>(id), hex.c_str());
        switch (id)
        {
        case r9::id_engine:
            printf(" rpm=%u gear=%u tcs=%u mode=%u", static_cast<unsigned>(r9::rpm(d)),
                   static_cast<unsigned>(r9::gear(d)), static_cast<unsigned>(r9::tcs(d)),
                   static_cast<unsigned>(r9::mode(d)));
            break;
        case r9::id_temps:
            printf(" water=%d air=%d", static_cast<int>(r9::water_tenths(d)), static_cast<int>(r9::air_tenths(d)));
            break;
        case r9::id_throttle:
            printf(" grip=%u valve=%u", static_cast<unsigned>(r9::grip_tenths(d)),
                   static_cast<unsigned>(r9::valve_tenths(d)));
            break;
        case r9::id_valve_cmd:
            printf(" valve_cmd=%u", static_cast<unsigned>(r9::valve_cmd_tenths(d)));
            break;
        }
        printf("\n");
    }

    return 0;
}
