#include <Controlino.h>
#include <LiquidCrystal.h>
#include <Midier.h>

#include <assert.h>

namespace arpeggino
{

namespace utils
{

struct Timer
{
    // control

    void start() // start (or restart)
    {
        _millis = millis();
    }

    void reset() // restart only if ticking
    {
        if (ticking())
        {
            start();
        }
    }

    void stop()
    {
        _millis = -1;
    }

    // query

    bool elapsed(unsigned ms) const // only if ticking
    {
        return ticking() && millis() - _millis >= ms;
    }

    bool ticking() const
    {
        return _millis != -1;
    }

private:
    unsigned long _millis = -1;
};

} // utils

namespace state
{

midier::Layers<8> layers; // the number of layers chosen will affect the global variable size
midier::Sequencer sequencer(layers);

} // state

namespace io
{

// here we declare all I/O controls with their corresponding pin numbers

controlino::Selector Selector(/* s0 = */ 6, /* s1 = */ 5, /* s2 = */ 4, /* s3 = */ 3);
controlino::Multiplexer Multiplexer(/* sig = */ 2, Selector);

controlino::Potentiometer BPM(A0, /* min = */ 20, /* max = */ 230); // we limit the value of BPM to [20,230]

// all configuration keys are behind the multiplexer
controlino::Key Note(Multiplexer, 7);
controlino::Key Mode(Multiplexer, 6);
controlino::Key Octave(Multiplexer, 5);
controlino::Key Perm(Multiplexer, 4);
controlino::Key Steps(Multiplexer, 3);
controlino::Key Rhythm(Multiplexer, 2);

// control buttons
controlino::Button Record(Multiplexer, 0);

struct LCD : LiquidCrystal
{
    LCD(uint8_t rs, uint8_t e, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) : LiquidCrystal(rs, e, d4, d5, d6, d7)
    {}

    template <typename T>
    char print(const T & arg)
    {
        return LiquidCrystal::print(arg);
    }

    template <typename T>
    char print(char col, char row, const T & arg)
    {
        setCursor(col, row);
        return print(arg);
    }

    template <typename T>
    char print(char col, char row, char max, const T & arg)
    {
        const auto written = print(col, row, arg);

        for (unsigned i = 0; i < max - written; ++i)
        {
            write(' '); // make sure the non-used characters are clear
        }

        return written;
    }
};

LCD lcd(/* rs = */ 7, /* e = */ 8,  /* d4 = */ 9, /* d5 = */ 10, /* d6 = */ 11, /* d7 = */ 12);

utils::Timer flashing;

} // io

namespace configurer
{

enum class Action
{
    None,

    Summary,
    Focus,
};

// a configurer is responsible for updating a single configuration
// parameter according to changes of an I/O control

struct Configurer
{
    Action(*check)();
    void(*update)();
};

Configurer BPM =
    {
        .check = []()
            {
                if (io::BPM.check() == controlino::Potentiometer::Event::Changed)
                {
                    return Action::Summary;
                }

                return Action::None;
            },
        .update = []()
            {
                state::sequencer.bpm = io::BPM.read();
            },
    };

Configurer Note =
    {
        .check = []()
            {
                if (io::Note.check() == controlino::Key::Event::Down)
                {
                    return Action::Summary;
                }

                return Action::None;
            },
        .update = []()
            {
                auto & config = state::sequencer.config; // a shortcut

                if (config.accidental() == midier::Accidental::Flat)
                {
                    config.accidental(midier::Accidental::Natural);
                }
                else if (config.accidental() == midier::Accidental::Natural)
                {
                    config.accidental(midier::Accidental::Sharp);
                }
                else if (config.accidental() == midier::Accidental::Sharp)
                {
                    config.accidental(midier::Accidental::Flat);

                    if      (config.note() == midier::Note::C) { config.note(midier::Note::D); }
                    else if (config.note() == midier::Note::D) { config.note(midier::Note::E); }
                    else if (config.note() == midier::Note::E) { config.note(midier::Note::F); }
                    else if (config.note() == midier::Note::F) { config.note(midier::Note::G); }
                    else if (config.note() == midier::Note::G) { config.note(midier::Note::A); }
                    else if (config.note() == midier::Note::A) { config.note(midier::Note::B); }
                    else if (config.note() == midier::Note::B) { config.note(midier::Note::C); }
                }
            },
    };

Configurer Mode =
    {
        .check = []()
            {
                if (io::Mode.check() == controlino::Key::Event::Down)
                {
                    return Action::Focus;
                }

                return Action::None;
            },
        .update = []()
            {
                const auto current = state::sequencer.config.mode();
                const auto next = (midier::Mode)(((unsigned)current + 1) % (unsigned)midier::Mode::Count);

                state::sequencer.config.mode(next);
            },
    };

Configurer Octave =
    {
        .check = []()
            {
                if (io::Octave.check() == controlino::Key::Event::Down)
                {
                    return Action::Summary;
                }

                return Action::None;
            },
        .update = []()
            {
                const auto current = state::sequencer.config.octave();
                const auto next = (current % 7) + 1;

                state::sequencer.config.octave(next);
            },
    };

Configurer Perm =
    {
        .check = []()
            {
                if (io::Perm.check() == controlino::Key::Event::Down)
                {
                    return Action::Focus;
                }

                return Action::None;
            },
        .update = []()
            {
                const auto current = state::sequencer.config.perm();
                const auto next = (current + 1) % midier::style::count(state::sequencer.config.steps());

                state::sequencer.config.perm(next);
            },
    };

Configurer Steps =
    {
        .check = []()
            {
                if (io::Steps.check() == controlino::Key::Event::Down)
                {
                    return Action::Focus;
                }

                return Action::None;
            },
        .update = []()
            {
                auto & config = state::sequencer.config; // a shortcut

                if (config.looped() == false) // we set to loop if currently not looping
                {
                    config.looped(true);
                }
                else
                {
                    unsigned steps = config.steps() + 1;

                    if (steps > 6)
                    {
                        steps = 3;
                    }

                    config.steps(steps);
                    config.perm(0); // reset the permutation
                    config.looped(false); // set as non looping
                }
            },
    };

Configurer Rhythm =
    {
        .check = []()
            {
                if (io::Rhythm.check() == controlino::Key::Event::Down)
                {
                    return Action::Focus;
                }

                return Action::None;
            },
        .update = []()
            {
                const auto current = state::sequencer.config.rhythm();
                const auto next = (midier::Rhythm)(((unsigned)current + 1) % (unsigned)midier::Rhythm::Count);

                state::sequencer.config.rhythm(next);
            }
    };

} // configurer

namespace viewer
{

enum class What
{
    Title,
    Data,
};

enum class How
{
    Summary,
    Focus,
};

using Viewer = void(*)(What, How);

struct : utils::Timer
{
    // query
    bool operator==(Viewer other) const { return _viewer == other; }
    bool operator!=(Viewer other) const { return _viewer != other; }

    // assignment
    void operator=(Viewer other) { _viewer = other; }

    // access
    void print(What what, How how) { _viewer(what, how); }

private:
    Viewer _viewer = nullptr;
} focused;

void BPM(What what, How how)
{
    assert(how == How::Summary);

    if (what == What::Title)
    {
        io::lcd.print(13, 1, "bpm");
    }

    if (what == What::Data)
    {
        io::lcd.print(9, 1, 3, state::sequencer.bpm);
    }
}

void Note(What what, How how)
{
    assert(how == How::Summary);

    if (what == What::Data)
    {
        io::lcd.setCursor(0, 0);

        const auto & config = state::sequencer.config; // a shortcut

        if      (config.note() == midier::Note::A) { io::lcd.print('A'); }
        else if (config.note() == midier::Note::B) { io::lcd.print('B'); }
        else if (config.note() == midier::Note::C) { io::lcd.print('C'); }
        else if (config.note() == midier::Note::D) { io::lcd.print('D'); }
        else if (config.note() == midier::Note::E) { io::lcd.print('E'); }
        else if (config.note() == midier::Note::F) { io::lcd.print('F'); }
        else if (config.note() == midier::Note::G) { io::lcd.print('G'); }

        if      (config.accidental() == midier::Accidental::Flat)    { io::lcd.print('b'); }
        else if (config.accidental() == midier::Accidental::Natural) { io::lcd.print(' '); }
        else if (config.accidental() == midier::Accidental::Sharp)   { io::lcd.print('#'); }
    }
}

void Mode(What what, How how)
{
    if (what == What::Data)
    {
        midier::mode::Name name;
        midier::mode::name(state::sequencer.config.mode(), /* out */ name);

        if (how == How::Summary)
        {
            name[3] = '\0'; // trim the full name into a 3-letter shortcut
            io::lcd.print(0, 1, name);
        }
        else if (how == How::Focus)
        {
            io::lcd.print(0, 1, sizeof(name), name);
        }
    }
    else if (what == What::Title && how == How::Focus)
    {
        io::lcd.print(0, 0, "Mode: ");
    }
}

void Octave(What what, How how)
{
    assert(how == How::Summary);

    if (what == What::Title)
    {
        io::lcd.print(3, 0, 'O');
    }
    else if (what == What::Data)
    {
        io::lcd.print(4, 0, state::sequencer.config.octave());
    }
}

void Style(What what, How how)
{
    if (how == How::Summary)
    {
        if (what == What::Title)
        {
            io::lcd.print(6, 0, 'S');
        }
        else if (what == What::Data)
        {
            const auto & config = state::sequencer.config; // a shortcut

            io::lcd.print(7, 0, config.steps());
            io::lcd.print(8, 0, config.looped() ? '+' : '-');
            io::lcd.print(9, 0, 3, config.perm() + 1);
        }
    }
    else if (how == How::Focus)
    {
        if (what == What::Title)
        {
            io::lcd.print(0, 0, "Style: ");
        }
        else if (what == What::Data)
        {
            const auto & config = state::sequencer.config; // a shortcut

            io::lcd.print(7, 0, config.steps());
            io::lcd.print(8, 0, config.looped() ? '+' : '-');
            io::lcd.print(9, 0, 3, config.perm() + 1);

            midier::style::Description desc;
            midier::style::description(config.steps(), config.perm(), /* out */ desc);
            io::lcd.print(0, 1, 16, desc); // all columns in the LCD

            if (config.looped())
            {
                io::lcd.setCursor(strlen(desc) + 1, 1);

                for (unsigned i = 0; i < 3; ++i)
                {
                    io::lcd.print('.');
                }
            }
        }
    }
}

void Rhythm(What what, How how)
{
    if (how == How::Summary)
    {
        if (what == What::Title)
        {
            io::lcd.print(4, 1, 'R');
        }
        else if (what == What::Data)
        {
            io::lcd.print(5, 1, 2, (unsigned)state::sequencer.config.rhythm() + 1);
        }
    }
    else if (how == How::Focus)
    {
        if (what == What::Title)
        {
            io::lcd.print(0, 0, "Rhythm #");
        }
        else if (what == What::Data)
        {
            io::lcd.print(8, 0, 2, (unsigned)state::sequencer.config.rhythm() + 1);

            midier::rhythm::Description desc;
            midier::rhythm::description(state::sequencer.config.rhythm(), /* out */ desc);
            io::lcd.print(0, 1, desc);
        }
    }
}

} // viewer

namespace component
{

struct Component
{
    configurer::Configurer configurer;
    viewer::Viewer viewer;
};

Component All[] =
    {
        { configurer::BPM, viewer::BPM },
        { configurer::Note, viewer::Note },
        { configurer::Mode, viewer::Mode },
        { configurer::Octave, viewer::Octave },
        { configurer::Perm, viewer::Style },
        { configurer::Steps, viewer::Style },
        { configurer::Rhythm, viewer::Rhythm },
    };

} // component

namespace control
{

void flash()
{
    if (io::flashing.ticking())
    {
        return; // already flashing
    }

    digitalWrite(13, HIGH);
    io::flashing.start();
}

namespace view
{

void summary(viewer::Viewer viewer = nullptr) // 'nullptr' means all components
{
    if (viewer::focused != nullptr) // some viewer is currently in focus
    {
        viewer::focused.stop(); // stop the timer
        viewer::focused = nullptr; // mark as there's no viewer currently in focus
        io::lcd.clear(); // clear the screen entirely
        viewer = nullptr; // mark to print all titles and values
    }

    if (viewer == nullptr)
    {
        for (const auto & component : component::All)
        {
            component.viewer(viewer::What::Title, viewer::How::Summary);
            component.viewer(viewer::What::Data, viewer::How::Summary);
        }
    }
    else
    {
        viewer(viewer::What::Data, viewer::How::Summary);
    }
}

void focus(viewer::Viewer viewer)
{
    if (viewer::focused != viewer) // either in summary mode or another viewer is currently in focus
    {
        io::lcd.clear(); // clear the screen entirely
        viewer::focused = viewer; // mark this viewer as the one being in focus
        viewer::focused.print(viewer::What::Title, viewer::How::Focus); // print the title (only if just became the one in focus)
    }

    viewer::focused.print(viewer::What::Data, viewer::How::Focus); // print the data anyways
    viewer::focused.start(); // start the timer or restart it if ticking already
}

void bar(midier::Sequencer::Bar bar)
{
    io::lcd.setCursor(14, 0);

    char written = 0;

    if (bar != midier::Sequencer::Bar::None)
    {
        written = io::lcd.print((unsigned)bar);
    }

    while (written++ < 2)
    {
        io::lcd.write(' ');
    }
}

} // view

} // control

namespace handle
{

void flashing()
{
    if (io::flashing.elapsed(70))
    {
        digitalWrite(13, LOW);
        io::flashing.stop();
    }
}

void recording()
{
    static bool __recording = false;

    const auto recording = state::sequencer.recording(); // is recording at the moment?

    if (__recording != recording)
    {
        digitalWrite(A1, recording ? HIGH : LOW);
        __recording = recording;
    }
}

void focus()
{
    if (viewer::focused.elapsed(3200))
    {
        control::view::summary(); // go back to summary view
    }
}

void components()
{
    // components will update the configuration on I/O events

    for (const auto & component : component::All)
    {
        const auto action = component.configurer.check();

        if (action == configurer::Action::None)
        {
            continue; // nothing to do
        }

        // update the configuration only if in summary mode or if this configurer is in focus

        if ((action == configurer::Action::Summary && viewer::focused == nullptr) ||
            (action == configurer::Action::Focus && viewer::focused == component.viewer))
        {
            component.configurer.update();
        }

        if (action == configurer::Action::Summary)
        {
            control::view::summary(component.viewer);
        }
        else if (action == configurer::Action::Focus)
        {
            control::view::focus(component.viewer);
        }
    }
}

void keys()
{
    // we extend `controlino::Key` so we could hold a Midier handle with every key
    struct Key : controlino::Key
    {
        Key(char pin) : controlino::Key(io::Multiplexer, pin) // keys are behind the multiplexer
        {}

        midier::Sequencer::Handle h;
    };

    static Key __keys[] = { 15, 14, 13, 12, 11, 10, 9, 8 }; // channel numbers of the multiplexer

    for (auto i = 0; i < sizeof(__keys) / sizeof(Key); ++i)
    {
        auto & key = __keys[i];

        const auto event = key.check();

        if (event == Key::Event::None)
        {
            continue; // nothing has changed
        }

        if (event == Key::Event::Down) // a key was pressed
        {
            key.h = state::sequencer.start(i + 1); // start playing an arpeggio of the respective scale degree
        }
        else if (event == Key::Event::Up) // a key was released
        {
            state::sequencer.stop(key.h); // stop playing the arpeggio
        }
    }
}

void record()
{
    const auto event = io::Record.check();

    if (event == controlino::Button::Event::Click)
    {
        state::sequencer.record();
    }
    else if (event == controlino::Button::Event::Press)
    {
        state::sequencer.revoke(); // revoke the last recorded layer
    }
    else if (event == controlino::Button::Event::ClickPress)
    {
        state::sequencer.wander();
    }
}

void click()
{
    // actually click Midier for it to play the MIDI notes
    const auto bar = state::sequencer.click(midier::Sequencer::Run::Async);

    if (bar != midier::Sequencer::Bar::Same)
    {
        control::flash();

        if (viewer::focused == nullptr)
        {
            control::view::bar(bar);
        }
    }
}

} // handle

extern "C" void setup()
{
    // initialize the Arduino "Serial" module and set the baud rate
    // to the same value you are using in your software.
    // if connected physically using a MIDI 5-DIN connection, use 31250.
    Serial.begin(9600);

    // initialize the LEDs
    pinMode(13, OUTPUT);
    pinMode(A1, OUTPUT);

    // initialize the LCD
    io::lcd.begin(16, 2);

    // print the initial configuration
    control::view::summary();
}

extern "C" void loop()
{
    handle::flashing();
    handle::recording();
    handle::focus();
    handle::components();
    handle::keys();
    handle::record();
    handle::click();
}

} // arpeggino
