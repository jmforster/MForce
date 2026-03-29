#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: wav_check <file.wav>\n"); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", argv[1]); return 1; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 44, SEEK_SET); // skip WAV header

    long nSamples = (fsize - 44) / 2;
    int nonzero = 0;
    int16_t peak = 0;
    double rms = 0;

    for (long i = 0; i < nSamples; ++i) {
        int16_t s;
        fread(&s, 2, 1, f);
        if (s != 0) nonzero++;
        if (abs(s) > abs(peak)) peak = s;
        rms += double(s) * double(s);
    }

    rms = sqrt(rms / nSamples);
    printf("%s: %ld samples, %d non-zero (%.1f%%), peak=%d, rms=%.1f\n",
           argv[1], nSamples, nonzero, 100.0 * nonzero / nSamples, peak, rms);

    fclose(f);
    return 0;
}
