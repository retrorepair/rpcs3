#pragma once

#include "Emu/Io/PadHandler.h"

#include "groovymister_wrapper.h"

#include <unordered_map>

// Injects the controllers wired to a MiSTer (read + streamed by the
// Groovy_MiSTer core over UDP) into RPCS3's pad system, the same way
// RetroArch's mister_joypad driver does. Joysticks only (2 controllers);
// PS/2 keyboard/mouse is out of scope.
//
// Uses the standard PadHandlerBase get_button_values() model: the base
// process()/get_mapping()/convert_stick_values() does all combo + analog +
// deadzone work; we only supply device discovery + raw values.
//
// Threading: process() (=> update_connection/get_button_values) runs only on
// this handler's own pad thread. The Groovy input socket (UDP 32101) is
// separate from the video socket and the video sender thread never calls the
// input API, so polling here is race-free. The shared gmw connection is owned
// by the video path (VKGroovyMisterOutput); we only bind/poll when it is live
// (gmw_is_connected) and never create a second gmw instance.
class groovy_mister_pad_handler final : public PadHandlerBase
{
	// Inputs v2: the Groovy wire is GENERIC. Bits 4-15 are Button 1..12
	// (GMW_JOY_B1..B12) and carry no platform meaning of their own — Groovy
	// fronts many platforms, and per-device .map files on the MiSTer decide
	// which physical button lands on which position. The MiSTer OSD names
	// these positions "Button 1".."Button 12", so we do too; that keeps the
	// pad dialog readable against the OSD's Controllers -> Button assignments
	// page for every device type (DualShock, Xbox, arcade encoder).
	//
	// The DualShock reading of those positions (B1 = Cross, B2 = Circle, ...)
	// is one labeling convention, not the contract. It is applied in exactly
	// one place — init_config()'s default bindings — because it is what the
	// MiSTer's own auto-mapping produces for a standard pad.
	//
	// B9/B10 (L2/R2) carry a composite value: the v2 analog trigger (0..255)
	// when the MiSTer OSD has Joysticks=Analog, else the digital bit (bits
	// 12/13, which arrive even on v1 16-bit packets).
	enum gm_key
	{
		None = 0,

		Up,
		Down,
		Left,
		Right,
		B1,
		B2,
		B3,
		B4,
		B5,
		B6,
		B7,
		B8,
		B9,
		B10,
		B11,
		B12,

		LSXNeg,
		LSXPos,
		LSYNeg,
		LSYPos,
		RSXNeg,
		RSXPos,
		RSYNeg,
		RSYPos,
	};

	struct gm_device : public PadDevice
	{
		u8 joy_index = 0;            // 0 -> joy1, 1 -> joy2
		bool connected = false;
		u32 last_joy_frame = 0;
		gmw_fpgaJoyInputs state{};
	};

public:
	groovy_mister_pad_handler();

	bool Init() override;

	std::vector<pad_list_entry> list_devices() override;
	void init_config(cfg_pad* cfg) override;

private:
	// Input binding/subscribe is owned by the video path
	// (VKGroovyMisterOutput::init), which sends it BEFORE CMD_INIT — the
	// only point the Groovy core reads it (once, in setInit). This handler
	// only polls the shared cached state once the connection is live.
	bool poll_shared(gm_device* dev);

	std::shared_ptr<PadDevice> get_device(const std::string& device) override;
	bool get_is_left_stick(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	bool get_is_right_stick(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	bool get_is_left_trigger(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	bool get_is_right_trigger(const std::shared_ptr<PadDevice>& device, u32 keyCode) override;
	PadHandlerBase::connection update_connection(const std::shared_ptr<PadDevice>& device) override;
	std::unordered_map<u32, u16> get_button_values(const std::shared_ptr<PadDevice>& device) override;
	// Rumble return channel (inputs v2): forwards the emulated pad's vibration
	// to the physical pad on the MiSTer. Runs on the pad thread, which also
	// owns all other inputs-socket traffic (PollInputs) — race-free.
	void apply_pad_data(const pad_ensemble& binding) override;
};
