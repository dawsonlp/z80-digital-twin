#include "pasmo_source.h"

#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: z80_disassemble --org 0xADDR program.bin > program.asm\n";
        return 0;
    }
    try {
        if (argc != 4 || std::string_view(argv[1]) != "--org")
            throw std::invalid_argument("usage: z80_disassemble --org 0xADDR program.bin");
        std::string_view value = argv[2];
        int base = 10;
        if (value.starts_with("0x") || value.starts_with("0X")) { value.remove_prefix(2); base = 16; }
        else if (value.starts_with('$')) { value.remove_prefix(1); base = 16; }
        unsigned origin = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), origin, base);
        if (ec != std::errc{} || end != value.data() + value.size() || origin > 65535)
            throw std::invalid_argument("origin must be an address in 0..65535");
        std::ifstream input(argv[3], std::ios::binary);
        if (!input) throw std::runtime_error("cannot open input binary");
        std::vector<uint8_t> bytes;
        char byte;
        while (input.get(byte)) {
            bytes.push_back(static_cast<uint8_t>(byte));
            if (bytes.size() > 65536u - origin)
                throw std::invalid_argument("binary range exceeds the 64 KB address space");
        }
        if (!input.eof()) throw std::runtime_error("cannot read input binary");
        std::cout << z80::dbg::DisassemblePasmo(bytes, static_cast<uint16_t>(origin));
        if (!std::cout) throw std::runtime_error("cannot write source output");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "z80_disassemble: " << error.what() << '\n';
        return 1;
    }
}
