import math

import numpy as np
from pathlib import Path

import matplotlib.pyplot as plt


SAMPLE_RATE = 48_000.0
CUTOFF_HZ = 2_000.0
Q = 0.707
NUM_SAMPLES = 4_096


class TPTSVF:
    def __init__(self, sample_rate, cutoff_hz, q):
        self.sample_rate = np.float32(sample_rate)

        self.ic1eq = np.float32(0.0)
        self.ic2eq = np.float32(0.0)

        self.set_parameters(cutoff_hz, q)

    def set_parameters(self, cutoff_hz, q):
        cutoff_hz = np.float32(cutoff_hz)
        q = np.float32(q)
        pi = np.float32(math.pi)

        self.g = np.float32(
            np.tan(pi * cutoff_hz / self.sample_rate)
        )

        self.k = np.float32(1.0) / q

        self.a1 = np.float32(1.0) / (
            np.float32(1.0)
            + self.g * (self.g + self.k)
        )

        self.a2 = self.g * self.a1
        self.a3 = self.g * self.a2

    def reset(self):
        self.ic1eq = np.float32(0.0)
        self.ic2eq = np.float32(0.0)

    def process_sample(self, input_sample):
        input_sample = np.float32(input_sample)

        v3 = input_sample - self.ic2eq

        v1 = (
            self.a1 * self.ic1eq
            + self.a2 * v3
        )

        v2 = (
            self.ic2eq
            + self.a2 * self.ic1eq
            + self.a3 * v3
        )

        two = np.float32(2.0)

        self.ic1eq = two * v1 - self.ic1eq
        self.ic2eq = two * v2 - self.ic2eq

        return v2


def generate_impulse(length):
    impulse = np.zeros(length)
    impulse[0] = 1.0
    return impulse


def process_signal(filter_instance, input_signal):
    output_signal = np.zeros_like(input_signal)

    for index, sample in enumerate(input_signal):
        output_signal[index] = filter_instance.process_sample(sample)

    return output_signal


def calculate_frequency_response(cutoff_hz, q):
    test_filter = TPTSVF(
        sample_rate=SAMPLE_RATE,
        cutoff_hz=cutoff_hz,
        q=q,
    )

    test_impulse = generate_impulse(NUM_SAMPLES)
    response = process_signal(test_filter, test_impulse)

    spectrum = np.fft.rfft(response)
    frequencies = np.fft.rfftfreq(
        NUM_SAMPLES,
        d=1.0 / SAMPLE_RATE,
    )

    magnitude_db = 20.0 * np.log10(
        np.maximum(np.abs(spectrum), 1.0e-12)
    )

    return frequencies, magnitude_db


svf = TPTSVF(
    sample_rate=SAMPLE_RATE,
    cutoff_hz=CUTOFF_HZ,
    q=Q,
)

impulse = generate_impulse(NUM_SAMPLES)
impulse_response = process_signal(svf, impulse)

print("Coefficienti:")
print(f"g  = {svf.g}")
print(f"k  = {svf.k}")
print(f"a1 = {svf.a1}")
print(f"a2 = {svf.a2}")
print(f"a3 = {svf.a3}")

print("\nPrimi 10 campioni della risposta impulsiva:")
print(impulse_response[:10])

print("\nTutti i campioni sono finiti:")
print(np.all(np.isfinite(impulse_response)))

results_directory = Path("analysis/results")
results_directory.mkdir(parents=True, exist_ok=True)


# Risposta in frequenza ottenuta dalla risposta impulsiva
spectrum = np.fft.rfft(impulse_response)

frequencies = np.fft.rfftfreq(
    NUM_SAMPLES,
    d=1.0 / SAMPLE_RATE,
)

magnitude = np.abs(spectrum)

# Evita log10(0)
magnitude_db = 20.0 * np.log10(
    np.maximum(magnitude, 1.0e-12)
)


# Grafico della risposta impulsiva
plt.figure(figsize=(10, 5))

plt.plot(impulse_response[:200])

plt.title("TPT-SVF impulse response")
plt.xlabel("Sample")
plt.ylabel("Amplitude")
plt.grid(True)

plt.tight_layout()
plt.savefig(
    results_directory / "impulse_response.png",
    dpi=150,
)

plt.show()


# Grafico della risposta in frequenza
plt.figure(figsize=(10, 5))

# Escludiamo 0 Hz perché l'asse è logaritmico
plt.semilogx(
    frequencies[1:],
    magnitude_db[1:],
)

plt.axvline(
    CUTOFF_HZ,
    color="red",
    linestyle="--",
    label=f"Cutoff = {CUTOFF_HZ:.0f} Hz",
)

plt.axhline(
    -3.0,
    color="gray",
    linestyle=":",
    label="-3 dB",
)

plt.title("TPT-SVF low-pass frequency response")
plt.xlabel("Frequency [Hz]")
plt.ylabel("Magnitude [dB]")
plt.xlim(20.0, SAMPLE_RATE / 2.0)
plt.ylim(-80.0, 10.0)
plt.grid(True, which="both")
plt.legend()

plt.tight_layout()
plt.savefig(
    results_directory / "frequency_response.png",
    dpi=150,
)

plt.show()


# Confronto della risposta in frequenza per diversi valori di Q
q_values = [0.5, 0.707, 1.0, 2.0, 4.0, 8.0]

plt.figure(figsize=(10, 6))

for q_value in q_values:
    frequencies, magnitude_db = calculate_frequency_response(
        cutoff_hz=CUTOFF_HZ,
        q=q_value,
    )

    plt.semilogx(
        frequencies[1:],
        magnitude_db[1:],
        label=f"Q = {q_value}",
    )

    cutoff_index = np.argmin(
        np.abs(frequencies - CUTOFF_HZ)
    )

    print(
        f"Q = {q_value:5.3f} | "
        f"Gain at cutoff = "
        f"{magnitude_db[cutoff_index]:6.2f} dB"
    )

plt.axvline(
    CUTOFF_HZ,
    color="black",
    linestyle="--",
    label="Cutoff",
)

plt.title("Effect of Q on the TPT-SVF response")
plt.xlabel("Frequency [Hz]")
plt.ylabel("Magnitude [dB]")
plt.xlim(20.0, SAMPLE_RATE / 2.0)
plt.ylim(-80.0, 25.0)
plt.grid(True, which="both")
plt.legend()

plt.tight_layout()
plt.savefig(
    results_directory / "q_comparison.png",
    dpi=150,
)

plt.show()


# Confronto tra diverse frequenze di cutoff con Q quasi Butterworth
cutoff_values = [
    200.0,
    500.0,
    2_000.0,
    8_000.0,
    16_000.0,
]

test_q = 0.707

plt.figure(figsize=(10, 6))

for cutoff_value in cutoff_values:
    frequencies, magnitude_db = calculate_frequency_response(
        cutoff_hz=cutoff_value,
        q=test_q,
    )

    plt.semilogx(
        frequencies[1:],
        magnitude_db[1:],
        label=f"Cutoff = {cutoff_value:.0f} Hz",
    )

    cutoff_index = np.argmin(
        np.abs(frequencies - cutoff_value)
    )

    measured_frequency = frequencies[cutoff_index]
    measured_gain_db = magnitude_db[cutoff_index]

    print(
        f"Requested cutoff = {cutoff_value:8.1f} Hz | "
        f"FFT bin = {measured_frequency:8.1f} Hz | "
        f"Gain = {measured_gain_db:6.2f} dB"
    )

plt.axhline(
    -3.0,
    color="black",
    linestyle=":",
    label="-3 dB",
)

plt.title("TPT-SVF cutoff comparison, Q = 0.707")
plt.xlabel("Frequency [Hz]")
plt.ylabel("Magnitude [dB]")
plt.xlim(20.0, SAMPLE_RATE / 2.0)
plt.ylim(-80.0, 5.0)
plt.grid(True, which="both")
plt.legend()

plt.tight_layout()
plt.savefig(
    results_directory / "cutoff_comparison.png",
    dpi=150,
)

plt.show()
