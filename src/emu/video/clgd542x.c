/*

	Cirrus Logic GD542x/3x video chipsets

*/

#include "clgd542x.h"


#define CRTC_PORT_ADDR ((vga.miscellaneous_output&1)?0x3d0:0x3b0)

//#define TEXT_LINES (LINES_HELPER)
#define LINES (vga.crtc.vert_disp_end+1)
#define TEXT_LINES (vga.crtc.vert_disp_end+1)

#define GRAPHIC_MODE (vga.gc.alpha_dis) /* else text mode */

#define EGA_COLUMNS (vga.crtc.horz_disp_end+1)
#define EGA_START_ADDRESS (vga.crtc.start_addr)
#define EGA_LINE_LENGTH (vga.crtc.offset<<1)

#define VGA_COLUMNS (vga.crtc.horz_disp_end+1)
#define VGA_START_ADDRESS (vga.crtc.start_addr)
#define VGA_LINE_LENGTH (vga.crtc.offset<<3)

#define IBM8514_LINE_LENGTH (m_vga->offset())

#define CHAR_WIDTH ((vga.sequencer.data[1]&1)?8:9)

#define TEXT_COLUMNS (vga.crtc.horz_disp_end+1)
#define TEXT_START_ADDRESS (vga.crtc.start_addr<<3)
#define TEXT_LINE_LENGTH (vga.crtc.offset<<1)

#define TEXT_COPY_9COLUMN(ch) (((ch & 0xe0) == 0xc0)&&(vga.attribute.data[0x10]&4))

const device_type CIRRUS_GD5428 = &device_creator<cirrus_gd5428_device>;
const device_type CIRRUS_GD5430 = &device_creator<cirrus_gd5430_device>;


cirrus_gd5428_device::cirrus_gd5428_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: svga_device(mconfig, CIRRUS_GD5428, "Cirrus Logic GD5428", tag, owner, clock, "clgd5428", __FILE__)
{
}

cirrus_gd5428_device::cirrus_gd5428_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source)
	: svga_device(mconfig, type, name, tag, owner, clock, shortname, source)
{
}

cirrus_gd5430_device::cirrus_gd5430_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: cirrus_gd5428_device(mconfig, CIRRUS_GD5430, "Cirrus Logic GD5430", tag, owner, clock, "clgd5430", __FILE__)
{
}

MACHINE_CONFIG_FRAGMENT( pcvideo_cirrus_gd5428 )
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_RAW_PARAMS(XTAL_25_1748MHz,900,0,640,526,0,480)
	MCFG_SCREEN_UPDATE_DEVICE("vga", cirrus_gd5428_device, screen_update)

	MCFG_PALETTE_ADD("palette", 0x100)
	MCFG_DEVICE_ADD("vga", CIRRUS_GD5428, 0)
MACHINE_CONFIG_END

MACHINE_CONFIG_FRAGMENT( pcvideo_cirrus_gd5430 )
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_RAW_PARAMS(XTAL_25_1748MHz,900,0,640,526,0,480)
	MCFG_SCREEN_UPDATE_DEVICE("vga", cirrus_gd5430_device, screen_update)

	MCFG_PALETTE_ADD("palette", 0x100)
	MCFG_DEVICE_ADD("vga", CIRRUS_GD5430, 0)
MACHINE_CONFIG_END

void cirrus_gd5428_device::device_start()
{
	zero();

	int i;
	for (i = 0; i < 0x100; i++)
		m_palette->set_pen_color(i, 0, 0, 0);

	// Avoid an infinite loop when displaying.  0 is not possible anyway.
	vga.crtc.maximum_scan_line = 1;

	// copy over interfaces
	vga.read_dipswitch = read8_delegate(); //read_dipswitch;
	vga.svga_intf.seq_regcount = 0x1f;
	vga.svga_intf.crtc_regcount = 0x2d;
	vga.svga_intf.vram_size = 0x200000;

	vga.memory.resize(vga.svga_intf.vram_size);
	memset(&vga.memory[0], 0, vga.svga_intf.vram_size);
	save_item(NAME(vga.memory));
	save_pointer(vga.crtc.data,"CRTC Registers",0x100);
	save_pointer(vga.sequencer.data,"Sequencer Registers",0x100);
	save_pointer(vga.attribute.data,"Attribute Registers", 0x15);
	save_item(NAME(m_chip_id));

	m_vblank_timer = machine().scheduler().timer_alloc(timer_expired_delegate(FUNC(vga_device::vblank_timer_cb),this));

	m_chip_id = 0x98;  // GD5428 - Rev 0
}

void cirrus_gd5430_device::device_start()
{
	cirrus_gd5428_device::device_start();
	m_chip_id = 0xa0;  // GD5430 - Rev 0
}

void cirrus_gd5428_device::device_reset()
{
	vga_device::device_reset();
	gc_locked = true;
	gc_mode_ext = 0;
	gc_bank_0 = gc_bank_1 = 0;
	m_lock_reg = 0;
	m_blt_status = 0;
	m_cursor_attr = 0x00;  // disable hardware cursor and extra palette
	m_cursor_x = m_cursor_y = 0;
	m_cursor_addr = 0;
	m_scratchpad1 = m_scratchpad2 = m_scratchpad3 = 0;
	m_cr19 = m_cr1a = m_cr1b = 0;
	m_vclk_num[0] = 0x4a;
	m_vclk_denom[0] = 0x2b;
	m_vclk_num[1] = 0x5b;
	m_vclk_denom[1] = 0x2f;
	m_blt_source = m_blt_dest = m_blt_source_current = m_blt_dest_current = 0;
	memset(m_ext_palette, 0, sizeof(m_ext_palette));
	m_ext_palette_enabled = false;
//	m_ext_palette[15].red = m_ext_palette[15].green = m_ext_palette[15].blue = 0xff;  // default?  Win3.1 doesn't seem to touch the extended DAC, or at least, it enables it, then immediately disables it then sets a palette...
}

UINT32 cirrus_gd5428_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	int x,y,bit;
	UINT32 ptr = (vga.svga_intf.vram_size - 0x4000);  // cursor patterns are stored in the last 16kB of VRAM
	svga_device::screen_update(screen, bitmap, cliprect);

	/*UINT8 cur_mode =*/ pc_vga_choosevideomode();
	if(m_cursor_attr & 0x01)  // hardware cursor enabled
	{
		// draw hardware graphics cursor
		if(m_cursor_attr & 0x04)  // 64x64
		{
			ptr += ((m_cursor_addr & 0x3c) * 256);
			for(y=0;y<64;y++)
			{
				for(x=0;x<64;x+=8)
				{
					for(bit=0;bit<8;bit++)
					{
						UINT8 pixel1 = vga.memory[ptr % vga.svga_intf.vram_size] >> (7-bit);
						UINT8 pixel2 = vga.memory[(ptr+512) % vga.svga_intf.vram_size] >> (7-bit);
						UINT8 output = ((pixel1 & 0x01) << 1) | (pixel2 & 0x01);
						switch(output)
						{
						case 0:  // transparent - do nothing
							break;
						case 1:  // background
							bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[0].red << 16) | (m_ext_palette[0].green << 8) | (m_ext_palette[0].blue);
							break;
						case 2:  // XOR
							bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit) = ~bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit);
							break;
						case 3:  // foreground
							bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[15].red << 16) | (m_ext_palette[15].green << 8) | (m_ext_palette[15].blue);
							break;
						}
					}
				}
			}
		}
		else
		{
			ptr += ((m_cursor_addr & 0x3f) * 256);
			for(y=0;y<32;y++)
			{
				for(x=0;x<32;x+=8)
				{
					for(bit=0;bit<8;bit++)
					{
						UINT8 pixel1 = vga.memory[ptr % vga.svga_intf.vram_size] >> (7-bit);
						UINT8 pixel2 = vga.memory[(ptr+128) % vga.svga_intf.vram_size] >> (7-bit);
						UINT8 output = ((pixel1 & 0x01) << 1) | (pixel2 & 0x01);
						switch(output)
						{
						case 0:  // transparent - do nothing
							break;
						case 1:  // background
							bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[0].red << 18) | (m_ext_palette[0].green << 10) | (m_ext_palette[0].blue << 2);
							break;
						case 2:  // XOR
							bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit) = ~bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit);
							break;
						case 3:  // foreground
							bitmap.pix32(m_cursor_y+y,m_cursor_x+x+bit) = (m_ext_palette[15].red << 18) | (m_ext_palette[15].green << 10) | (m_ext_palette[15].blue << 2);
							break;
						}
					}
					ptr++;
				}
			}
		}
	}
	return 0;	
}

void cirrus_gd5428_device::cirrus_define_video_mode()
{
	UINT8 divisor = 1;
	float clock;
	UINT8 clocksel = (vga.miscellaneous_output & 0xc) >> 2;
	
	svga.rgb8_en = 0;
	svga.rgb15_en = 0;
	svga.rgb16_en = 0;
	svga.rgb24_en = 0;
	svga.rgb32_en = 0;

	if(gc_locked || m_vclk_num[clocksel] == 0 || m_vclk_denom[clocksel] == 0)
		clock = (vga.miscellaneous_output & 0xc) ? XTAL_28_63636MHz : XTAL_25_1748MHz;
	else
	{
		int numerator = m_vclk_num[clocksel] & 0x7f;
		int denominator = (m_vclk_denom[clocksel] & 0x3e) >> 1;
		int mul = m_vclk_denom[clocksel] & 0x01 ? 2 : 1;
		clock = 14.31818f * ((float)numerator / ((float)denominator * mul));
		clock *= 1000000;
	}
	
	if (!gc_locked && (vga.sequencer.data[0x07] & 0x01))
	{
		switch(vga.sequencer.data[0x07] & 0x0E)
		{
			case 0x00:  svga.rgb8_en = 1; break;
			case 0x02:  svga.rgb16_en = 1; divisor = 2; break; //double VCLK
			case 0x04:  svga.rgb24_en = 1; divisor = 3; break;
			case 0x06:  svga.rgb16_en = 1; break;
			case 0x08:  svga.rgb32_en = 1; break;
		}
	}
	recompute_params_clock(divisor, (int)clock);
}

UINT16 cirrus_gd5428_device::offset()
{
	UINT16 off = vga_device::offset();

	if (svga.rgb8_en == 1) // guess
		off <<= 2;
//	popmessage("Offset: %04x  %s %s ** -- actual: %04x",vga.crtc.offset,vga.crtc.dw?"DW":"--",vga.crtc.word_mode?"BYTE":"WORD",off);
	return off;
}

void cirrus_gd5428_device::start_bitblt()
{
	UINT32 x,y;

	logerror("CL: BitBLT started: Src: %06x Dst: %06x Width: %i Height %i ROP: %02x Mode: %02x\n",m_blt_source,m_blt_dest,m_blt_width,m_blt_height,m_blt_rop,m_blt_mode);

	m_blt_source_current = m_blt_source;
	m_blt_dest_current = m_blt_dest;
	
	for(y=0;y<m_blt_height;y++)
	{
		for(x=0;x<m_blt_width;x++)
		{
			copy_pixel();
			if(m_blt_mode & 0x80)  // colour expand
			{
				if(x % 8)
					m_blt_source_current++;
			}
			else
				m_blt_source_current++;
			m_blt_dest_current++;
			if(m_blt_mode & 0x40 && (x % 8) == 7)  // 8x8 pattern - reset pattern source location
				m_blt_source_current = m_blt_source + (m_blt_source_pitch*(y % 8));
		}
		if(m_blt_mode & 0x40)  // 8x8 pattern
			m_blt_source_current = m_blt_source + (m_blt_source_pitch*(y % 8));
		else
			m_blt_source_current = m_blt_source + (m_blt_source_pitch*y);
		m_blt_dest_current = m_blt_dest + (m_blt_dest_pitch*y);
	}
	m_blt_status &= ~0x02;
}

void cirrus_gd5428_device::copy_pixel()
{
	UINT8 src = vga.memory[m_blt_source_current % vga.svga_intf.vram_size];
	UINT8 dst = vga.memory[m_blt_dest_current % vga.svga_intf.vram_size];
	
	if(m_blt_mode & 0x40)  // enable 8x8 pattern
	{
		if(m_blt_mode & 0x80)  // colour expand
			src = (vga.memory[m_blt_source % vga.svga_intf.vram_size] >> (abs((int)(m_blt_source_current - m_blt_source)) % 8)) & 0x01 ? 0xff : 0x00;
	}
	
	switch(m_blt_rop)
	{
	case 0x00:  // BLACK
		vga.memory[m_blt_dest_current % vga.svga_intf.vram_size] = 0x00;
		break;
	case 0x0b:  // NOT DST
		vga.memory[m_blt_dest_current % vga.svga_intf.vram_size] = ~dst;
		break;
	case 0x0d:  // SRC
		vga.memory[m_blt_dest_current % vga.svga_intf.vram_size] = src;
		break;
	case 0x0e:  // WHITE
		vga.memory[m_blt_dest_current % vga.svga_intf.vram_size] = 0xff;
		break;
	case 0x59:  // SRCINVERT
		vga.memory[m_blt_dest_current % vga.svga_intf.vram_size] = dst ^ src;
		break;
	default:
		popmessage("CL: Unsupported BitBLT ROP mode %02x",m_blt_rop);
	}
}

UINT8 cirrus_gd5428_device::cirrus_seq_reg_read(UINT8 index)
{
	UINT8 res;

	res = 0xff;

	switch(index)
	{
		case 0x02:
			if(gc_mode_ext & 0x08)
				res = vga.sequencer.map_mask & 0xff;
			else
				res = vga.sequencer.map_mask & 0x0f;
			break;
		case 0x06:
			if(gc_locked)
				return 0x0f;
			else
				return m_lock_reg;
			break;
		case 0x09:
			//printf("%02x\n",index);
			res = vga.sequencer.data[index];
			break;
		case 0x0a:
			res = m_scratchpad1;
			break;
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
			res = m_vclk_num[index-0x0b];
			break;
		case 0x0f:
			res = vga.sequencer.data[index] & 0xe7;
			res |= 0x18;  // 32-bit DRAM data bus width (1MB-2MB)
			break;
		case 0x12:
			res = m_cursor_attr;
			break;
		case 0x14:
			res = m_scratchpad2;
			break;
		case 0x15:
			res = m_scratchpad3;
			break;
		case 0x1b:
		case 0x1c:
		case 0x1d:
		case 0x1e:
			res = m_vclk_denom[index-0x1b];
			break;
		default:
			res = vga.sequencer.data[index];
	}

	return res;
}

void cirrus_gd5428_device::cirrus_seq_reg_write(UINT8 index, UINT8 data)
{
	logerror("CL: SEQ write %02x to SR%02x\n",data,index);
	switch(index)
	{
		case 0x02:
			if(gc_mode_ext & 0x08)
				vga.sequencer.map_mask = data & 0xff;
			else
				vga.sequencer.map_mask = data & 0x0f;
			break;
		case 0x06:
			// Note: extensions are always enabled on the GD5429
			if((data & 0x17) == 0x12)  // bits 3,5,6,7 ignored
			{
				gc_locked = false;
				logerror("Cirrus register extensions unlocked\n");
			}
			else
			{
				gc_locked = true;
				logerror("Cirrus register extensions locked\n");
			}
			m_lock_reg = data & 0x17;
			break;
		case 0x07:
			if((data & 0xf0) != 0)
				popmessage("1MB framebuffer window enabled at %iMB (%02x)",data >> 4,data);
			vga.sequencer.data[vga.sequencer.index] = data;
			break;
		case 0x09:
			//printf("%02x %02x\n",index,data);
			vga.sequencer.data[vga.sequencer.index] = data;
			break;
		case 0x0a:
			m_scratchpad1 = data;  // GD5402/GD542x BIOS writes VRAM size here.
			break;
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
			m_vclk_num[index-0x0b] = data;
			break;
		case 0x10:
		case 0x30:
		case 0x50:
		case 0x70:
		case 0x90:
		case 0xb0:
		case 0xd0:
		case 0xf0:  // bits 5-7 of the register index are the low bits of the X co-ordinate
			m_cursor_x = (data << 3) | ((index & 0xe0) >> 5);
			break;
		case 0x11:
		case 0x31:
		case 0x51:
		case 0x71:
		case 0x91:
		case 0xb1:
		case 0xd1:
		case 0xf1:  // bits 5-7 of the register index are the low bits of the Y co-ordinate
			m_cursor_y = (data << 3) | ((index & 0xe0) >> 5);
			break;
		case 0x12:
			// bit 0 - enable cursor
			// bit 1 - enable extra palette (cursor colours are there)
			// bit 2 - 64x64 cursor (32x32 if clear, GD5422+)
			// bit 7 - overscan colour protect - if set, use colour 2 in the extra palette for the border (GD5424+)
			m_cursor_attr = data;
			m_ext_palette_enabled = data & 0x02;
			break;
		case 0x13:
			m_cursor_addr = data;  // bits 0 and 1 are ignored if using 64x64 cursor
			break;
		case 0x14:
			m_scratchpad2 = data;
			break;
		case 0x15:
			m_scratchpad3 = data;  // GD543x BIOS writes VRAM size here
			break;
		case 0x1b:
		case 0x1c:
		case 0x1d:
		case 0x1e:
			m_vclk_denom[index-0x1b] = data;
			break;
		default:
			vga.sequencer.data[vga.sequencer.index] = data;
			seq_reg_write(vga.sequencer.index,data);
	}
}

UINT8 cirrus_gd5428_device::cirrus_gc_reg_read(UINT8 index)
{
	UINT8 res = 0xff;

	switch(index)
	{
	case 0x00:
		if(gc_mode_ext & 0x02)
			res = vga.gc.set_reset & 0xff;
		else
			res = vga.gc.set_reset & 0x0f;
		break;
	case 0x01:
		if(gc_mode_ext & 0x02)
			res = vga.gc.enable_set_reset & 0xff;
		else
			res = vga.gc.enable_set_reset & 0x0f;
		break;
	case 0x05:
		res  = (vga.gc.shift256 & 1) << 6;
		res |= (vga.gc.shift_reg & 1) << 5;
		res |= (vga.gc.host_oe & 1) << 4;
		res |= (vga.gc.read_mode & 1) << 3;
		if(gc_mode_ext & 0x04)
			res |= (vga.gc.write_mode & 7);
		else
			res |= (vga.gc.write_mode & 3);
		break;
	case 0x09:  // Offset register 0
		res = gc_bank_0;
		break;
	case 0x0a:  // Offset register 1
		res = gc_bank_1;
		break;
	case 0x0b:  // Graphics controller mode extensions
		res = gc_mode_ext;
		break;
	case 0x0c:  // Colour Key
		break;
	case 0x0d:  // Colour Key Mask
		break;
	case 0x0e:  // Miscellaneous Control
		break;
	case 0x10:  // Foreground Colour Byte 1
		break;
	case 0x11:  // Background Colour Byte 1
		break;
	case 0x20:  // BLT Width 0
		res = m_blt_width & 0x00ff;
		break;
	case 0x21:  // BLT Width 1
		res = m_blt_width >> 8;
		break;
	case 0x22:  // BLT Height 0
		res = m_blt_height & 0x00ff;
		break;
	case 0x23:  // BLT Height 1
		res = m_blt_height >> 8;
		break;
	case 0x24:  // BLT Destination Pitch 0
		res = m_blt_dest_pitch & 0x00ff;
		break;
	case 0x25:  // BLT Destination Pitch 1
		res = m_blt_dest_pitch >> 8;
		break;
	case 0x26:  // BLT Source Pitch 0
		res = m_blt_source_pitch & 0x00ff;
		break;
	case 0x27:  // BLT Source Pitch 1
		res = m_blt_source_pitch >> 8;
		break;
	case 0x28:  // BLT Destination start 0
		res = m_blt_dest & 0x000000ff;
		break;
	case 0x29:  // BLT Destination start 1
		res = (m_blt_dest & 0x0000ff00) >> 8;
		break;
	case 0x2a:  // BLT Destination start 2
		res = (m_blt_dest & 0x00ff0000) >> 16;
		break;
	case 0x2c:  // BLT source start 0
		res = m_blt_source & 0x000000ff;
		break;
	case 0x2d:  // BLT source start 1
		res = (m_blt_source & 0x0000ff00) >> 8;
		break;
	case 0x2e:  // BLT source start 2
		res = (m_blt_source & 0x00ff0000) >> 16;
		break;
	case 0x2f:  // BLT destination write mask (GD5430/36/40)
		// TODO
		break;
	case 0x30:  // BLT Mode
		res = m_blt_mode;
		break;
	case 0x31:  // BitBLT Start / Status
		res = m_blt_status;
		break;
	case 0x32:  // BitBLT ROP mode
		res = m_blt_rop;
		break;
	default:
		res = gc_reg_read(index);
	}

	return res;
}

void cirrus_gd5428_device::cirrus_gc_reg_write(UINT8 index, UINT8 data)
{
	logerror("CL: GC write %02x to GR%02x\n",data,index);
	switch(index)
	{
	case 0x00:  // if extended writes are enabled (bit 2 of index 0bh), then index 0 and 1 are extended to 8 bits
		if(gc_mode_ext & 0x02)
			vga.gc.set_reset = data & 0xff;
		else
			vga.gc.set_reset = data & 0x0f;
		break;
	case 0x01:
		if(gc_mode_ext & 0x02)
			vga.gc.enable_set_reset = data & 0xff;
		else
			vga.gc.enable_set_reset = data & 0x0f;
		break;
	case 0x05:
		vga.gc.shift256 = (data & 0x40) >> 6;
		vga.gc.shift_reg = (data & 0x20) >> 5;
		vga.gc.host_oe = (data & 0x10) >> 4;
		vga.gc.read_mode = (data & 8) >> 3;
		if(gc_mode_ext & 0x04)
			vga.gc.write_mode = data & 7;
		else
			vga.gc.write_mode = data & 3;
		break;
	case 0x09:  // Offset register 0
		gc_bank_0 = data;
		logerror("CL: Offset register 0 set to %i\n",data);
		break;
	case 0x0a:  // Offset register 1
		gc_bank_1 = data;
		logerror("CL: Offset register 1 set to %i\n",data);
		break;
	case 0x0b:  // Graphics controller mode extensions
		gc_mode_ext = data;
		if(!(data & 0x02))
		{
			vga.gc.set_reset &= 0x0f;
			vga.gc.enable_set_reset &= 0x0f;
		}
		if(!(data & 0x08))
			vga.sequencer.map_mask &= 0x0f;
		break;
	case 0x0c:  // Colour Key
		break;
	case 0x0d:  // Colour Key Mask
		break;
	case 0x0e:  // Miscellaneous Control
		break;
	case 0x10:  // Foreground Colour Byte 1
		break;
	case 0x11:  // Background Colour Byte 1
		break;
	case 0x20:  // BLT Width 0
		m_blt_width = (m_blt_width & 0xff00) | data;
		break;
	case 0x21:  // BLT Width 1
		m_blt_width = (m_blt_width & 0x00ff) | (data << 8);
		break;
	case 0x22:  // BLT Height 0
		m_blt_height = (m_blt_height & 0xff00) | data;
		break;
	case 0x23:  // BLT Height 1
		m_blt_height = (m_blt_height & 0x00ff) | (data << 8);
		break;
	case 0x24:  // BLT Destination Pitch 0
		m_blt_dest_pitch = (m_blt_dest_pitch & 0xff00) | data;
		break;
	case 0x25:  // BLT Destination Pitch 1
		m_blt_dest_pitch = (m_blt_dest_pitch & 0x00ff) | (data << 8);
		break;
	case 0x26:  // BLT Source Pitch 0
		m_blt_source_pitch = (m_blt_source_pitch & 0xff00) | data;
		break;
	case 0x27:  // BLT Source Pitch 1
		m_blt_source_pitch = (m_blt_source_pitch & 0x00ff) | (data << 8);
		break;
	case 0x28:  // BLT Destination start 0
		m_blt_dest = (m_blt_dest & 0xffffff00) | data;
		break;
	case 0x29:  // BLT Destination start 1
		m_blt_dest = (m_blt_dest & 0xffff00ff) | (data << 8);
		break;
	case 0x2a:  // BLT Destination start 2
		m_blt_dest = (m_blt_dest & 0xff00ffff) | (data << 16);
		break;
	case 0x2c:  // BLT source start 0
		m_blt_source = (m_blt_source & 0xffffff00) | data;
		break;
	case 0x2d:  // BLT source start 1
		m_blt_source = (m_blt_source & 0xffff00ff) | (data << 8);
		break;
	case 0x2e:  // BLT source start 2
		m_blt_source = (m_blt_source & 0xff00ffff) | (data << 16);
		break;
	case 0x2f:  // BLT destination write mask (GD5430/36/40)
		// TODO
		break;
	case 0x30:  // BLT Mode
		m_blt_mode = data;
		break;
	case 0x31:  // BitBLT Start / Status
		m_blt_status = data & 0xf2;
		if(data & 0x02)
			start_bitblt();
		break;
	case 0x32:  // BitBLT ROP mode
		m_blt_rop = data;
		break;
	default:
		gc_reg_write(index,data);
	}
}

READ8_MEMBER(cirrus_gd5428_device::port_03c0_r)
{
	UINT8 res = 0xff;

	switch(offset)
	{
		case 0x05:
			res = cirrus_seq_reg_read(vga.sequencer.index);
			break;
		case 0x09:
			if(!m_ext_palette_enabled)
				res = vga_device::port_03c0_r(space,offset,mem_mask);
			else
			{
				if (vga.dac.read)
				{
					switch (vga.dac.state++)
					{
						case 0:
							res = m_ext_palette[vga.dac.read_index & 0x0f].red;
							break;
						case 1:
							res = m_ext_palette[vga.dac.read_index & 0x0f].green;
							break;
						case 2:
							res = m_ext_palette[vga.dac.read_index & 0x0f].blue;
							break;
					}

					if (vga.dac.state==3)
					{
						vga.dac.state = 0;
						vga.dac.read_index++;
					}
				}
			}				
			break;
		case 0x0f:
			res = cirrus_gc_reg_read(vga.gc.index);
			break;
		default:
			res = vga_device::port_03c0_r(space,offset,mem_mask);
			break;
	}

	return res;
}

WRITE8_MEMBER(cirrus_gd5428_device::port_03c0_w)
{
	switch(offset)
	{
		case 0x05:
			cirrus_seq_reg_write(vga.sequencer.index,data);
			break;
		case 0x09:
			if(!m_ext_palette_enabled)
				vga_device::port_03c0_w(space,offset,data,mem_mask);
			else
			{
				if (!vga.dac.read)
				{
					switch (vga.dac.state++) {
					case 0:
						m_ext_palette[vga.dac.write_index & 0x0f].red=data;
						break;
					case 1:
						m_ext_palette[vga.dac.write_index & 0x0f].green=data;
						break;
					case 2:
						m_ext_palette[vga.dac.write_index & 0x0f].blue=data;
						break;
					}
					vga.dac.dirty=1;
					if (vga.dac.state==3) 
					{
						vga.dac.state=0; 
						vga.dac.write_index++;
					}
				}
			}
			break;
		case 0x0f:
			cirrus_gc_reg_write(vga.gc.index,data);
			break;
		default:
			vga_device::port_03c0_w(space,offset,data,mem_mask);
			break;
	}
	cirrus_define_video_mode();
}

READ8_MEMBER(cirrus_gd5428_device::port_03b0_r)
{
	UINT8 res = 0xff;

	if (CRTC_PORT_ADDR == 0x3b0)
	{
		switch(offset)
		{
			case 5:
				res = cirrus_crtc_reg_read(vga.crtc.index);
				break;
			default:
				res = vga_device::port_03b0_r(space,offset,mem_mask);
				break;
		}
	}

	return res;
}

READ8_MEMBER(cirrus_gd5428_device::port_03d0_r)
{
	UINT8 res = 0xff;

	if (CRTC_PORT_ADDR == 0x3d0)
	{
		switch(offset)
		{
			case 5:
				res = cirrus_crtc_reg_read(vga.crtc.index);
				break;
			default:
				res = vga_device::port_03d0_r(space,offset,mem_mask);
				break;
		}
	}

	return res;
}

WRITE8_MEMBER(cirrus_gd5428_device::port_03b0_w)
{
	if (CRTC_PORT_ADDR == 0x3b0)
	{
		switch(offset)
		{
			case 5:
				vga.crtc.data[vga.crtc.index] = data;
				cirrus_crtc_reg_write(vga.crtc.index,data);
				break;
			default:
				vga_device::port_03b0_w(space,offset,data,mem_mask);
				break;
		}
	}
	cirrus_define_video_mode();
}

WRITE8_MEMBER(cirrus_gd5428_device::port_03d0_w)
{
	if (CRTC_PORT_ADDR == 0x3d0)
	{
		switch(offset)
		{
			case 5:
				vga.crtc.data[vga.crtc.index] = data;
				cirrus_crtc_reg_write(vga.crtc.index,data);
				break;
			default:
				vga_device::port_03d0_w(space,offset,data,mem_mask);
				break;
		}
	}
	cirrus_define_video_mode();
}

UINT8 cirrus_gd5428_device::cirrus_crtc_reg_read(UINT8 index)
{
	UINT8 res = 0xff;

	switch(index)
	{
	case 0x16:  // VGA Vertical Blank end - some SVGA chipsets use all 8 bits, and this is one of them (according to MFGTST CRTC tests)
		res = vga.crtc.vert_blank_end & 0x00ff;
		break;
	case 0x19:
		res = m_cr19;
		break;
	case 0x1a:
		res = m_cr1a;
		break;
	case 0x1b:
		res = m_cr1b;
		break;
	case 0x27:
		res = m_chip_id;
		break;
	default:
		res = crtc_reg_read(index);
		break;
	}

	return res;
}

void cirrus_gd5428_device::cirrus_crtc_reg_write(UINT8 index, UINT8 data)
{
	logerror("CL: CRTC write %02x to CR%02x\n",data,index);
	switch(index)
	{
	case 0x16:  // VGA Vertical Blank end - some SVGA chipsets use all 8 bits, and this is one of them (according to MFGTST CRTC tests)
		vga.crtc.vert_blank_end &= ~0x00ff;
		vga.crtc.vert_blank_end |= data;
		break;
	case 0x19:
		m_cr19 = data;
		break;
	case 0x1a:
		m_cr1a = data;
		vga.crtc.horz_blank_end = (vga.crtc.horz_blank_end & 0xff3f) | ((data & 0x30) << 2);
		vga.crtc.vert_blank_end = (vga.crtc.vert_blank_end & 0xfcff) | ((data & 0xc0) << 2);
		break;
	case 0x1b:
		m_cr1b = data;
		vga.crtc.start_addr_latch &= ~0x070000;
		vga.crtc.start_addr_latch |= ((data & 0x01) << 16);
		vga.crtc.start_addr_latch |= ((data & 0x0c) << 15);
		vga.crtc.offset = (vga.crtc.offset & 0x00ff) | ((data & 0x10) << 4);
		cirrus_define_video_mode();
		break;
	case 0x1d:
		//vga.crtc.start_addr_latch = (vga.crtc.start_addr_latch & 0xf7ffff) | ((data & 0x01) << 16);  // GD543x
		break;
	case 0x27:
		// Do nothing, read only
		break;
	default:
		crtc_reg_write(index,data);
		break;
	}

}

inline UINT8 cirrus_gd5428_device::cirrus_vga_latch_write(int offs, UINT8 data)
{
	UINT8 res = 0;

	switch (vga.gc.write_mode & 3) {
	case 0:
		data = rotate_right(data);
		if(vga.gc.enable_set_reset & 1<<offs)
			res = vga_logical_op((vga.gc.set_reset & 1<<offs) ? vga.gc.bit_mask : 0, offs,vga.gc.bit_mask);
		else
			res = vga_logical_op(data, offs, vga.gc.bit_mask);
		break;
	case 1:
		res = vga.gc.latch[offs];
		break;
	case 2:
		res = vga_logical_op((data & 1<<offs) ? 0xff : 0x00,offs,vga.gc.bit_mask);
		break;
	case 3:
		data = rotate_right(data);
		res = vga_logical_op((vga.gc.set_reset & 1<<offs) ? 0xff : 0x00,offs,data&vga.gc.bit_mask);
		break;
	case 4:
		popmessage("CL: Unimplemented VGA write mode 4 enabled");
		break;
	case 5:
		popmessage("CL: Unimplemented VGA write mode 5 enabled");
		break;
	}

	return res;
}

READ8_MEMBER(cirrus_gd5428_device::mem_r)
{
	UINT32 addr;
	UINT8 bank;
	UINT8 cur_mode = pc_vga_choosevideomode();

	if(gc_locked || offset >= 0x10000 || cur_mode == TEXT_MODE || cur_mode == SCREEN_OFF)
		return vga_device::mem_r(space,offset,mem_mask);

	if(offset >= 0x8000 && offset < 0x10000 && (gc_mode_ext & 0x01)) // if accessing bank 1 (if enabled)
		bank = gc_bank_1;
	else
		bank = gc_bank_0;

	if(gc_mode_ext & 0x20)  // 16kB bank granularity
		addr = bank * 0x4000;
	else  // 4kB bank granularity
		addr = bank * 0x1000;

	// Is the display address adjusted automatically when using Chain-4 addressing?  The GD542x BIOS doesn't do it, but Virtual Pool expects it.
	if(!(vga.sequencer.data[4] & 0x8))
		addr <<= 2;	

	if(svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb24_en)
	{
		UINT8 data = 0;
		if(gc_mode_ext & 0x01)
		{
			if(offset & 0x10000) 
				return 0;
			if(offset < 0x8000)
				offset &= 0x7fff;
			else
			{
				offset -= 0x8000;
				offset &= 0x7fff;
			}
		}
		else
			offset &= 0xffff;
		if(vga.sequencer.data[4] & 0x8)
			data = vga.memory[(offset+addr) % vga.svga_intf.vram_size];
		else
		{
			if(vga.gc.write_mode == 4 || vga.gc.write_mode == 5 || (vga.gc.write_mode == 1 && gc_mode_ext & 0x02))
			{
				int i;

				for(i=0;i<8;i++)
				{
					if(vga.sequencer.map_mask & 1 << i)
						data |= vga.memory[((offset*8+i)+addr) % vga.svga_intf.vram_size];
				}
			}
			else
			{
				int i;

				for(i=0;i<4;i++)
				{
					if(vga.sequencer.map_mask & 1 << i)
						data |= vga.memory[((offset*4+i)+addr) % vga.svga_intf.vram_size];
				}
			}
		return data;
		}
	}

	switch(vga.gc.memory_map_sel & 0x03)
	{
		case 0: break;
		case 1: if(gc_mode_ext & 0x01) offset &= 0x7fff; else offset &= 0x0ffff; break;
		case 2: offset -= 0x10000; offset &= 0x07fff; break;
		case 3: offset -= 0x18000; offset &= 0x07fff; break;
	}

	if(vga.sequencer.data[4] & 4)
	{
		int data;
		if (!space.debugger_access())
		{
			vga.gc.latch[0]=vga.memory[(offset+addr) % vga.svga_intf.vram_size];
			vga.gc.latch[1]=vga.memory[((offset+addr)+0x10000) % vga.svga_intf.vram_size];
			vga.gc.latch[2]=vga.memory[((offset+addr)+0x20000) % vga.svga_intf.vram_size];
			vga.gc.latch[3]=vga.memory[((offset+addr)+0x30000) % vga.svga_intf.vram_size];
		}

		if (vga.gc.read_mode)
		{
			UINT8 byte,layer;
			UINT8 fill_latch;
			data=0;

			for(byte=0;byte<8;byte++)
			{
				fill_latch = 0;
				for(layer=0;layer<4;layer++)
				{
					if(vga.gc.latch[layer] & 1 << byte)
						fill_latch |= 1 << layer;
				}
				fill_latch &= vga.gc.color_dont_care;
				if(fill_latch == vga.gc.color_compare)
					data |= 1 << byte;
			}
		}
		else
			data=vga.gc.latch[vga.gc.read_map_sel];

		return data;
	}
	else
	{
		// TODO: Lines up in 16-colour mode, likely different for 256-colour modes (docs say video addresses are shifted right 3 places)
		UINT8 i,data;
//		UINT8 bits = ((gc_mode_ext & 0x08) && (vga.gc.write_mode == 1)) ? 8 : 4;

		data = 0;
		//printf("%08x\n",offset);

		if(gc_mode_ext & 0x02)
		{
			for(i=0;i<8;i++)
			{
				if(vga.sequencer.map_mask & 1 << i)
					data |= vga.memory[(((offset+addr))+i*0x10000) % vga.svga_intf.vram_size];
			}
		}
		else
		{
			for(i=0;i<4;i++)
			{
				if(vga.sequencer.map_mask & 1 << i)
					data |= vga.memory[(((offset+addr))+i*0x10000) % vga.svga_intf.vram_size];
			}
		}

		return data;
	}
}

WRITE8_MEMBER(cirrus_gd5428_device::mem_w)
{
	UINT32 addr;
	UINT8 bank;
	UINT8 cur_mode = pc_vga_choosevideomode();

	if(gc_locked || offset >= 0x10000 || cur_mode == TEXT_MODE || cur_mode == SCREEN_OFF)
	{
		vga_device::mem_w(space,offset,data,mem_mask);
		return;
	}

	if(offset >= 0x8000 && offset < 0x10000 && (gc_mode_ext & 0x01)) // if accessing bank 1 (if enabled)
		bank = gc_bank_1;
	else
		bank = gc_bank_0;

	if(gc_mode_ext & 0x20)  // 16kB bank granularity
		addr = bank * 0x4000;
	else  // 4kB bank granularity
		addr = bank * 0x1000;

	// Is the display address adjusted automatically when using Chain-4 addressing?  The GD542x BIOS doesn't do it, but Virtual Pool expects it.
	if(!(vga.sequencer.data[4] & 0x8))
		addr <<= 2;	

	if(svga.rgb8_en || svga.rgb15_en || svga.rgb16_en || svga.rgb24_en)
	{
		if(offset & 0x10000) 
			return;
		if(gc_mode_ext & 0x01)
		{
			if(offset < 0x8000)
				offset &= 0x7fff;
			else
			{
				offset -= 0x8000;
				offset &= 0x7fff;
			}
		}
		else
			offset &= 0xffff;
		if(gc_mode_ext & 0x08)
		{
			int i;
			for(i=0;i<8;i++)
			{
				if(vga.sequencer.map_mask & 1 << i)
				{
					if(gc_mode_ext & 0x02)
						vga.memory[(((offset+addr)>>3)+i*0x10000) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? cirrus_vga_latch_write(i,data) : data;
					else
						vga.memory[((offset+addr)+i*0x10000) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? cirrus_vga_latch_write(i,data) : data;
				}
			}
			return;
		}
		if(vga.sequencer.data[4] & 0x8)
			vga.memory[(offset+addr) % vga.svga_intf.vram_size] = data;
		else
		{
			int i;
			if(vga.gc.write_mode == 4 || vga.gc.write_mode == 5 || (vga.gc.write_mode == 1 && gc_mode_ext & 0x08))
			{
				for(i=0;i<8;i++)
				{
					if(vga.sequencer.map_mask & 1 << i)
						vga.memory[((offset*8+i)+addr) % vga.svga_intf.vram_size] = data;
				}
			}
			else
			{
				for(i=0;i<4;i++)
				{
					if(vga.sequencer.map_mask & 1 << i)
						vga.memory[((offset*4+i)+addr) % vga.svga_intf.vram_size] = data;
				}
			}
		}
	}
	else
	{
		//Inside each case must prevent writes to non-mapped VGA memory regions, not only mask the offset.
		switch(vga.gc.memory_map_sel & 0x03)
		{
			case 0: break;
			case 1:
				if(offset & 0x10000)
					return;

				if(gc_mode_ext & 0x01)
					offset &= 0x7fff;
				else
					offset &= 0xffff;
				break;
			case 2:
				if((offset & 0x18000) != 0x10000)
					return;

				offset &= 0x07fff;
				break;
			case 3:
				if((offset & 0x18000) != 0x18000)
					return;

				offset &= 0x07fff;
				break;
		}

		{
		// TODO: Lines up in 16-colour mode, likely different for 256-colour modes (docs say video addresses are shifted right 3 places)
			UINT8 i;
//			UINT8 bits = ((gc_mode_ext & 0x08) && (vga.gc.write_mode == 1)) ? 8 : 4;

			for(i=0;i<4;i++)
			{
				if(vga.sequencer.map_mask & 1 << i)
				{
					if(gc_mode_ext & 0x02)
					{
						vga.memory[(((offset+addr) << 1)+i*0x10000) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? cirrus_vga_latch_write(i,data) : data;
						vga.memory[(((offset+addr) << 1)+i*0x10000+1) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? cirrus_vga_latch_write(i,data) : data;
					}
					else
						vga.memory[(((offset+addr))+i*0x10000) % vga.svga_intf.vram_size] = (vga.sequencer.data[4] & 4) ? cirrus_vga_latch_write(i,data) : data;
				}
			}
			return;
		}
	}
}

