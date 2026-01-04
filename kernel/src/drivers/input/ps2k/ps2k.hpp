#ifndef PS2K_HPP
#define PS2K_HPP 1

#include <cstdint>
#include <cstddef>
#include "ps2k_key_event.hpp"

typedef void (*event_callback_fn)(const key_event& ev, void* userdata);

namespace drivers::input::ps2k {

void initialise();

bool read(key_event& ev);
size_t read_events(key_event* events, size_t max_events);
size_t available_events();
void flush_events();
void set_event_callback(event_callback_fn callback, void* userdata);
void clear_event_callback();

void user_ps2k_poll();

}

#endif
