#include "stdafx.h"
#include "groovy_mister_pad_handler.h"
#include "Emu/Io/pad_config.h"

#include <algorithm>
#include <string>

groovy_mister_pad_handler::groovy_mister_pad_handler()
    : PadHandlerBase(pad_handler::groovy_mister)
{
	// Unique names for the config files and the pad settings dialog. These are
	// the MiSTer's own generic wire positions, so the source column here reads
	// one-for-one against the OSD's Controllers -> Button assignments page.
	// Renamed from the PS names ("Cross".."R3") — profiles saved before this
	// no longer resolve and go silently unbound, so hit Default in the pad
	// dialog once after updating.
	button_list =
	{
		{ gm_key::None,     "" },
		{ gm_key::Up,       "Up" },
		{ gm_key::Down,     "Down" },
		{ gm_key::Left,     "Left" },
		{ gm_key::Right,    "Right" },
		{ gm_key::B1,       "Button 1" },
		{ gm_key::B2,       "Button 2" },
		{ gm_key::B3,       "Button 3" },
		{ gm_key::B4,       "Button 4" },
		{ gm_key::B5,       "Button 5" },
		{ gm_key::B6,       "Button 6" },
		{ gm_key::B7,       "Button 7" },
		{ gm_key::B8,       "Button 8" },
		{ gm_key::B9,       "Button 9" },
		{ gm_key::B10,      "Button 10" },
		{ gm_key::B11,      "Button 11" },
		{ gm_key::B12,      "Button 12" },
		{ gm_key::LSXNeg,   "LS X-" },
		{ gm_key::LSXPos,   "LS X+" },
		{ gm_key::LSYNeg,   "LS Y-" },
		{ gm_key::LSYPos,   "LS Y+" },
		{ gm_key::RSXNeg,   "RS X-" },
		{ gm_key::RSXPos,   "RS X+" },
		{ gm_key::RSYNeg,   "RS Y-" },
		{ gm_key::RSYPos,   "RS Y+" },
	};

	init_configs();

	// Groovy analog axes are signed char (-128..127).
	thumb_max   = 127;
	trigger_min = 0;
	trigger_max = 255;

	b_has_config    = true;
	b_has_rumble    = true;  // inputs v2: rumble return channel over the inputs socket
	b_has_deadzones = true;
	b_has_battery   = false;
	b_has_motion    = false;
	b_has_orientation = false;

	m_name_string = "MiSTer Joy #";
	m_max_devices = 2;

	m_trigger_threshold = trigger_max / 2;
	m_thumb_threshold   = thumb_max / 2;
}

bool groovy_mister_pad_handler::Init()
{
	// Nothing to set up here: the shared gmw connection is owned by the
	// video path. Binding/polling happens lazily once it is live.
	return true;
}

std::vector<pad_list_entry> groovy_mister_pad_handler::list_devices()
{
	// Two fixed devices, always offered (like the keyboard handler). Whether
	// a MiSTer is actually streaming is reported via update_connection().
	std::vector<pad_list_entry> devices;
	devices.emplace_back(m_name_string + "1", false);
	devices.emplace_back(m_name_string + "2", false);
	return devices;
}

void groovy_mister_pad_handler::init_config(cfg_pad* cfg)
{
	if (!cfg) return;

	// Defaults apply the DualShock reading of the generic wire positions, which
	// is what the MiSTer's own auto-mapping produces for a standard pad (its
	// OSD documents the same: Button 1 = Cross, Button 4 = Triangle, ...).
	// Dpad direct, joy L/R analog -> left/right stick.
	cfg->ls_left.def  = ::at32(button_list, gm_key::LSXNeg);
	cfg->ls_down.def  = ::at32(button_list, gm_key::LSYNeg);
	cfg->ls_right.def = ::at32(button_list, gm_key::LSXPos);
	cfg->ls_up.def    = ::at32(button_list, gm_key::LSYPos);
	cfg->rs_left.def  = ::at32(button_list, gm_key::RSXNeg);
	cfg->rs_down.def  = ::at32(button_list, gm_key::RSYNeg);
	cfg->rs_right.def = ::at32(button_list, gm_key::RSXPos);
	cfg->rs_up.def    = ::at32(button_list, gm_key::RSYPos);

	cfg->start.def    = ::at32(button_list, gm_key::B8);
	cfg->select.def   = ::at32(button_list, gm_key::B7);
	cfg->ps.def       = ::at32(button_list, gm_key::None);

	cfg->cross.def    = ::at32(button_list, gm_key::B1);
	cfg->circle.def   = ::at32(button_list, gm_key::B2);
	cfg->square.def   = ::at32(button_list, gm_key::B3);
	cfg->triangle.def = ::at32(button_list, gm_key::B4);

	cfg->left.def     = ::at32(button_list, gm_key::Left);
	cfg->down.def     = ::at32(button_list, gm_key::Down);
	cfg->right.def    = ::at32(button_list, gm_key::Right);
	cfg->up.def       = ::at32(button_list, gm_key::Up);

	cfg->l1.def       = ::at32(button_list, gm_key::B5);
	cfg->r1.def       = ::at32(button_list, gm_key::B6);
	cfg->l2.def       = ::at32(button_list, gm_key::B9);
	cfg->r2.def       = ::at32(button_list, gm_key::B10);
	cfg->l3.def       = ::at32(button_list, gm_key::B11);
	cfg->r3.def       = ::at32(button_list, gm_key::B12);

	cfg->pressure_intensity_button.def = ::at32(button_list, gm_key::None);
	cfg->analog_limiter_button.def     = ::at32(button_list, gm_key::None);
	cfg->orientation_reset_button.def  = ::at32(button_list, gm_key::None);

	// Deadzones (Groovy axis range is small; keep conservative defaults).
	cfg->lstick_anti_deadzone.def = static_cast<u32>(0.13 * thumb_max);
	cfg->rstick_anti_deadzone.def = static_cast<u32>(0.13 * thumb_max);
	cfg->lstickdeadzone.def    = 0;
	cfg->rstickdeadzone.def    = 0;
	cfg->ltriggerthreshold.def = 0;
	cfg->rtriggerthreshold.def = 0;

	cfg->from_default();
}

std::shared_ptr<PadDevice> groovy_mister_pad_handler::get_device(const std::string& device)
{
	const usz pos = device.find(m_name_string);
	if (pos == umax)
		return nullptr;

	int n = 0;
	try
	{
		n = std::stoi(device.substr(pos + m_name_string.size()));
	}
	catch (...)
	{
		return nullptr;
	}

	if (n != 1 && n != 2)
		return nullptr;

	std::shared_ptr<gm_device> dev = std::make_shared<gm_device>();
	dev->joy_index = static_cast<u8>(n - 1);
	return dev;
}

bool groovy_mister_pad_handler::poll_shared(gm_device* dev)
{
	if (!dev)
		return false;

	if (!gmw_is_connected())
	{
		// Connection (owned by the video path) is down: report no input.
		// Input binding/subscribe is the video path's responsibility (it
		// sends it before CMD_INIT, the only point the core reads it).
		dev->connected = false;
		return false;
	}

	gmw_pollInputs();                 // non-blocking, drains pending datagrams
	gmw_getJoyInputs(&dev->state);    // copies the library's latest cached state

	dev->connected = true;
	dev->last_joy_frame = dev->state.joyFrame;
	return true;
}

PadHandlerBase::connection groovy_mister_pad_handler::update_connection(const std::shared_ptr<PadDevice>& device)
{
	gm_device* dev = static_cast<gm_device*>(device.get());
	if (!dev)
		return connection::disconnected;

	if (!poll_shared(dev))
		return connection::disconnected;

	return connection::connected;
}

std::unordered_map<u32, u16> groovy_mister_pad_handler::get_button_values(const std::shared_ptr<PadDevice>& device)
{
	std::unordered_map<u32, u16> values;

	gm_device* dev = static_cast<gm_device*>(device.get());
	if (!dev || !dev->connected)
		return values;

	const u32 map = (dev->joy_index == 0) ? dev->state.joy1 : dev->state.joy2;

	const auto set_btn = [&](gm_key k, u32 mask)
	{
		values[k] = (map & mask) ? 255 : 0;
	};

	set_btn(gm_key::Right, GMW_JOY_RIGHT);
	set_btn(gm_key::Left,  GMW_JOY_LEFT);
	set_btn(gm_key::Down,  GMW_JOY_DOWN);
	set_btn(gm_key::Up,    GMW_JOY_UP);
	set_btn(gm_key::B1,    GMW_JOY_B1);
	set_btn(gm_key::B2,    GMW_JOY_B2);
	set_btn(gm_key::B3,    GMW_JOY_B3);
	set_btn(gm_key::B4,    GMW_JOY_B4);
	set_btn(gm_key::B5,    GMW_JOY_B5);
	set_btn(gm_key::B6,    GMW_JOY_B6);
	set_btn(gm_key::B7,    GMW_JOY_B7);
	set_btn(gm_key::B8,    GMW_JOY_B8);
	set_btn(gm_key::B11,   GMW_JOY_B11);
	set_btn(gm_key::B12,   GMW_JOY_B12);

	// B9/B10 (L2/R2 by convention): prefer the v2 analog trigger (nonzero only
	// when the MiSTer OSD has Joysticks=Analog and the pad has analog
	// triggers); fall back to the digital bits 12/13, which arrive even on v1
	// 16-bit packets — so arcade sticks and older cores keep working unchanged.
	const u8 lt = (dev->joy_index == 0) ? dev->state.joy1LTAnalog : dev->state.joy2LTAnalog;
	const u8 rt = (dev->joy_index == 0) ? dev->state.joy1RTAnalog : dev->state.joy2RTAnalog;
	values[gm_key::B9]  = lt ? lt : ((map & GMW_JOY_B9) ? 255 : 0);
	values[gm_key::B10] = rt ? rt : ((map & GMW_JOY_B10) ? 255 : 0);

	// The MiSTer forwards RAW EVDEV axes, where negative Y is UP. Every other
	// RPCS3 handler treats Y+ as up (ls_up.def = LSYPos), so flip Y here rather
	// than inverting the default bindings — that keeps the dialog reading the
	// same way across handlers. The core's per-profile OSD toggle "Invert stick
	// Y" must stay Off, or the two cancel out. -128 is not representable when
	// negated, so it is clamped exactly as the core's own invy does.
	const auto flip_y = [](int v) { return v <= -127 ? 127 : -v; };

	const int lx = (dev->joy_index == 0) ? dev->state.joy1LXAnalog : dev->state.joy2LXAnalog;
	const int ly = flip_y((dev->joy_index == 0) ? dev->state.joy1LYAnalog : dev->state.joy2LYAnalog);
	const int rx = (dev->joy_index == 0) ? dev->state.joy1RXAnalog : dev->state.joy2RXAnalog;
	const int ry = flip_y((dev->joy_index == 0) ? dev->state.joy1RYAnalog : dev->state.joy2RYAnalog);

	// Split each signed axis into two symmetric 0..127 half-axes; thumb_max is
	// 127 so the base normalisation scales them. The clamp guards a rogue -128.
	const auto neg_half = [](int v) -> u16 { return v < 0 ? static_cast<u16>(std::min(-v, 127)) : 0; };
	const auto pos_half = [](int v) -> u16 { return v > 0 ? static_cast<u16>(std::min(v, 127)) : 0; };

	values[gm_key::LSXNeg] = neg_half(lx);
	values[gm_key::LSXPos] = pos_half(lx);
	values[gm_key::LSYNeg] = neg_half(ly);
	values[gm_key::LSYPos] = pos_half(ly);
	values[gm_key::RSXNeg] = neg_half(rx);
	values[gm_key::RSXPos] = pos_half(rx);
	values[gm_key::RSYNeg] = neg_half(ry);
	values[gm_key::RSYPos] = pos_half(ry);

	return values;
}

bool groovy_mister_pad_handler::get_is_left_stick(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	switch (keyCode)
	{
	case gm_key::LSXNeg:
	case gm_key::LSXPos:
	case gm_key::LSYNeg:
	case gm_key::LSYPos:
		return true;
	default:
		return false;
	}
}

bool groovy_mister_pad_handler::get_is_right_stick(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	switch (keyCode)
	{
	case gm_key::RSXNeg:
	case gm_key::RSXPos:
	case gm_key::RSYNeg:
	case gm_key::RSYPos:
		return true;
	default:
		return false;
	}
}

bool groovy_mister_pad_handler::get_is_left_trigger(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	return keyCode == gm_key::B9;
}

bool groovy_mister_pad_handler::get_is_right_trigger(const std::shared_ptr<PadDevice>& /*device*/, u32 keyCode)
{
	return keyCode == gm_key::B10;
}

void groovy_mister_pad_handler::apply_pad_data(const pad_ensemble& binding)
{
	const auto& device = binding.device;
	const auto& pad = binding.pad;

	gm_device* dev = static_cast<gm_device*>(device.get());
	if (!dev || !dev->config || !pad)
		return;

	// Rumble rides the inputs socket; only meaningful while the shared gmw
	// connection (owned by the video path) is live and bound.
	if (!dev->connected || !gmw_is_connected())
		return;

	const cfg_pad* cfg = dev->config;

	const u8 speed_large = cfg->get_large_motor_speed(pad->m_vibrate_motors);
	const u8 speed_small = cfg->get_small_motor_speed(pad->m_vibrate_motors);

	dev->new_output_data |= dev->large_motor != speed_large || dev->small_motor != speed_small;

	dev->large_motor = speed_large;
	dev->small_motor = speed_small;

	const auto now = steady_clock::now();
	const auto elapsed = now - dev->last_output;

	// Send on state change (20ms floor so per-frame effect ramps don't spam the
	// HPS) plus a periodic refresh: the 4-byte datagram is fire-and-forget UDP,
	// so the re-send heals a lost stop/update. The core repeats the last value
	// until replaced (0/0 stops) and force-stops motors on session close.
	if ((dev->new_output_data && elapsed > 20ms) || elapsed > min_output_interval)
	{
		gmw_send_rumble(dev->joy_index, speed_large, speed_small);
		dev->new_output_data = false;
		dev->last_output = now;
	}
}
