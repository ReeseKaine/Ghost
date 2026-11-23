// ym2151_wrap.cpp -- a C face for ymfm's YM2151 (host/ymfm, BSD 3-Clause), used by g2sfx.c
#include "ymfm/ymfm_opm.h"
namespace {
struct ym_if : ymfm::ymfm_interface {};
struct ym_box { ym_if intf; ymfm::ym2151 chip; ym_box() : chip(intf) { chip.reset(); } };
}
extern "C" {
void *ym2151_new(void) { return new ym_box(); }
void ym2151_free(void *p) { delete static_cast<ym_box *>(p); }
void ym2151_write(void *p, int port, int data)
{
    ym_box *b = static_cast<ym_box *>(p);
    if (port & 1) b->chip.write_data((uint8_t)data); else b->chip.write_address((uint8_t)data);
}
int ym2151_status(void *p) { return static_cast<ym_box *>(p)->chip.read_status(); }
void ym2151_sample(void *p, int *left, int *right)   // one sample at clock / 64
{
    ymfm::ym2151::output_data o;
    static_cast<ym_box *>(p)->chip.generate(&o, 1);
    *left = o.data[0]; *right = o.data[1];
}
}
