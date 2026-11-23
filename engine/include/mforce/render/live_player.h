#pragma once
#include "mforce/contract/performance.h"
#include "mforce/core/spsc_queue.h"
#include "mforce/patch/patch.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/ring_out.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace mforce {

// One instrument played live behind one function, fill().
//
// A voice is touched by one thread at a time, and ownership passes with a
// message. The control thread (one thread: note_on, note_off and the setters)
// owns the idle voices and its records of the voices it has handed over; it
// starts a voice before sending it. The audio thread (fill) owns the voices
// it was sent and its sounding set, and gives each voice back when it is
// over. The two queues and the volume are the only members both touch.
class LivePlayer {
public:
  using NoteId = std::uint32_t;

  // Sounds at most `voices` notes at once, or the patch's polyphony when that
  // is larger, and builds twice that many voices: the spares let a new note
  // start while a stolen one fades.
  LivePlayer(const Patch& patch, int voices);

  // --- control thread ---
  // The note at the top of the next buffer, or carried onto the open held
  // line. 0 when it is not sent: no idle voice, or the queue is full.
  NoteId note_on(const PerformedNote& note);
  // Releases the open line when `id` is its current note; otherwise nothing.
  void note_off(NoteId id);
  void set_wheel(float v);
  void set_pressure(float v);
  void set_volume(float v);
  int  sample_rate() const;
  // The voices handed to the audio thread and not yet given back.
  int  sounding();

  // --- audio thread ---
  // `frames` samples of the instrument's mono output. Never blocks,
  // allocates, frees, prints or throws.
  void fill(float* mono, int frames) noexcept;

private:
  // A message from the control thread to the audio thread.
  struct Message {
    enum class Kind : std::uint8_t { Start, Carry, Release, Fade };
    Kind  kind{Kind::Start};
    int   voice{-1};
    NoteId id{0};
    float gain{1.0f};          // Start: the note's velocity
    int   bodySamples{0};      // Start: the note's length in samples
    bool  held{false};         // Start: the voice opens a held line
    Voice::Note note{};        // Carry: the next note (no curve)
    bool  hold{false};         // Carry: the next note holds
    float fromFreq{0.0f};      // Carry: the frequency glided from
  };
  // A voice the audio thread gives back.
  struct Done { int voice{-1}; };

  // The audio thread's record of a sounding voice.
  struct Sounding {
    int   voice{-1};
    float gain{1.0f};
    RingOut ring;
    int   played{0};           // samples since the voice started
    int   noteSamples{0};      // samples since the current note began
    int   releaseAt{-1};       // release when the current note has played this many
    int   fadeLeft{-1};        // samples of the fade still to play; -1 when not fading
    bool  over{false};
  };
  // The control thread's record of a voice.
  struct Handed {
    bool     out{false};       // handed to the audio thread
    NoteId   id{0};
    std::uint64_t order{0};    // when it started
    bool     released{false};  // no key holds it
    bool     fading{false};
  };
  // The open held line, on the control side.
  struct Line {
    bool   open{false};
    int    voice{-1};
    NoteId id{0};              // its current note
    float  lastFreq{0.0f};
  };

  static constexpr std::size_t kQueueSize = 256;

  // Control thread.
  void drain_done();
  int  idle_voice() const;
  bool steal_if_full();
  NoteId carry(const PerformedNote& note, const Voice::Note& n);
  NoteId start_fresh(const PerformedNote& note, const Voice::Note& n);
  // Audio thread.
  void apply(const Message& m) noexcept;
  Sounding* find(int voice) noexcept;
  void release(Sounding& s, int lastNoteSamples) noexcept;
  void render(Sounding& s, float* mono, int frames) noexcept;
  void give_back() noexcept;

  // Fixed at construction; read by both threads, written by neither.
  std::unique_ptr<PitchedInstrument> instrument_;
  int sampleRate_{0};
  int voices_{0};
  int fadeSamples_{0};
  float glideSec_{0.0f};

  // Both threads.
  SpscQueue<Message, kQueueSize> toAudio_;
  SpscQueue<Done, kQueueSize>    toControl_;
  std::atomic<float>             volume_{1.0f};

  // Control thread only.
  std::vector<Handed> handed_;   // one per voice of the pool
  Line line_;
  NoteId nextId_{1};
  std::uint64_t nextOrder_{0};

  // Audio thread only.
  std::vector<Sounding> sounding_;   // capacity for every voice, reserved at construction
};

} // namespace mforce
