import os
import sys
import numpy as np
import matplotlib.pyplot as plt

def perfect_radar_diagnostic(file_path, sample_rate=4e6, num_chirps=256, samples_per_chirp=256, is_down=False):
    """
    Mathematically rigorous LChirp signal diagnostics.
    Strictly separates UP and DOWN logic via the is_down flag to eliminate Nyquist edge artifacts.
    """
    direction_str = "DOWN-CHIRP (REVERSE)" if is_down else "UP-CHIRP (FORWARD)"
    print(f"=== RUNNING DIAGNOSTICS ({direction_str}): {os.path.basename(file_path)} ===")
    
    expected_samples = num_chirps * samples_per_chirp
    if not os.path.exists(file_path):
        print(f"❌ ERROR: File {file_path} not found.")
        return
        
    raw_data = np.fromfile(file_path, dtype=np.int16, count=expected_samples * 2)
    
    # Shape into 2D matrices [256 chirps x 256 samples]
    I = raw_data[0::2].astype(np.float64).reshape((num_chirps, samples_per_chirp))
    Q = raw_data[1::2].astype(np.float64).reshape((num_chirps, samples_per_chirp))
    Z = I + 1j * Q
    
    dt = 1.0 / sample_rate
    chirps_profiles = []
    all_f_mean = []
    
    for i in range(num_chirps):
        single_chirp = Z[i, :]
        
        # Calculate phase delta between adjacent points via complex multiplication (length 255)
        dot_product = single_chirp[1:].real * single_chirp[:-1].real + single_chirp[1:].imag * single_chirp[:-1].imag
        cross_product = single_chirp[1:].imag * single_chirp[:-1].real - single_chirp[1:].real * single_chirp[:-1].imag
        
        # Instantaneous frequency over intervals
        d_freq = np.arctan2(cross_product, dot_product) / (2 * np.pi * dt)
        
        # Mode-specific handling to fix Nyquist edge wrapping instabilities
        if is_down:
            d_freq = np.remainder(d_freq + sample_rate/2, sample_rate) - sample_rate/2
        else:
            d_freq = np.where(d_freq > (sample_rate / 2 - 100), -sample_rate / 2, d_freq)
        
        # Restore 256-point grid by duplicating the last interval
        freq_profile = np.zeros(samples_per_chirp)
        freq_profile[:-1] = d_freq
        freq_profile[-1] = d_freq[-1]
        
        chirps_profiles.append(freq_profile)
        all_f_mean.append(np.mean(freq_profile))
        
    chirps_profiles = np.array(chirps_profiles)
    all_f_mean = np.array(all_f_mean)
    
    # Extract physical parameters from the first chirp
    first_chirp_freq = chirps_profiles[0, :]
    f0_fact = first_chirp_freq[0]
    f1_fact = first_chirp_freq[-2]  # Actual terminal interval before padding
    
    print("\n=== PHYSICAL PARAMETERS ===")
    print(f"  • f0 (Start Freq):      {f0_fact/1e6:+.4f} MHz")
    print(f"  • f1 (Stop Freq):       {f1_fact/1e6:+.4f} MHz")
    print(f"  • Chirp Bandwidth (B):  {abs(f1_fact - f0_fact)/1e6:.4f} MHz")
    print(f"  • Mean Frequency:       {np.mean(all_f_mean)/1e6:+.4f} MHz")
    
    print("\n=== BURST AMPLITUDE ANALYSIS ===")
    mean_i, mean_q = np.mean(I), np.mean(Q)
    print(f"  • I Channel Range:      [{int(np.min(I))}, {int(np.max(I))}] | DC Offset: {mean_i:+.2f}")
    print(f"  • Q Channel Range:      [{int(np.min(Q))}, {int(np.max(Q))}] | DC Offset: {mean_q:+.2f}")
    
    print("\n=== BURST COHERENCE COEFFS ===")
    diff_matrix = chirps_profiles - first_chirp_freq
    max_deviation = np.max(np.abs(diff_matrix))
    
    print(f"  • Max Profile Deviation: {max_deviation:.6f} Hz")
    if max_deviation < 1.0:
        print("✅ VERDICT: All chirps are perfectly coherent and identical. Generator is stable.")
    else:
        print("❌ ERROR: Frequency trajectories vary between chirps!")

    # Graphics Rendering
    fig, axs = plt.subplots(3, 1, figsize=(10, 12))
    time_axis = np.arange(samples_per_chirp) * dt * 1e6
    
    # 1. Waveform Plot
    axs[0].plot(time_axis, I[0, :], label='I (Real)', color='blue', alpha=0.7)
    axs[0].plot(time_axis, Q[0, :], label='Q (Imag)', color='orange', alpha=0.7)
    axs[0].set_title("Time-Domain Waveform of the First Chirp")
    axs[0].set_xlabel("Time, µs")
    axs[0].set_ylabel("Amplitude")
    axs[0].grid(True)
    axs[0].legend()
    
    # 2. Instantaneous Frequency Plot
    axs[1].plot(time_axis, first_chirp_freq / 1e6, color='green', linewidth=2)
    axs[1].set_title(f"Instantaneous Frequency Sweep ({direction_str})")
    axs[1].set_xlabel("Time, µs")
    axs[1].set_ylabel("Frequency, MHz")
    axs[1].grid(True)
    axs[1].set_ylim([-2.1, 2.1])
    
    # 3. IQ XY-Plot
    axs[2].plot(I[0, :], Q[0, :], color='purple', alpha=0.6, marker='.', markersize=4)
    axs[2].set_title("Complex IQ Plane Constellation (Chirp Trajectory)")
    axs[2].set_xlabel("I Channel")
    axs[2].set_ylabel("Q Channel")
    axs[2].axis('equal')
    axs[2].grid(True)
    
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    FILE_PATH = "chirp.ci16" 
    SAMPLE_RATE = 4e6         
    NUM_CHIRPS = 256          
    SAMPLES_PER_CHIRP = 256   
    
    run_down_mode = False
    if len(sys.argv) > 1 and sys.argv[1].lower() == 'down':
        run_down_mode = True
        
    perfect_radar_diagnostic(
        file_path=FILE_PATH, 
        sample_rate=SAMPLE_RATE, 
        num_chirps=NUM_CHIRPS, 
        samples_per_chirp=SAMPLES_PER_CHIRP,
        is_down=run_down_mode
    )
