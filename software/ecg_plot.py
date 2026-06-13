import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import numpy as np
import sys
from scipy.signal import find_peaks

sys.stdout.reconfigure(encoding='utf-8')

# CONFIG
PORT     = 'COM8'
BAUD     = 921600
FS       = 1000
WINDOW   = 5 * FS
VREF     = 3.3
ADC_MAX  = 4095

# SERIAL
try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"Da ket noi {PORT} @ {BAUD} baud")
except Exception as e:
    print(f"Loi mo serial: {e}")
    sys.exit(1)

# BUFFER

buf_volt = deque([0.0] * WINDOW, maxlen=WINDOW)
bpm_history = deque([0.0] * 10, maxlen=10)  # 10 most recent bpm
current_bpm = 0.0


# SETUP PLOT

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(13, 8))
fig.suptitle('ECG Real-time — STM32F446RE @ 1000 Hz', fontsize=13)

# --- Subplot 1: ECG Waveform + BPM ---
line_volt, = ax1.plot([], [], lw=1, color='steelblue')
peaks_scatter = ax1.scatter([], [], color='red', s=50, zorder=5, label='R peaks')
ax1.set_xlim(0, WINDOW)
ax1.set_ylim(0, 3.3)
ax1.set_ylabel('Voltage (V)', fontsize=11)
ax1.axhline(1.65, ls='--', color='gray', alpha=0.5, label='Mid-rail 1.65V')
ax1.grid(True, alpha=0.3)
ax1.legend(loc='upper right', fontsize=9)
bpm_text = ax1.text(0.02, 0.92, 'BPM: --',
                    transform=ax1.transAxes,
                    fontsize=16, fontweight='bold',
                    color='red',
                    bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))
ax1.set_title('ECG Waveform', fontsize=11)

# --- Subplot 2: FFT Spectrum ---
fft_freqs = np.fft.rfftfreq(WINDOW, d=1.0/FS)
line_fft,  = ax2.plot([], [], lw=1.5, color='darkorange')
ax2.set_xlim(0, 150)
ax2.set_ylim(0, 0.5)
ax2.set_xlabel('Frequency (Hz)', fontsize=11)
ax2.set_ylabel('Amplitude (V)', fontsize=11)
ax2.grid(True, alpha=0.3)
ax2.set_title('FFT Spectrum (0-150Hz)', fontsize=11)

x_data = list(range(WINDOW))

fft_counter = 0
FFT_UPDATE_INTERVAL = 25
BPM_UPDATE_INTERVAL = 50   # update bpm every 50 frames

bpm_counter = 0


# ca;lculate BPM
def calculate_bpm(data):
    arr = np.array(data)

    # Normalize de detect peak
    arr_norm = arr - np.mean(arr)
    if np.std(arr_norm) == 0:
        return 0.0

    # Threshold = 50% cua max amplitude
    threshold = 0.5 * np.max(arr_norm)

    # Tim R peaks:
    # - height: peak needs to be > threshold
    # - distance: 2 peaks must be at least 300ms apart (0.3s * FS)
    peaks, _ = find_peaks(arr_norm,
                          height=threshold,
                          distance=int(0.3 * FS))

    if len(peaks) < 2:
        return 0.0

    # R-R distance
    rr_intervals = np.diff(peaks) / FS 
    rr_mean = np.mean(rr_intervals)

    if rr_mean <= 0:
        return 0.0

    bpm = 60.0 / rr_mean
    return bpm, peaks

# ANIMATION UPDATE
def update(frame):
    global fft_counter, bpm_counter, current_bpm

    while ser.in_waiting:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                adc  = int(line)
                adc  = max(0, min(4095, adc))
                volt = adc * VREF / ADC_MAX
                buf_volt.append(volt)
        except (ValueError, UnicodeDecodeError):
            pass

    data = list(buf_volt)
    line_volt.set_data(x_data, data)

    # Update BPM
    bpm_counter += 1
    if bpm_counter >= BPM_UPDATE_INTERVAL:
        bpm_counter = 0
        try:
            result = calculate_bpm(data)
            if result != 0.0:
                bpm, peaks = result
                
                if 30 <= bpm <= 200:
                    bpm_history.append(bpm)
                    current_bpm = np.mean(bpm_history)
                    bpm_text.set_text(f'BPM: {current_bpm:.0f}')

                    # Hien thi R peaks tren waveform
                    peaks_scatter.set_offsets(
                        np.c_[peaks, np.array(data)[peaks]]
                    )
        except:
            pass

    # Update FFT
    fft_counter += 1
    if fft_counter >= FFT_UPDATE_INTERVAL:
        fft_counter = 0
        arr = np.array(data)
        arr = arr - np.mean(arr)
        win     = np.hanning(len(arr))
        fft_val = np.abs(np.fft.rfft(arr * win)) * 2.0 / np.sum(win)
        idx_max = np.argmin(np.abs(fft_freqs - 150))
        line_fft.set_data(fft_freqs[:idx_max], fft_val[:idx_max])
        if len(fft_val) > 0:
            peak = np.max(fft_val[:idx_max])
            if peak > 0:
                ax2.set_ylim(0, peak * 1.3)

    return line_volt, line_fft, bpm_text, peaks_scatter


# RUN

ani = animation.FuncAnimation(
    fig,
    update,
    interval=20,
    blit=True,
    cache_frame_data=False
)

plt.tight_layout()

try:
    plt.show()
except KeyboardInterrupt:
    pass
finally:
    ser.close()
    print("Da dong serial port.")