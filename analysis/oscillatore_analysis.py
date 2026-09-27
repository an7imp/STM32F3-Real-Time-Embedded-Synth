from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


SAMPLE_RATE = 48_000
TABLE_SIZE = 1_024
DURATION_SECONDS = 1.0

TEST_FREQUENCIES = [
    220.0,
    4_000.0,
]


def generate_wavetable(waveform):
    indices = np.arange(TABLE_SIZE)

    if waveform == "sine":
        phase = 2.0 * np.pi * indices / TABLE_SIZE
        table = np.sin(phase) * 32767.0

    elif waveform == "square":
        table = np.where(
            indices < TABLE_SIZE // 2,
            32767.0,
            -32767.0,
        )

    elif waveform == "saw":
        table = (
            (65535 * indices // TABLE_SIZE)
            - 32768
        )

    else:
        raise ValueError(f"Unknown waveform: {waveform}")

    return table.astype(np.int16)


def generate_dds_signal(waveform, frequency):
    wavetable = generate_wavetable(waveform)

    sample_count = int(
        SAMPLE_RATE * DURATION_SECONDS
    )

    output = np.zeros(sample_count, dtype=np.float32)

    phase_accumulator = 0

    phase_increment = int(
        frequency * (1 << 32) / SAMPLE_RATE
    )

    for index in range(sample_count):
        phase_accumulator = (
            phase_accumulator + phase_increment
        ) & 0xFFFFFFFF

        table_index = phase_accumulator >> 22

        output[index] = (
            np.float32(wavetable[table_index])
            / np.float32(32768.0)
        )

    return output


def calculate_spectrum(signal):
    window = np.hanning(len(signal))

    windowed_signal = signal * window

    spectrum = np.fft.rfft(windowed_signal)

    magnitude = (
        2.0 * np.abs(spectrum) / np.sum(window)
    )

    magnitude_db = 20.0 * np.log10(
        np.maximum(magnitude, 1.0e-12)
    )

    frequencies = np.fft.rfftfreq(
        len(signal),
        d=1.0 / SAMPLE_RATE,
    )

    return frequencies, magnitude_db


results_directory = Path("analysis/results")
results_directory.mkdir(parents=True, exist_ok=True)

waveforms = ["sine", "square", "saw"]

figure, axes = plt.subplots(
    len(waveforms),
    len(TEST_FREQUENCIES),
    figsize=(14, 10),
    sharex=True,
    sharey=True,
)

for row, waveform in enumerate(waveforms):
    for column, frequency in enumerate(TEST_FREQUENCIES):
        signal = generate_dds_signal(
            waveform,
            frequency,
        )

        frequencies, magnitude_db = calculate_spectrum(
            signal
        )

        axis = axes[row, column]

        axis.plot(
            frequencies,
            magnitude_db,
            linewidth=0.8,
        )

        axis.set_title(
            f"{waveform.capitalize()} — "
            f"{frequency:.0f} Hz"
        )

        axis.set_xlim(0.0, SAMPLE_RATE / 2.0)
        axis.set_ylim(-120.0, 5.0)
        axis.grid(True)

        if row == len(waveforms) - 1:
            axis.set_xlabel("Frequency [Hz]")

        if column == 0:
            axis.set_ylabel("Magnitude [dBFS]")

figure.suptitle(
    "Current STM32 wavetable DDS spectra",
    fontsize=14,
)

figure.tight_layout()

figure.savefig(
    results_directory
    / "oscillator_spectrum_naive.png",
    dpi=150,
)

plt.show()