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

#define PWM1_OUT 33
#define PWM2_OUT 34
#define PWM3_OUT 35
#define PWM4_OUT 36

#define GATE_CHANNEL  1
#define GATE_LEVEL   65

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
	return { "CCDDEFFGGAAB"[char(note % 12)], " # #  # # # "[char(note % 12)], char((note / 12) + '0') };
}

namespace disp
{
	constexpr std::uint8_t numDisplays = 6;
	std::int8_t selidx = 0;
	bool isSelected = false;
	constexpr char selchar[4] = { ' ', '>', ' ', '\x7E' };

	std::uint8_t lastnote;
	std::int16_t lastbend;
	std::uint8_t lastmod, lastbr, lastfoot;
	volatile bool packetincoming = false;

	bool gate = false;
}

void dispout(
	std::uint8_t note, std::int16_t bend, std::uint8_t mod,
	std::uint8_t breath, std::uint8_t foot
)
{
	disp::lastnote = note;
	disp::lastbend = bend;
	disp::lastmod  = mod;
	disp::lastbr   = breath;
	disp::lastfoot = foot;

	//ledcWrite(0, duty);
	char str[LCD_X];

	sprintf(str, "%cNote: %s", disp::selchar[(disp::selidx == 0) | (disp::isSelected << 1)], notestr(note).c_str());
	lcd.setCursor(0, 0);
	lcd.write(str);
	
	const auto bendvalue = float(bend) * (2.f * (1.f / 8192.f));
	sprintf(str, "%cBend:%c%.2f ", disp::selchar[(disp::selidx == 1) | (disp::isSelected << 1)], bendvalue < 0.0f ? '-' : bendvalue > 0.0f ? '+' : ' ', fabsf(bendvalue));
	lcd.setCursor(0, 1);
	lcd.write(str);
	
	sprintf(str, "%cMOD:%3hu", disp::selchar[(disp::selidx == 2) | (disp::isSelected << 1)], std::uint16_t(mod));
	lcd.setCursor(0, 2);
	lcd.write(str);

	sprintf(str, "%cVCF:%3hu", disp::selchar[(disp::selidx == 3) | (disp::isSelected << 1)], std::uint16_t(breath));
	lcd.setCursor(0, 3);
	lcd.write(str);

	sprintf(str, "%cLFO1:%3hu", disp::selchar[(disp::selidx == 4) | (disp::isSelected << 1)], std::uint16_t(foot));
	lcd.setCursor(LCD_X / 2 + 1, 2);
	lcd.write(str);

	sprintf(str, "%cGATE:%3hu", disp::selchar[(disp::selidx == 5) | (disp::isSelected << 1)], std::uint16_t(disp::gate));
	lcd.setCursor(LCD_X / 2 + 1, 3);
	lcd.write(str);
}

unsigned long miditimertime = 0;
constexpr unsigned long miditimerBomb = 1000;

void setmiditext(bool set = true)
{
	lcd.setCursor(LCD_X - 4, 0);
	lcd.write(set ? "MIDI" : "    ");
}
void resetmiditexttimer()
{
	miditimertime = millis() + miditimerBomb;
	disp::packetincoming = true;
	setmiditext();
}

void midicallback(midi::Event event, std::int16_t data)
{
	static std::list<std::uint8_t> notes;
	static std::list<std::uint8_t>::iterator noteIters[127];
	static bool noteExist[127] = { 0 };
	static std::int16_t pitchbend = 0;
	static std::uint8_t mod = 0, breath = 0, foot = 0;

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
		ledcWrite(GATE_CHANNEL, GATE_LEVEL);
		disp::gate = true;
		dispout(note, pitchbend, mod, breath, foot);
		
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
			ledcWrite(GATE_CHANNEL, GATE_LEVEL);
			disp::gate = true;
			dispout(notes.back(), pitchbend, mod, breath, foot);
		}
		else
		{
			ledcWrite(GATE_CHANNEL, 0);
			disp::gate = false;
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
			dispout(note, pitchbend, mod, breath, foot);
		}

		break;
	default:
		switch (event)
		{
		case midi::Event::ControlModulation:
			mod = std::uint8_t(data);
			Serial.printf("Modulation value: %hd\n", mod);
			ledcWrite(0, mod);
			break;
		case midi::Event::ControlBreath:
			breath = std::uint8_t(data);
			ledcWrite(2, breath);
			Serial.printf("Breath value: %hd\n", breath);
			break;
		case midi::Event::ControlFoot:
			foot = std::uint8_t(data);
			ledcWrite(3, foot);
			Serial.printf("Foot value: %hd\n", foot);
			break;
		}
		dispout(notes.empty() ? 0 : notes.back(), pitchbend, mod, breath, foot);
	}

	resetmiditexttimer();
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
	ledcAttachPin(PWM1_OUT, 0);
	ledcSetup(0, 78000, 7);
	ledcWrite(0, 0);
	ledcAttachPin(PWM2_OUT, 1);
	ledcSetup(1, 78000, 7);
	ledcWrite(1, 0);
	ledcAttachPin(PWM3_OUT, 2);
	ledcSetup(2, 78000, 7);
	ledcWrite(2, 0);
	ledcAttachPin(PWM4_OUT, 3);
	ledcSetup(3, 78000, 7);
	ledcWrite(3, 0);

	srdac::initDac(SR_SER, SR_CLK, SR_RCLK);
	srdac::write(0);

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
	lcd.write("    Enceladus 080   ");
	lcd.setCursor(0, 1);
	lcd.write("     ...DODGY...    ");
	lcd.setCursor(0, 2);
	lcd.write("     Version 1.0    ");

	delay(3000);
	lcd.clear();

	dispout(0, 0, 0, 0, 0);
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

void scroll1bit(bool & bit1, const std::int16_t delta)
{
	const auto tempmod = std::int16_t(bit1) + delta;
	bit1 = bool(clamp(tempmod, 0, 1));
	setzerotimer();
}
void scroll7bit(std::uint8_t & bit7, const std::int16_t delta)
{
	const auto tempmod = std::int16_t(bit7) + 5 * delta;
	bit7 = std::uint8_t(clamp(tempmod, 0, 127));
	setzerotimer();
}

void loop()
{
	while (1)
	{
		if (iszerotimer && (millis() > zerotimertime))
		{
			disp::lastbend = 0;
			disp::lastmod  = 0;
			//disp::lastbr   = 0;
			//disp::lastfoot = 0;
			srdac::write(srdac::noteToVal(disp::lastnote, disp::lastbend));
			ledcWrite(0, disp::lastmod);
			//ledcWrite(2, disp::lastbr);
			//ledcWrite(3, disp::lastfoot);
			dispout(disp::lastnote, disp::lastbend, disp::lastmod, disp::lastbr, disp::lastfoot);
			iszerotimer = false;
		}
		if (disp::packetincoming && (millis() > miditimertime))
		{
			setmiditext(false);
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
					scroll7bit(disp::lastmod, delta);
					break;
				case 3:
					scroll7bit(disp::lastbr, delta);
					break;
				case 4:
					scroll7bit(disp::lastfoot, delta);
					break;
				case 5:
					scroll1bit(disp::gate, delta);
					break;
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
			ledcWrite(0, disp::lastmod);
			ledcWrite(GATE_CHANNEL, GATE_LEVEL);
			disp::gate = true;
			dispout(disp::lastnote, disp::lastbend, disp::lastmod, disp::lastbr, disp::lastfoot);
		}
		else if (xSemaphoreTake(xSemaphoreBtn, 10) == pdTRUE)
		{
			encBtnUsed = false;

			disp::isSelected ^= encBtnDown;
			dispout(disp::lastnote, disp::lastbend, disp::lastmod, disp::lastbr, disp::lastfoot);
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
