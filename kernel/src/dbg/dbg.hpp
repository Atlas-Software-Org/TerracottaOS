#ifndef DBG_HPP
#define DBG_HPP 1

#include "dbg.hpp"
#include <mem/mem.hpp>
#include <drivers/tty/ldisc/ldisc.hpp>

namespace dbg {

namespace memview {

void print_memory_contents_at(uint64_t addr, uint64_t len, uint64_t paging_len);

}

namespace disasm {

void disasm_at_memory(uint64_t addr, uint64_t len, uint64_t paging_len);

}

namespace stacktrace {

void stacktrace(uint64_t addr, uint64_t len, uint64_t paging_len);

}

}

#endif
