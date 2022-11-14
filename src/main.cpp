/**
 * Simple example MIDI class
 * author: chegewara
 */
// Original example from: https://github.com/chegewara/EspTinyUSB/blob/master/examples/


#include <Arduino.h>
#include <LiquidCrystal.h>
#include <freertos/semphr.h>
#include <list>
#include "srdac.hpp"
#include "midi.hpp"

#define ENC_1   11
#define ENC_2   12
#define ENC_BTN 13

#define PWM_OUT 18

#define SR_SER  16
#define SR_CLK  15
#define SR_RCLK 14

#define LCD_X 20
#define LCD_Y 4

#define LCD_RS 7
#define LCD_RW 6
#define LCD_E  5
#define LCD_D4 4
#define LCD_D5 3
#define LCD_D6 2
#define LCD_D7 1

#define LCD_BLIGHT 10

static LiquidCrystal lcd(LCD_RS, LCD_RW, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

enum encDirection
{
	left,
	right
};

volatile bool encDirection = false, encUsed = false, encBtnDown = false, encBtnUsed = false;
SemaphoreHandle_t xSemaphoreEnc = nullptr, xSemaphoreBtn = nullptr;

void IRAM_ATTR encoderISR()
{
	static BaseType_t xHigherPriorityTaskWoken;
	xHigherPriorityTaskWoken = pdFALSE;

	if (digitalRead(ENC_1) != digitalRead(ENC_2))
	{
		// Keerati paremale
		encDirection = right;
		encUsed = true;
	}
	else
	{
		// Keerati vasakule
		encDirection = left;
		encUsed = true;
	}

	xSemaphoreGiveFromISR(xSemaphoreEnc, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void IRAM_ATTR encoderBtnISR()
{
	static BaseType_t xHigherPriorityTaskWoken;	
	xHigherPriorityTaskWoken = pdFALSE;

	if (!digitalRead(ENC_BTN))
	{
		encBtnDown = true;
		encBtnUsed = true;
	}
	else
	{
		encBtnDown = false;
		encBtnUsed = true;
	}

	
	xSemaphoreGiveFromISR(xSemaphoreBtn, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

std::string notestr(std::int16_t note)
{
	return { "CCDDEFFGGAAB"[char(note % 12)], " # #  # # # "[char(note % 12)], char(note / 12) + '0' };
}

namespace disp
{
	constexpr std::uint8_t numDisplays = 3;
	std::int8_t selidx = 0;
	bool isSelected = false;
	constexpr char selchar[4] = { ' ', '>', ' ', '\x7E' };

	std::uint8_t lastnote;
	std::int16_t lastbend;
	std::uint8_t lastmod;
}

void dispout(std::uint8_t note, std::int16_t bend, std::uint8_t mod)
{
	disp::lastnote = note;
	disp::lastbend = bend;
	disp::lastmod  = mod;

	//ledcWrite(0, duty);
	char str[LCD_X];

	sprintf(str, "%cNote: %s",    disp::selchar[(disp::selidx == 0) | (disp::isSelected << 1)], notestr(note).c_str());
	lcd.setCursor(0, 1);
	lcd.write(str);
	
	const auto bendvalue = float(bend) * (2.f * (1.f / 8192.f));
	sprintf(str, "%cBend: %c%.2f ", disp::selchar[(disp::selidx == 1) | (disp::isSelected << 1)], bendvalue < 0.0f ? '-' : '+', fabsf(bendvalue));
	lcd.setCursor(0, 2);
	lcd.write(str);
	
	sprintf(str, "%cMod: %hu  ",  disp::selchar[(disp::selidx == 2) | (disp::isSelected << 1)], std::uint16_t(mod));
	lcd.setCursor(0, 3);
	lcd.write(str);

}

void midicallback(midi::Event event, std::int16_t data)
{
	static std::list<std::uint8_t> notes;
	static std::list<std::uint8_t>::iterator noteIters[127];
	static bool noteExist[127] = { 0 };
	static std::int16_t pitchbend = 0;
	static std::uint8_t mod = 0;

	switch (event)
	{
	case midi::Event::NoteOn:
	{
		Serial.printf("Note %s on\n", notestr(data).c_str());
		const std::uint8_t note = std::uint8_t(data);
		
		if (noteExist[note])
		{
			Serial.println("Note already pressed!");
		}
		
		notes.push_back(note);
		noteIters[note] = std::prev(notes.end());
		noteExist[note] = true;
		
		srdac::write(srdac::noteToVal(note, pitchbend));
		dispout(note, pitchbend, mod);
		
		break;
	}
	case midi::Event::NoteOff:
	{
		Serial.printf("Note %s off\n", notestr(data).c_str());
		const std::uint8_t note = std::uint8_t(data);

		if (!noteExist[note])
		{
			Serial.println("Note already released!");
			break;
		}

		notes.erase(noteIters[note]);
		noteExist[note] = false;
		
		if (!notes.empty())
		{
			srdac::write(srdac::noteToVal(notes.back(), pitchbend));
			dispout(notes.back(), pitchbend, mod);
		}
		
		break;
	}
	case midi::Event::PitchBend:
		Serial.printf("Pitch bend: %hd -> %.2f semitones\n", data, float(data) * (2.f * (1.f / 8192.f)));
		
		if (notes.empty())
		{
			Serial.println("No notes pressed ATM!");
		}
		
		{
			const auto note = (notes.empty()) ? 0 : notes.back();
			pitchbend = data;

			srdac::write(srdac::noteToVal(note, pitchbend));
			dispout(note, pitchbend, mod);
		}

		break;
	case midi::Event::ControlModulation:
		mod = std::uint8_t(data);
		Serial.printf("Modulation value: %hd\n", mod);
		dispout(notes.empty() ? 0 : notes.back(), pitchbend, mod);
		break;
	}
}

void setup()
{
	Serial.begin(115200);
	// Init semaphore
	xSemaphoreEnc = xSemaphoreCreateBinary();
	xSemaphoreBtn = xSemaphoreCreateBinary();

	// Add encoder isr
	attachInterrupt(ENC_1,   &encoderISR,    CHANGE);
	attachInterrupt(ENC_BTN, &encoderBtnISR, CHANGE);

	// Initialize PWM
	ledcAttachPin(PWM_OUT, 0);
	ledcSetup(0, 78000, 9);
	ledcWrite(0, 0);

	srdac::initDac(SR_SER, SR_CLK, SR_RCLK);

	if (!midi::begin(&midicallback))
	{
		Serial.println("Failed to initialize USB-MIDI!");
		return;
	}

	Serial.println("USB-MIDI intialized!");

	// Set up LCD screen
	lcd.begin(LCD_X, LCD_Y);
	pinMode(LCD_BLIGHT, OUTPUT);
	digitalWrite(LCD_BLIGHT, 1);

	Serial.println("Everything initialized!");
	lcd.setCursor(0, 0);
	lcd.write("  Enceladus Synth  ");
	dispout(0, 0, 0);
}

constexpr std::uint16_t dutylimit = 96;
std::uint16_t duty = 0;

unsigned long zerotimertime = 0;
bool iszerotimer = false;
constexpr unsigned long zerotimerBomb = 1500;

void setzerotimer()
{
	zerotimertime = millis() + zerotimerBomb;
	iszerotimer = true;
}

template<typename T>
constexpr T clamp(T value, T min, T max)
{
	return (value < min) ? min : ((value > max) ? max : value);
}

void loop()
{
	while (1)
	{
		if (iszerotimer && (millis() > zerotimertime))
		{
			disp::lastbend = 0;
			disp::lastmod  = 0;
			srdac::write(srdac::noteToVal(disp::lastnote, disp::lastbend));
			dispout(disp::lastnote, disp::lastbend, disp::lastmod);
			iszerotimer = false;
		}

		if (xSemaphoreTake(xSemaphoreEnc, 10) == pdTRUE)
		{
			encUsed = false;

			const std::int16_t delta = (encDirection == encDirection::left) ? -1 : +1;
			if (disp::isSelected)
			{
				switch (disp::selidx)
				{
				case 0:
				{	
					const auto tempnote = std::int16_t(disp::lastnote) + delta;
					disp::lastnote = std::uint16_t(clamp(tempnote, 0, 96));
					break;
				}
				case 1:
				{
					const auto tempbend = disp::lastbend + 410 * delta;
					disp::lastbend = clamp(tempbend, -8192, 8191);
					setzerotimer();
					break;
				}
				case 2:
				{
					const auto tempmod = std::int16_t(disp::lastmod) + 10 * delta;
					disp::lastmod = std::uint8_t(clamp(tempmod, 0, 127));
					setzerotimer();
					break;
				}
				default:
					Serial.println("Unknown display index!");
				}
				
			}
			else
			{
				const auto newidx = disp::selidx + delta;
				disp::selidx = (newidx < 0) ? (disp::numDisplays - 1) : ((newidx >= disp::numDisplays) ? 0 : newidx);
			}
			srdac::write(srdac::noteToVal(disp::lastnote, disp::lastbend));
			dispout(disp::lastnote, disp::lastbend, disp::lastmod);
		}
		else if (xSemaphoreTake(xSemaphoreBtn, 10) == pdTRUE)
		{
			encBtnUsed = false;

			disp::isSelected ^= encBtnDown;
			dispout(disp::lastnote, disp::lastbend, disp::lastmod);
		}
		else
		{
			if (Serial.available())
			{
				static String str;
				bool clearstr = false;
				while (Serial.available())
				{
					const char ch = Serial.read();
					Serial.print(ch);
					if ((ch == '\n') || (ch == '\r'))
					{
						clearstr = true;
						break;
					}
					str += ch;
				}

				if (clearstr)
				{
					Serial.flush();

					const auto cmd = str.substring(0, 3);
					str.clear();

					duty = atoi(cmd.c_str());
					duty = (duty > dutylimit) ? 0 : duty;
					srdac::write(duty);
					Serial.printf("Duty cycle: %hu\n", duty);
				}
			}
		}
	}
}
