#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

/** Catch2 runs with our own main so that JUCE's message manager and singletons exist for the
    whole session: constructing an AudioProcessor without them trips leak detectors on shutdown.
*/
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    return Catch::Session().run (argc, argv);
}
