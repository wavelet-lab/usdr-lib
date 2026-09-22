import os
import numpy as np
import matplotlib.pyplot as plt

def analyze_chirp_fft(file_path, sample_rate=4e6, num_chirps=256, samples_per_chirp=256):
    """
    Spectral Analysis (FFT) of the generated chirp signal.
    Visualizes the raw spectrum shape and measures frequency band symmetry.
    """
    print(f"=== RUNNING FFT ANALYSIS: {os.path.basename(file_path)} ===")
    
    # 1. Read file data
    expected_samples = num_chirps * samples_per_chirp
    if not os.path.exists(file_path):
        print(f"❌ ERROR: File {file_path} not found.")
        return
        
    raw_data = np.fromfile(file_path, dtype=np.int16, count=expected_samples * 2)
    
    # Assemble complex signal Z = I + j*Q
    I = raw_data[0::2].astype(np.float64)
    Q = raw_data[1::2].astype(np.float64)
    Z = I + 1j * Q
    
    # Shape into matrices [chirps, samples]
    chirps = Z.reshape((num_chirps, samples_per_chirp))
    
    # Use the very first chirp for analysis (all are identical due to coherence)
    single_chirp = chirps[0, :]
    
    # 2. Compute FFT
    # Use a rectangular window (no windowing) to observe raw LChirp mathematics
    fft_data = np.fft.fft(single_chirp)
    
    # Shift the spectrum so that 0 Hz is centered
    fft_shifted = np.fft.fftshift(fft_data)
    
    # Convert to magnitude (in dB, normalized to the maximum bin)
    amplitude_magnitude = np.abs(fft_shifted)
    amplitude_db = 20 * np.log10(amplitude_magnitude / np.max(amplitude_magnitude))
    
    # Frequency axis grid from -Fs/2 to +Fs/2
    freq_axis = np.fft.fftshift(np.fft.fftfreq(samples_per_chirp, d=1.0/sample_rate))
    
    # 3. SPECTRUM SYMMETRY VERIFICATION
    # Compare the left half (negative frequencies) with the flipped right half
    left_half = amplitude_magnitude[:samples_per_chirp // 2]
    right_half = amplitude_magnitude[samples_per_chirp // 2:]
    right_half_flipped = np.flip(right_half)  # flip the right wing
    
    # Calculate absolute delta between symmetrical bins
    spectrum_diff = np.abs(left_half - right_half_flipped)
    max_spec_err = np.max(spectrum_diff) / np.max(amplitude_magnitude) * 100
    
    print("\n=== SPECTRAL ANALYSIS RESULTS ===")
    print(f"  • FFT Size:           {samples_per_chirp} points")
    print(f"  • Spectrum Asymmetry: {max_spec_err:.6f}%")
    
    # If asymmetry is negligible, the spectrum is perfectly mirrored
    if max_spec_err < 1e-3:
        print("✅ VERDICT: Chirp spectrum is perfectly symmetrical around 0 Hz.")
    else:
        print("⚠️ WARNING: Slight spectrum asymmetry detected.")

    # 4. PLOT RENDERING
    plt.figure(figsize=(10, 6))
    
    # Plot spectrum in dB
    plt.plot(freq_axis / 1e6, amplitude_db, color='crimson', linewidth=2, label='Chirp Spectrum (FFT)')
    
    # Highlight the DC central bin (0 Hz) where our DC offset resides
    dc_index = samples_per_chirp // 2
    plt.plot(freq_axis[dc_index] / 1e6, amplitude_db[dc_index], 'go', markersize=8, 
             label=f'Central DC Bin (0 Hz): {amplitude_db[dc_index]:.2f} dB')
    
    plt.title("Magnitude Spectrum of Generated Chirp (Rectangular Window)")
    plt.xlabel("Frequency, MHz")
    plt.ylabel("Magnitude, dB")
    plt.ylim([-40, 5])  # Lower bound limited for noise floor clarity
    plt.grid(True, which='both', linestyle='--', alpha=0.7)
    plt.legend()
    
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    FILE_PATH = "chirp.ci16" 
    SAMPLE_RATE = 4e6         
    NUM_CHIRPS = 256          
    SAMPLES_PER_CHIRP = 256   
    
    analyze_chirp_fft(FILE_PATH, SAMPLE_RATE, NUM_CHIRPS, SAMPLES_PER_CHIRP)
