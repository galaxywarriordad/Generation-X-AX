#include "hwaudio/segapcm.hpp"

#include <cmath>

SegaPCM::SegaPCM(uint32_t clock, RomLoader* rom, uint8_t* ram, int32_t bank)
{
    this->ram = ram;
    pcm_rom = rom->rom;
    low = new uint8_t[16];
    max_addr = rom->length;
    bankshift = bank & 0xFF;
    rgnmask = max_addr - 1;

    int32_t mask = bank >> 16;
    if (mask == 0)
        mask = BANK_MASK7 >> 16;

    int32_t rom_mask;
    for (rom_mask = 1; rom_mask < max_addr; rom_mask *= 2)
        ;
    rom_mask--;

    bankmask = mask & (rom_mask >> bankshift);

    for (int32_t i = 0; i < 0x100; i++)
        ram[i] = 0xff;
}

SegaPCM::~SegaPCM()
{
    delete[] low;
}

void SegaPCM::init(int32_t rate, int32_t fps)
{
    this->sample_freq = rate;
    downsample = (32000.0 / (double)(rate > 0 ? rate : 44100));
    SoundChip::init(STEREO, rate, fps);
}

void SegaPCM::stream_update()
{
    SoundChip::clear_buffer();

    const double fp_scale = downsample * 65536.0;

    for (int ch = 0; ch < 16; ch++)
    {
        uint8_t *regs = ram + 8 * ch;

        if ((regs[0x86] & 1) == 0)
        {
            uint8_t *rom = pcm_rom + ((regs[0x86] & bankmask) << bankshift);

            uint32_t addr = (regs[0x85] << 16) | (regs[0x84] << 8) | low[ch];
            const uint32_t loop = (regs[0x05] << 16) | (regs[0x04] << 8);
            const uint8_t end = regs[0x06] + 1;
            const uint32_t step_fp = static_cast<uint32_t>(std::floor(static_cast<double>(regs[7]) * fp_scale + 0.5));
            const uint32_t step_int = step_fp >> 16;
            const uint32_t step_frac = step_fp & 0xFFFFu;
            uint32_t frac_acc = 0;

            for (uint32_t i = 0; i < frame_size; i++)
            {
                if ((addr >> 16) == end)
                {
                    if ((regs[0x86] & 2) == 0)
                    {
                        addr = loop;
                    }
                    else
                    {
                        regs[0x86] |= 1;
                        break;
                    }
                }

                const int8_t v = static_cast<int8_t>(rom[(addr >> 8) & rgnmask] - 0x80);

                write_buffer(LEFT,  i, read_buffer(LEFT,  i) + (v * regs[2]));
                write_buffer(RIGHT, i, read_buffer(RIGHT, i) + (v * regs[3]));

                frac_acc += step_frac;
                const uint32_t carry = frac_acc >> 16;
                frac_acc &= 0xFFFFu;
                addr = (addr + step_int + carry) & 0xffffffu;
            }

            regs[0x84] = static_cast<uint8_t>(addr >> 8);
            regs[0x85] = static_cast<uint8_t>(addr >> 16);
            low[ch] = (regs[0x86] & 1) ? 0 : static_cast<uint8_t>(addr & 0xFFu);
        }
    }
}
