/**
 * Simple example MIDI class
 * author: chegewara
 */
// Original example from: https://github.com/chegewara/EspTinyUSB/blob/master/examples/

#include "midi.hpp"

static MIDIusb s_midi;
static midi::EventCallbackFunc_t s_cbfunc = nullptr;

bool midi::begin(midi::EventCallbackFunc_t cbfunc)
{
	static char manufacturer[] = "Tallegg";
	static char product[]      = "Enceladus MIDI";

	s_midi.manufacturer(manufacturer);
	if (!s_midi.begin(product))
	{
		return false;
	}

	s_cbfunc = cbfunc;

	return true;
}

bool midi::parseEvent(const std::uint8_t * packet)
{
	bool hasEvent = true;
	Event ev;
	std::int16_t data;

	switch (packet[0] & 0x0F)
	{
	case 0x8:	// Note off
		ev = Event::NoteOff;
		data = std::int16_t(packet[2]);
		break;
	case 0x9:	// Note on
		ev = Event::NoteOn;
		data = std::int16_t(packet[2]);
		break;
	case 0xB:	// Control change
		// Determine the control type
		switch (packet[2])
		{
		case 0x01:
			ev = Event::ControlModulation;
			break;
		case 0x02:
			ev = Event::ControlBreath;
			break;
		case 0x04:
			ev = Event::ControlFoot;
			break;
		default:
			ev = Event::ControlUnknown;
		}
		data = std::int16_t(packet[3]);
		break;
	case 0xE:	// Pitch-bend
		ev = Event::PitchBend;
		data = (std::int16_t(packet[2]) | (std::int16_t(packet[3]) << 7)) - 0x2000;
		break;
	default:
		hasEvent = false;
	}

	if (hasEvent && (s_cbfunc != nullptr))
	{
		s_cbfunc(ev, data);
	}

	return hasEvent;
}

void tud_midi_rx_cb(std::uint8_t itf)
{
	std::uint8_t midipacket[4];
	if (tud_midi_packet_read(midipacket))
	{
		midi::parseEvent(midipacket);
		//Serial.printf("Read Midi data: 0x%08X\n", *(int*)midipacket);
	}
}