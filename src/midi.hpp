#pragma once

#include <midiusb.h>
#include <cstdint>

namespace midi
{
	enum class Event : std::uint_fast8_t
	{
		NoteOn,
		NoteOff,

		ControlUnknown,
		ControlModulation,
		ControlBreath,
		ControlFoot,
		
		PitchBend,
	};
	using EventCallbackFunc_t = void (*)(midi::Event event, std::int16_t data);

	bool begin(midi::EventCallbackFunc_t cbfunc = nullptr);
	bool parseEvent(const std::uint8_t * packet);


}
