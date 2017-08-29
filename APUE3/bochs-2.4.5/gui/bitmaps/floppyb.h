/////////////////////////////////////////////////////////////////////////
// $Id: floppyb.h,v 1.3 2009/12/04 20:02:12 sshwarts Exp $
/////////////////////////////////////////////////////////////////////////

#define BX_FLOPPYB_BMAP_X 32
#define BX_FLOPPYB_BMAP_Y 32

static const unsigned char bx_floppyb_bmap[(BX_FLOPPYB_BMAP_X * BX_FLOPPYB_BMAP_Y)/8] = {
   0x00, 0xe0, 0x00, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0xe0, 0x08, 0x00,
   0x00, 0x20, 0x01, 0x00, 0x00, 0x20, 0x09, 0x00, 0x00, 0xe0, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0x01, 0x80, 0x07,
   0x20, 0xfd, 0xbf, 0x04, 0x20, 0x01, 0x80, 0x04, 0xe0, 0xfd, 0xbf, 0x07,
   0xe0, 0x01, 0x80, 0x07, 0xe0, 0xfd, 0xbf, 0x07, 0xe0, 0x01, 0x80, 0x07,
   0xe0, 0xfd, 0xbf, 0x07, 0xe0, 0x01, 0x80, 0x07, 0xe0, 0xfd, 0xbf, 0x07,
   0xe0, 0x01, 0x80, 0x07, 0xe0, 0xfd, 0xbf, 0x07, 0xe0, 0x01, 0x80, 0x07,
   0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07,
   0xe0, 0xff, 0xff, 0x07, 0xe0, 0xaf, 0xea, 0x07, 0xe0, 0xf7, 0xd5, 0x07,
   0xe0, 0xef, 0xea, 0x07, 0xe0, 0xf7, 0xd5, 0x07, 0xc0, 0xef, 0xea, 0x07,
   0x80, 0x57, 0xd5, 0x07, 0x00, 0xaf, 0xea, 0x07
};

static const unsigned char bx_floppyb_eject_bmap[(BX_FLOPPYB_BMAP_X * BX_FLOPPYB_BMAP_Y)/8] = {
   0x01, 0xe0, 0x00, 0x80, 0x02, 0x20, 0x01, 0x40, 0x04, 0xe0, 0x08, 0x20,
   0x08, 0x20, 0x01, 0x10, 0x10, 0x20, 0x09, 0x08, 0x20, 0xe0, 0x00, 0x04,
   0x40, 0x00, 0x00, 0x02, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0x01, 0x80, 0x07,
   0x20, 0xff, 0xff, 0x04, 0x20, 0x05, 0xa0, 0x04, 0xe0, 0xfd, 0xbf, 0x07,
   0xe0, 0x11, 0x88, 0x07, 0xe0, 0xfd, 0xbf, 0x07, 0xe0, 0x41, 0x82, 0x07,
   0xe0, 0xfd, 0xbf, 0x07, 0xe0, 0x81, 0x81, 0x07, 0xe0, 0xfd, 0xbf, 0x07,
   0xe0, 0x21, 0x84, 0x07, 0xe0, 0xfd, 0xbf, 0x07, 0xe0, 0x09, 0x90, 0x07,
   0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07, 0xe0, 0xff, 0xff, 0x07,
   0xe0, 0xff, 0xff, 0x07, 0xe0, 0xaf, 0xea, 0x07, 0xe0, 0xf7, 0xd5, 0x07,
   0xf0, 0xef, 0xea, 0x0f, 0xe8, 0xf7, 0xd5, 0x17, 0xc4, 0xef, 0xea, 0x27,
   0x82, 0x57, 0xd5, 0x47, 0x01, 0xaf, 0xea, 0x87
};
