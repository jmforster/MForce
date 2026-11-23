#pragma once
#include "mforce/contract/performance.h"
#include "mforce/core/spsc_queue.h"
#include "mforce/patch/patch.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/ring_out.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace mforce {

// One instrument played live behind one function, fill().
//
// A voice is touched by one thread at a time, and ownership passes with a
// message. The control thread (one thread: note_on, note_off, the setters and
// sounding) owns the idle voices and its records of the voices it has handed
// over; it starts a voice before sending it. The audio thread (fill) owns the
// voices it was sent and its sounding set, and gives each voice back when it
// is over, so both threads touch the voices, never the same one at once.
// Both also touch the two queues, the volume, and the instrument's played
// state (wheel and pressure: atomics the control thread writes and the audio
// thread reads). The audio thread reads the instrument's peakGuard, which
// must not change while the player runs.
//
// A pin's constant (set_value) is written into a voice by whichever thread
// holds it: the audio thread writes a Set message into each voice it holds,
// and the control thread writes every value set so far into a voice just
// before it starts it. So each voice is still written by one thread at a
// time, and a voice has every value whichever thread held it when the value
// was set.
class LivePlayer {
public:
  using NoteId = std::uint32_t;

  // Sounds at most `voices` notes at once, or the patch's polyphony when that
  // is larger, and builds twice that many voices: the spares let a new note
  // start while a stolen one fades.
  LivePlayer(const Patch& patch, int voices);

  // --- control thread ---
  // The note at the top of the next buffer, or carried onto the open held
  // line. 0 when it is not sent: no idle voice, the queue has no room for
  // everything the note needs, or a key-up is still waiting to be sent.
  // An id is never 0.
  NoteId note_on(const PerformedNote& note);
  // Releases the open line when `id` is its current note; otherwise nothing.
  // A key-up the queue cannot take now is kept and sent first by the next
  // control call: it is never lost.
  void note_off(NoteId id);
  void set_wheel(float v);
  void set_pressure(float v);
  void set_volume(float v);
  // The constant of `pin` on `node`, in every voice: the voices that are
  // sounding hear it within one buffer, the others when they next start.
  // False when the player cannot follow it, and nothing changes: the voices
  // have no constant of that node and pin (a pin a wire feeds, a pin with no
  // number, a name that is not there, a node a MultiplexSource's template
  // reaches); the caller builds a new player then. A Set the queue cannot
  // take now is kept and sent by a later control call: a value is never
  // lost. Allocates nothing.
  bool set_value(std::string_view node, std::string_view pin, float v);
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
    enum class Kind : std::uint8_t { Start, Carry, Release, Fade, Set };
    Kind  kind{Kind::Start};
    int   voice{-1};
    float gain{1.0f};          // Start: the note's velocity
    int   bodySamples{0};      // Start: the note's length in samples
    bool  held{false};         // Start: the voice opens a held line
    Voice::Note note{};        // Carry: the next note (no curve)
    bool  hold{false};         // Carry: the next note holds
    float fromFreq{0.0f};      // Carry: the frequency glided from
    int   constant{-1};        // Set: the index into every voice's constants()
    float value{0.0f};         // Set: the constant's new number
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
    std::uint64_t startedAt{0};
    std::uint64_t releasedAt{0};   // when no key held it any more
    bool     released{false};      // no key holds it
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

  // The voice `v` of the instrument's pool: the player is the instrument's
  // other driver.
  Voice& voice(int v);

  // Construction: every voice lists the same constants in the same order.
  void check_constants_agree();
  // Control thread.
  void catch_up();
  int  constant_index(std::string_view node, std::string_view pin);
  Message set_message(int k) const;
  void send_unsent();
  void write_kept(Voice& v);
  Message release_message(int v) const;
  NoteId take_id();
  int  idle_voice() const;
  int  steal_victim() const;
  NoteId carry(const PerformedNote& note, const Voice::Note& n);
  NoteId start_fresh(const PerformedNote& note, const Voice::Note& n);
  // Audio thread.
  void apply(const Message& m) noexcept;
  Sounding* find(int v) noexcept;
  void release(Sounding& s, int lastNoteSamples) noexcept;
  void render(Sounding& s, float* mono, int frames) noexcept;
  void give_back() noexcept;

  // Set at construction. The instrument's voices pass between the threads
  // by hand-over; its played state is atomics; nothing else of it changes
  // while the player runs.
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
  int pendingRelease_{-1};   // the voice whose Release waits for room; -1 for none
  int lastStarted_{-1};
  NoteId nextId_{1};
  std::uint64_t nextOrder_{0};
  // By index into a voice's constants(), sized at construction: the latest
  // value set; whether one was set; whether its Set waits for room.
  std::vector<float> kept_;
  std::vector<std::uint8_t> isKept_;
  std::vector<std::uint8_t> unsent_;
  int unsentCount_{0};

  // Audio thread only.
  std::vector<Sounding> sounding_;   // capacity for every voice, reserved at construction
};

} // namespace mforce
