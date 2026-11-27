# Spectral repair prototype (parked)

**Question:** can the Player offer one spectral-repair job well enough to ship it: box a whine on a spectrogram, turn it down, and A/B the result, without audibly damaging the audio around it?

**Answer for 2.2: not yet.** The tracked approach removes a steady whine well on speech, electronic music and a waltz, but it failed on a solo trumpet with a drifting whine, and a conservative audibility check could not rule out audible dents in the music next to the whine. 2.2 ships the transient shaper instead.

## How it was tested

- **Material.** Four real recordings: speech (14 s), solo trumpet (5 s), an electronic track (61 s) and a waltz (49 s).
- **Injected whine.** Each recording got three kinds of whine:
  - a steady tone;
  - a slowly drifting tone, both at 25 dB below the music's RMS;
  - a 15.7 kHz CRT whine at 30 dB below.
- **Selection box.** From 0.5 s after the start to 0.5 s before the end, ±50 Hz around the whine.
- **Exact damage split.** The processing is linear once its mask or fit is known. That lets the clean music and the whine be run through it separately, so leftover whine and damage to the music are measured exactly rather than estimated.
- **Audibility check.** A frame counts as audible damage when the damage inside ±1 ERB of the whine is louder than the music there minus 15 dB, and is above the threshold of hearing (taking a 0 dBFS sine as 100 dB SPL). This is a conservative proxy, not a listening test.

## Results

**Spectral subtraction in the box (STFT).** Audio outside the box stays bit-exact.

- It only took the whine down 11 to 28 dB.
- It removed 2 to 10 dB of the music inside the box band.
- A plain notch did better on the whine (−22 to −28 dB), but it removed all the music in the band.

**Sinusoid tracking and subtraction.** The approach:

- Track the whine's frequency per frame against a running-median spectrum.
- Fit its amplitude and phase in short blocks, clamp outlier blocks and subtract.

| Recording | Whine | Whine left | Damage vs whole mix | Frames with possibly audible damage |
| --- | --- | --- | --- | --- |
| Speech | steady | −23 dB | −35 dB | 35 % |
| Speech | drift | −29.5 dB | −40 dB | 39 % |
| Trumpet | steady | −19.5 dB | −36 dB | 24 % |
| Trumpet | drift | **−5.8 dB (fails)** | −32 dB | 21 % |
| Electronic | steady / drift | −41 to −42 dB | −52 dB | 58 to 62 % |
| Waltz | steady | −22.6 dB | −41 dB | 57 % |
| Waltz | drift | −14.8 dB | −43 dB | 39 % |

- **Whine audibility.** The whine was audible in 82 to 100 % of frames before processing and 0.3 to 24 % after.
- **CRT whine (15.7 kHz).** It sat below the threshold of hearing in this material, both before and after; the damage was −70 to −78 dB.

## Why it is parked

1. **The trumpet drift case fails.** The trumpet's own notes cross the whine, and the tracker follows the notes.
2. **The damage can't be certified as inaudible.** It is small against the whole mix, but the conservative check flags partials next to the whine in 21 to 62 % of frames, and these results were not listened to.

To pick it up again:

- **Listen first:** an A/B by ear on these cases decides whether the flagged frames matter.
- **Then fix the tracker:** stop it locking onto musical partials, for example with harmonic-aware tracking or a user-drawn path.

The prototype code and evaluation scripts are kept outside the repository.
