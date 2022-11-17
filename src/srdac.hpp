#pragma once

#include <cstdint>
#include <Arduino.h>
#include <SPI.h>

#define DAC_SPI_FREQ 705600

namespace srdac
{
	void initDac(std::int8_t input, std::int8_t clk, std::int8_t rclk);
	void write(std::uint16_t value);

	constexpr std::uint16_t numWholeSteps = 96;
	constexpr std::uint16_t dacMaxStep    = 65535;
	constexpr std::uint16_t stepSize      = dacMaxStep / numWholeSteps;
	constexpr std::uint16_t maxStep       = stepSize * numWholeSteps;

	constexpr std::uint16_t noteToVal(std::uint8_t note)
	{
		return std::uint16_t(note) * stepSize;
	}
	constexpr std::uint16_t noteToVal(std::uint8_t note, std::int16_t pitch)
	{
		return std::uint16_t(
			std::int16_t(noteToVal(note)) +
			std::int16_t((std::int32_t(pitch) * std::int32_t(stepSize)) / 4096)
		);
	}
}
