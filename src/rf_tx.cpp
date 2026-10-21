#include "rf_tx.h"

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/pio.h>

namespace rf {

namespace {

PIO pio = nullptr;
uint sm = 0;

// The PIO program, built with the SDK's encoders rather than pioasm:
//
//   .wrap_target
//       pull block      ; mark length
//       out x, 32
//       set pins, 1
//   mark:
//       jmp x-- mark
//       pull block      ; space length
//       out x, 32
//       set pins, 0
//   space:
//       jmp x-- space
//   .wrap
//
// From one `set` to the next is 1 (set) + x+1 (loop) + 2 (pull, out) = x+4
// ticks, so each word is the length in µs minus OVERHEAD. After the last
// space, `pull block` waits with the pin low.
constexpr uint32_t OVERHEAD = 4;
uint16_t program[8];

void put(uint32_t us, void (*tick)()) {
    pio_sm_put_blocking(pio, sm, us > OVERHEAD ? us - OVERHEAD : 0);
    if (tick) tick();
}

} // namespace

bool begin(uint8_t pin) {
    program[0] = pio_encode_pull(false, true);
    program[1] = pio_encode_out(pio_x, 32);
    program[2] = pio_encode_set(pio_pins, 1);
    program[3] = pio_encode_jmp_x_dec(3);
    program[4] = pio_encode_pull(false, true);
    program[5] = pio_encode_out(pio_x, 32);
    program[6] = pio_encode_set(pio_pins, 0);
    program[7] = pio_encode_jmp_x_dec(7);
    pio_program_t prog{};
    prog.instructions = program;
    prog.length = 8;
    prog.origin = -1;  // relocatable: the SDK adjusts the jmp targets

    uint offset;
    if (!pio_claim_free_sm_and_add_program(&prog, &pio, &sm, &offset)) return false;

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 7);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_out_shift(&c, false, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);  // 8 words deep
    sm_config_set_clkdiv(&c, static_cast<float>(clock_get_hz(clk_sys)) / 1000000.0f);  // 1 tick = 1 µs

    pio_sm_set_pins_with_mask(pio, sm, 0, 1u << pin);  // idle low before the PIO takes the pin
    pio_sm_set_pindirs_with_mask(pio, sm, 1u << pin, 1u << pin);
    pio_gpio_init(pio, pin);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
    return true;
}

void send(const uint16_t* lead, size_t leadLen, const uint16_t* code, size_t codeLen, int repeats, void (*tick)()) {
    if (!pio) return;
    for (size_t i = 0; i < leadLen; ++i) put(lead[i], tick);
    for (int r = 0; r < repeats; ++r)
        for (size_t i = 0; i < codeLen; ++i) put(code[i], tick);

    // Done when the state machine stalls on an empty FIFO: that is the
    // `pull` after the final space. Clear the stall flag now, so only a
    // stall after the last word counts.
    const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    pio->fdebug = stall;
    const uint32_t start = millis();
    while (!(pio->fdebug & stall) && millis() - start < 100) {
        if (tick) tick();
    }
}

} // namespace rf
