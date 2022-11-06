#include "srdac.hpp"

static bool s_init = false;
static std::int8_t s_input = -1, s_clk = -1, s_rclk = -1;

static SPIClass dacSPI;

void srdac::initDac(std::int8_t input, std::int8_t clk, std::int8_t rclk)
{
	pinMode(rclk,  OUTPUT);
	digitalWrite(rclk, 0);

	dacSPI.begin(clk, -1, input);
	dacSPI.setFrequency(DAC_SPI_FREQ);
	dacSPI.setBitOrder(LSBFIRST);

	s_input = input;
	s_clk   = clk;
	s_rclk  = rclk;

	s_init = true;
}

void srdac::write(std::uint16_t value)
{
	assert(s_init == true);

	digitalWrite(s_rclk, LOW);

	const uint8_t packet[2] = {
		std::uint8_t(value & 0xFF),
		std::uint8_t((value >> 8) & 0xFF)
	};
	dacSPI.writeBytes(packet, 2);

	digitalWrite(s_rclk, HIGH);
}
