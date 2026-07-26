#include "fb2k_sdk.h"

DECLARE_COMPONENT_VERSION(
    "Auto Calibration Convolver",
    "1.0.0",
    "Automatically convolves playback with a calibration impulse response (WAV)\n"
    "matched to the current stream sample rate.\n\n"
    "Files are looked up in a user-configured folder using a filename template\n"
    "such as Calibration_{samplerate}.wav. When no matching file exists, audio\n"
    "passes through untouched and the reason is reported in the Console.\n\n"
    "DSP name: Auto Calibration Convolver\n"
);

VALIDATE_COMPONENT_FILENAME("foo_dsp_autoconv.dll");
