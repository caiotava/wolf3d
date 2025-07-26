// ID_VL.C

#include <string.h>
#include "ID_HEAD.H"
#include "ID_VL.H"

#include "WL_DEF.H"
#pragma hdrstop

//
// SC_INDEX is expected to stay at SC_MAPMASK for proper operation
//

unsigned	bufferofs;
unsigned	displayofs,pelpan;

unsigned	screenseg=SCREENSEG;		// set to 0xa000 for asm convenience

unsigned	linewidth;
unsigned	ylookup[MAXSCANLINES];

bool		screenfaded;
unsigned	bordercolor;

bool		fastpalette;				// if true, use outsb to set

uint32_t	screenBuffer[320*200];

SDL_Color	palette1[256],palette2[256];
SDL_Color	curpal[256];

#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, SDL_ALPHA_OPAQUE}

SDL_Color gamepal[] = {
	#include "wolfpal.inc"
};

//===========================================================================

// asm
void VL_SetCRTC (int crtc) {}
void VL_WaitVBL (int vbls) {
	SDL_Delay(vbls * 8);
}

/*
=======================
=
= VL_Startup	// WOLFENSTEIN HACK
=
=======================
*/

void	VL_Startup (void)
{
}



/*
=======================
=
= VL_Shutdown
=
=======================
*/

void	VL_Shutdown (void)
{
	VL_SetTextMode ();
}

/*
=======================
=
= VL_SetTextMode
=
=======================
*/

void	VL_SetTextMode (void)
{
// asm	mov	ax,3
// asm	int	0x10
}

//===========================================================================

/*
=================
=
= VL_ClearVideo
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearVideo (const uint32_t color)
{
	int r = color & 0xff;
	int g = (color >> 8) & 0xff;
	int b = (color >> 16) & 0xff;

	SDL_SetRenderDrawColor(sdlRenderer, r, g, b, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(sdlRenderer);
	SDL_RenderPresent(sdlRenderer);
}


/*
=============================================================================

			VGA REGISTER MANAGEMENT ROUTINES

=============================================================================
*/


//===========================================================================

/*
====================
=
= VL_SetLineWidth
=
= Line witdh is in WORDS, 40 words is normal width for vgaplanegr
=
====================
*/

void VL_SetLineWidth (unsigned width)
{
//
// set up lookup tables
//
	for (int i = 0; i < MAXVIEWHEIGHT; i++) {
		ylookup[i]= i * width;
	}
}

/*
====================
=
= VL_SetSplitScreen
=
====================
*/

void VL_SetSplitScreen (int linenum)
{
	VL_WaitVBL (1);
	linenum=linenum*2-1;
	// outportb (CRTC_INDEX,CRTC_LINECOMPARE);
	// outportb (CRTC_INDEX+1,linenum % 256);
	// outportb (CRTC_INDEX,CRTC_OVERFLOW);
	// outportb (CRTC_INDEX+1, 1+16*(linenum/256));
	// outportb (CRTC_INDEX,CRTC_MAXSCANLINE);
	// outportb (CRTC_INDEX+1,inportb(CRTC_INDEX+1) & (255-64));
}


/*
=============================================================================

						PALETTE OPS

		To avoid snow, do a WaitVBL BEFORE calling these

=============================================================================
*/


/*
=================
=
= VL_FillPalette
=
=================
*/

void VL_FillPalette (int red, int green, int blue)
{
	int	i;

	// outportb (PEL_WRITE_ADR,0);
	// for (i=0;i<256;i++)
	// {
	// 	outportb (PEL_DATA,red);
	// 	outportb (PEL_DATA,green);
	// 	outportb (PEL_DATA,blue);
	// }
}

//===========================================================================

/*
=================
=
= VL_SetColor
=
=================
*/

void VL_SetColor	(int color, int red, int green, int blue)
{
	// outportb (PEL_WRITE_ADR,color);
	// outportb (PEL_DATA,red);
	// outportb (PEL_DATA,green);
	// outportb (PEL_DATA,blue);
}

//===========================================================================

/*
=================
=
= VL_GetColor
=
=================
*/

void VL_GetColor	(int color, int *red, int *green, int *blue)
{
	// outportb (PEL_READ_ADR,color);
	// *red = inportb (PEL_DATA);
	// *green = inportb (PEL_DATA);
	// *blue = inportb (PEL_DATA);
}

//===========================================================================

/*
=================
=
= VL_SetPalette
=
= If fast palette setting has been tested for, it is used
= (some cards don't like outsb palette setting)
=
=================
*/

void VL_SetPalette (const SDL_Color *palette, const int size)
{
	memcpy(curpal, palette, sizeof(SDL_Color) * 256);

	SDL_SetPaletteColors(sdlScreen->format->palette, palette, 0, size);
	SDL_SetPaletteColors(sdlScreenBuffer->format->palette, palette, 0, size);
}


//===========================================================================


void VL_GetPalette (SDL_Color *palette) {
	memcpy(palette, curpal, sizeof(SDL_Color) * 256);
}


//===========================================================================

/*
=================
=
= VL_FadeOut
=
= Fades the current palette to the given color in the given number of steps
=
=================
*/

void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
	int			i,j,orig,delta;
	SDL_Color	*origptr, *newptr;

	red = red * 255 / 63;
	green = green * 255 / 63;
	blue = blue * 255 / 63;

	VL_WaitVBL(1);
	VL_GetPalette(palette1);
	memcpy (palette2, palette1,sizeof(SDL_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		origptr = &palette1[start];
		newptr = &palette2[start];
		for (j=start;j<=end;j++)
		{
			orig = origptr->r;
			delta = red-orig;
			newptr->r = orig + delta * i / steps;
			orig = origptr->g;
			delta = green-orig;
			newptr->g = orig + delta * i / steps;
			orig = origptr->b;
			delta = blue-orig;
			newptr->b = orig + delta * i / steps;
			newptr->a = SDL_ALPHA_OPAQUE;
			origptr++;
			newptr++;
		}

		VL_WaitVBL(1);
		VL_SetPalette (palette2, true);
	}

//
// final color
//
	VL_FillPalette (red,green,blue);

	screenfaded = true;
}


/*
=================
=
= VL_FadeIn
=
=================
*/

void VL_FadeIn (int start, int end, SDL_Color *palette, int steps)
{
	int		i,j,delta;

	VL_WaitVBL(1);
	VL_GetPalette(palette1);
	memcpy (palette2, palette1,sizeof(SDL_Color) * 256);
//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		for (j=start;j<=end;j++)
		{
			delta = palette[j].r-palette1[j].r;
			palette2[j].r = palette1[j].r + delta * i / steps;
			delta = palette[j].g-palette1[j].g;
			palette2[j].g = palette1[j].g + delta * i / steps;
			delta = palette[j].b-palette1[j].b;
			palette2[j].b = palette1[j].b + delta * i / steps;
			palette2[j].a = SDL_ALPHA_OPAQUE;
		}

		VL_WaitVBL(1);
		VL_SetPalette (palette2, true);
	}

//
// final color
//
	VL_SetPalette (palette, true);
	screenfaded = false;
}



/*
==================
=
= VL_ColorBorder
=
==================
*/

void VL_ColorBorder (int color)
{
	// _AH=0x10;
	// _AL=1;
	// _BH=color;
	// geninterrupt (0x10);
	bordercolor = color;
}



/*
=============================================================================

							PIXEL OPS

=============================================================================
*/

byte	pixmasks[4] = {1,2,4,8};
byte	leftmasks[4] = {15,14,12,8};
byte	rightmasks[4] = {1,3,7,15};


/*
=================
=
= VL_Plot
=
=================
*/

void VL_Plot (int x, int y, int color)
{
	byte mask;

	mask = pixmasks[x&3];
	// VGAMAPMASK(mask);
	// *(byte *)MK_FP(SCREENSEG,bufferofs+(ylookup[y]+(x>>2))) = color;
	// VGAMAPMASK(15);
}


/*
=================
=
= VL_Hlin
=
=================
*/

void VL_Hlin (unsigned x, unsigned y, unsigned width, unsigned color)
{
	byte *dest = VL_LockSurface(sdlScreenBuffer);
	if (dest == NULL) {
		return;
	}

	*dest++ = color;

	memset (dest+ylookup[y]+x, color, width);

	VL_UnlockSurface(sdlScreenBuffer);
}


/*
=================
=
= VL_Vlin
=
=================
*/

void VL_Vlin (int x, int y, int height, int color)
{
	byte	*dest = VL_LockSurface(sdlScreenBuffer);
	if (dest == NULL) {
		return;
	}

	dest += ylookup[y] + x;

	while (height--)
	{
		*dest = color;
		dest += linewidth;
	}

	VL_UnlockSurface(sdlScreenBuffer);
}


/*
=================
=
= VL_Bar
=
=================
*/

void VL_Bar (int x, int y, int width, int height, int color)
{
	byte *dest = VL_LockSurface(sdlScreenBuffer);
	if (dest == NULL) {
		return;
	}

	dest += ylookup[y] + x;

	while (height--) {
		memset(dest, color, width);
		dest += sdlScreenBuffer->pitch;
	}

	VL_UnlockSurface(sdlScreenBuffer);
}

/*
============================================================================

							MEMORY OPS

============================================================================
*/

/*
===================
=
= VL_DePlaneVGA
=
= Unweave a VGA graphic to simplify drawing
=
===================
*/

void VL_DePlaneVGA (byte *source, int width, int height)
{
	int  x,y,plane;
	byte *temp,*dest;

	const word size = width * height;

	if (width & 3)
		Quit ("DePlaneVGA: width not divisible by 4!");

	MM_GetPtr((memptr**)&temp, size);

	//
	// munge pic into the temp buffer
	//
	byte *srcline = source;
	word pwidth = width >> 2;

	for (plane = 0; plane < 4; plane++)
	{
		dest = temp;

		for (y = 0; y < height; y++)
		{
			for (x = 0; x < pwidth; x++)
				*(dest + (x << 2) + plane) = *srcline++;

			dest += width;
		}
	}

	//
	// copy the temp buffer back into the original source
	//
	memcpy (source,temp,size);

	MM_FreePtr ((memptr**)&temp);
}

/*
=================
=
= VL_MemToLatch
=
=================
*/

void VL_MemToLatch (byte *source, int width, int height, unsigned dest)
{
	unsigned	count;
	byte	plane,mask;

	count = ((width+3)/4)*height;
	mask = 1;
	for (plane = 0; plane<4 ; plane++)
	{
		// VGAMAPMASK(mask);
		mask <<= 1;

// asm	mov	cx,count
// asm mov ax,SCREENSEG
// asm mov es,ax
// asm	mov	di,[dest]
// asm	lds	si,[source]
// asm	rep movsb
// asm mov	ax,ss
// asm	mov	ds,ax

		source+= count;
	}
}


//===========================================================================


/*
=================
=
= VL_MemToScreen
=
= Draws a block of data to the screen.
=
=================
*/

void VL_MemToScreen (const byte *source, const int width, const int height, const int x, const int y)
{
	byte *dest = VL_LockSurface(sdlScreenBuffer);
	if (dest == NULL) {
		return;
	}

	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			const byte color = source[(j*width) + i];
			dest[ylookup[j+y] + x+i] = color;
		}
	}

	VL_UnlockSurface(sdlScreenBuffer);
}

//==========================================================================


/*
=================
=
= VL_MaskedToScreen
=
= Masks a block of main memory to the screen.
=
=================
*/

void VL_MaskedToScreen (byte *source, int width, int height, int x, int y)
{
	byte    *screen,*dest,mask;
	byte	*maskptr;
	int		plane;

	width>>=2;
	// dest = MK_FP(SCREENSEG,bufferofs+ylookup[y]+(x>>2) );
//	mask = 1 << (x&3);

//	maskptr = source;

	for (plane = 0; plane<4; plane++)
	{
		// VGAMAPMASK(mask);
		mask <<= 1;
		if (mask == 16)
			mask = 1;

		screen = dest;
		for (y=0;y<height;y++,screen+=linewidth,source+=width)
			memcpy (screen,source,width);
	}
}

//==========================================================================

/*
=================
=
= VL_LatchToScreen
=
=================
*/

void VL_LatchToScreen (unsigned source, int width, int height, int x, int y)
{
	// VGAWRITEMODE(1);
	// VGAMAPMASK(15);

// asm	mov	di,[y]				// dest = bufferofs+ylookup[y]+(x>>2)
// asm	shl	di,1
// asm	mov	di,[WORD PTR ylookup+di]
// asm	add	di,[bufferofs]
// asm	mov	ax,[x]
// asm	shr	ax,2
// asm	add	di,ax
//
// asm	mov	si,[source]
// asm	mov	ax,[width]
// asm	mov	bx,[linewidth]
// asm	sub	bx,ax
// asm	mov	dx,[height]
// asm	mov	cx,SCREENSEG
// asm	mov	ds,cx
// asm	mov	es,cx
//
// drawline:
// asm	mov	cx,ax
// asm	rep movsb
// asm	add	di,bx
// asm	dec	dx
// asm	jnz	drawline
//
// asm	mov	ax,ss
// asm	mov	ds,ax
//
// 	VGAWRITEMODE(0);
}


//===========================================================================

/*
=================
=
= VL_ScreenToScreen
=
=================
*/

void VL_ScreenToScreen (unsigned source, unsigned dest,int width, int height)
{
// 	VGAWRITEMODE(1);
// 	VGAMAPMASK(15);
//
// asm	mov	si,[source]
// asm	mov	di,[dest]
// asm	mov	ax,[width]
// asm	mov	bx,[linewidth]
// asm	sub	bx,ax
// asm	mov	dx,[height]
// asm	mov	cx,SCREENSEG
// asm	mov	ds,cx
// asm	mov	es,cx
//
// drawline:
// asm	mov	cx,ax
// asm	rep movsb
// asm	add	si,bx
// asm	add	di,bx
// asm	dec	dx
// asm	jnz	drawline
//
// asm	mov	ax,ss
// asm	mov	ds,ax
//
// 	VGAWRITEMODE(0);
}


/*
=============================================================================

						STRING OUTPUT ROUTINES

=============================================================================
*/




/*
===================
=
= VL_DrawTile8String
=
===================
*/

void VL_DrawTile8String (char *str, char *tile8ptr, int printx, int printy)
{
	int		i;
	unsigned	*dest,*screen,*src;

	// dest = MK_FP(SCREENSEG,bufferofs+ylookup[printy]+(printx>>2));

	while (*str)
	{
		src = (unsigned *)(tile8ptr + (*str<<6));
		// each character is 64 bytes

		// VGAMAPMASK(1);
		screen = dest;
		for (i=0;i<8;i++,screen+=linewidth)
			*screen = *src++;
		// VGAMAPMASK(2);
		screen = dest;
		for (i=0;i<8;i++,screen+=linewidth)
			*screen = *src++;
		// VGAMAPMASK(4);
		screen = dest;
		for (i=0;i<8;i++,screen+=linewidth)
			*screen = *src++;
		// VGAMAPMASK(8);
		screen = dest;
		for (i=0;i<8;i++,screen+=linewidth)
			*screen = *src++;

		str++;
		printx += 8;
		dest+=2;
	}
}



/*
===================
=
= VL_DrawLatch8String
=
===================
*/

void VL_DrawLatch8String (char *str, unsigned tile8ptr, int printx, int printy)
{
	int		i;
	unsigned	src,dest;

	dest = bufferofs+ylookup[printy]+(printx>>2);

	// VGAWRITEMODE(1);
	// VGAMAPMASK(15);

	while (*str)
	{
		src = tile8ptr + (*str<<4);		// each character is 16 latch bytes

// asm	mov	si,[src]
// asm	mov	di,[dest]
// asm	mov	dx,[linewidth]
//
// asm	mov	ax,SCREENSEG
// asm	mov	ds,ax
//
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
// asm	lodsw
// asm	mov	[di],ax
// asm	add	di,dx
//
// asm	mov	ax,ss
// asm	mov	ds,ax

		str++;
		printx += 8;
		dest+=2;
	}

	// VGAWRITEMODE(0);
}


/*
===================
=
= VL_SizeTile8String
=
===================
*/

void VL_SizeTile8String (char *str, int *width, int *height)
{
	*height = 8;
	*width = 8*strlen(str);
}


byte* VL_LockSurface(SDL_Surface *surface) {
	if (SDL_MUSTLOCK(surface)) {
		if (SDL_LockSurface(surface) < 0) {
			return NULL;
		}
	}

	return surface->pixels;
}

void VL_UnlockSurface(SDL_Surface *surface) {
	if (SDL_MUSTLOCK(surface)) {
		SDL_UnlockSurface(surface);
	}
}
