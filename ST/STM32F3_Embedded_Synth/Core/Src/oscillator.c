#include "oscillator.h"

#include "audio_config.h"
#include "oscillator_tables.h"

#include <math.h>

#define TABLE_INDEX_BITS 10U
#define TABLE_FRACTION_BITS (32U - TABLE_INDEX_BITS)
#define TABLE_FRACTION_MASK ((1UL << TABLE_FRACTION_BITS) - 1UL)
#define TABLE_FRACTION_SCALE (1.0f / (float)(1UL << TABLE_FRACTION_BITS))
#define OSCILLATOR_NYQUIST_MARGIN 0.90f

#if OSCILLATOR_TABLE_SIZE != TABLE_SIZE
#error "Oscillator table size must match the DDS table size"
#endif

#if (TABLE_SIZE & (TABLE_SIZE - 1U)) != 0U
#error "DDS table size must be a power of two"
#endif

volatile uint8_t wave = 0U;
volatile uint8_t oscillator_mipmapped_enabled = 0U;
volatile uint8_t wavetable_interpolation_enabled = 0U;

volatile uint32_t phase_accumulator = 0U;
uint32_t phase_increment = 0U;
uint8_t active_mip_level = 0U;
float current_frequency = 400.0f;

static int16_t sample_table[TABLE_SIZE];

void GenerateSampleTable(void)
{
    switch (wave) {
    case 0U:
        for (uint32_t i = 0U; i < TABLE_SIZE; ++i) {
            float angle = 2.0f * (float)M_PI * (float)i / (float)TABLE_SIZE;
            sample_table[i] = (int16_t)(32767.0f * sinf(angle));
        }
        break;

    case 1U:
        for (uint32_t i = 0U; i < TABLE_SIZE; ++i) {
            sample_table[i] = (i < TABLE_SIZE / 2U) ? 32767 : -32767;
        }
        break;

    case 2U:
        for (uint32_t i = 0U; i < TABLE_SIZE; ++i) {
            sample_table[i] =
                (int16_t)((int32_t)(65535U * i / TABLE_SIZE) - 32768);
        }
        break;

    default:
        for (uint32_t i = 0U; i < TABLE_SIZE; ++i) {
            sample_table[i] = 0;
        }
        break;
    }
}

static uint8_t Wavetable_SelectMipLevel(float frequency)
{
    if (frequency <= 0.0f) {
        return OSCILLATOR_MIP_LEVEL_COUNT - 1U;
    }

    const float harmonic_limit =
        (OSCILLATOR_NYQUIST_MARGIN * 0.5f * (float)SAMPLE_RATE) / frequency;

    for (uint8_t level = 0U; level < OSCILLATOR_MIP_LEVEL_COUNT; ++level) {
        if ((float)oscillator_mip_max_harmonic[level] <= harmonic_limit) {
            return level;
        }
    }

    return OSCILLATOR_MIP_LEVEL_COUNT - 1U;
}

static inline float Wavetable_ReadNearest(const int16_t *table,
                                          uint32_t phase)
{
    const uint32_t index = phase >> TABLE_FRACTION_BITS;
    return (float)table[index] * (1.0f / 32768.0f);
}

static inline float Wavetable_ReadLinear(const int16_t *table,
                                         uint32_t phase)
{
    const uint32_t index = phase >> TABLE_FRACTION_BITS;
    const uint32_t next_index = (index + 1U) & (TABLE_SIZE - 1U);
    const float fraction =
        (float)(phase & TABLE_FRACTION_MASK) * TABLE_FRACTION_SCALE;
    const float current = (float)table[index];
    const float next = (float)table[next_index];

    return (current + fraction * (next - current)) * (1.0f / 32768.0f);
}

static inline const int16_t *Oscillator_SelectTable(void)
{
    if ((oscillator_mipmapped_enabled == 0U) || (wave == 0U)) {
        return sample_table;
    }

    if (wave == 1U) {
        return oscillator_square_mip_tables[active_mip_level];
    }

    return oscillator_saw_mip_tables[active_mip_level];
}

float Oscillator_ProcessSample(void)
{
    const int16_t *table;

    phase_accumulator += phase_increment;
    table = Oscillator_SelectTable();

    if (wavetable_interpolation_enabled != 0U) {
        return Wavetable_ReadLinear(table, phase_accumulator);
    }

    return Wavetable_ReadNearest(table, phase_accumulator);
}

void SetFrequency(float frequency_hz)
{
    current_frequency = frequency_hz;
    phase_increment =
        (uint32_t)((frequency_hz * (float)(1ULL << 32)) / (float)SAMPLE_RATE);
    active_mip_level = Wavetable_SelectMipLevel(frequency_hz);
    phase_accumulator = 0U;
}
