// WL_SCALE.C

#include "WL_DEF.H"
#pragma hdrstop

#define OP_RETF	0xcb

/*
=============================================================================

						  GLOBALS

=============================================================================
*/

t_compscale *scaledirectory[MAXSCALEHEIGHT+1];
long			fullscalefarcall[MAXSCALEHEIGHT+1];

int			maxscale,maxscaleshl2;

bool	insetupscaling;

/*
=============================================================================

						  LOCALS

=============================================================================
*/

t_compscale *work;
unsigned BuildCompScale (int height, memptr *finalspot);

int			stepbytwo;

//===========================================================================

/*
==============
=
= BadScale
=
==============
*/

void BadScale (void)
{
	Quit ("BadScale called!");
}


/*
==========================
=
= SetupScaling
=
==========================
*/

void SetupScaling (int maxscaleheight)
{
	int		i,x,y;
	byte	*dest;

	insetupscaling = true;

	maxscaleheight/=2;			// one scaler every two pixels

	maxscale = maxscaleheight-1;
	maxscaleshl2 = maxscale<<2;

//
// free up old scalers
//
	for (i=1;i<MAXSCALEHEIGHT;i++)
	{
		if (scaledirectory[i])
			MM_FreePtr ((memptr*)scaledirectory[i]);
		if (i>=stepbytwo)
			i += 2;
	}
	memset (scaledirectory,0,sizeof(scaledirectory));

	MM_SortMem ();

//
// build the compiled scalers
//
	stepbytwo = viewheight/2;	// save space by double stepping
	MM_GetPtr ((memptr**)&work,20000);

	for (i=1;i<=maxscaleheight;i++)
	{
		// BuildCompScale (i*2,(memptr*)scaledirectory[i]);
		if (i>=stepbytwo)
			i+= 2;
	}
	MM_FreePtr ((memptr**)&work);

//
// compact memory and lock down scalers
//
	MM_SortMem ();
	for (i=1;i<=maxscaleheight;i++)
	{
		MM_SetLock ((memptr*)scaledirectory[i],true);
		fullscalefarcall[i] = (long)scaledirectory[i];
		fullscalefarcall[i] <<=16;
		fullscalefarcall[i] += scaledirectory[i]->codeofs[0];
		if (i>=stepbytwo)
		{
			scaledirectory[i+1] = scaledirectory[i];
			fullscalefarcall[i+1] = fullscalefarcall[i];
			scaledirectory[i+2] = scaledirectory[i];
			fullscalefarcall[i+2] = fullscalefarcall[i];
			i+=2;
		}
	}
	scaledirectory[0] = scaledirectory[1];
	fullscalefarcall[0] = fullscalefarcall[1];

//
// check for oversize wall drawing
//
	for (i=maxscaleheight;i<MAXSCALEHEIGHT;i++)
		fullscalefarcall[i] = (long)BadScale;

	insetupscaling = false;
}

//===========================================================================

/*
========================
=
= BuildCompScale
=
= Builds a compiled scaler object that will scale a 64 tall object to
= the given height (centered vertically on the screen)
=
= height should be even
=
= Call with
= ---------
= DS:SI		Source for scale
= ES:DI		Dest for scale
=
= Calling the compiled scaler only destroys AL
=
========================
*/

unsigned BuildCompScale (int height, memptr *finalspot)
{
	byte		*code;

	int			i;
	long		fix,step;
	unsigned	src,totalscaled,totalsize;
	int			startpix,endpix,toppix;


	step = ((long)height<<16) / 64;
	code = &work->code[0];
	toppix = (viewheight-height)/2;
	fix = 0;

	for (src=0;src<=64;src++)
	{
		startpix = fix>>16;
		fix += step;
		endpix = fix>>16;

		if (endpix>startpix)
			work->width[src] = endpix-startpix;
		else
			work->width[src] = 0;

//
// mark the start of the code
//
		work->codeofs[src] = (unsigned long)code;

//
// compile some code if the source pixel generates any screen pixels
//
		startpix+=toppix;
		endpix+=toppix;

		if (startpix == endpix || endpix < 0 || startpix >= viewheight || src == 64)
			continue;

	//
	// mov al,[si+src]
	//
		*code++ = 0x8a;
		*code++ = 0x44;
		*code++ = src;

		for (;startpix<endpix;startpix++)
		{
			if (startpix >= viewheight)
				break;						// off the bottom of the view area
			if (startpix < 0)
				continue;					// not into the view area

		//
		// mov [es:di+heightofs],al
		//
			*code++ = 0x26;
			*code++ = 0x88;
			*code++ = 0x85;
			// *((unsigned *)code)++ = startpix*SCREENBWIDE;
		}

	}

//
// retf
//
	*code++ = 0xcb;

	totalsize = (unsigned long)code;
	MM_GetPtr (&finalspot,totalsize);
	memcpy ((byte*)(*finalspot),(byte*)work,totalsize);

	return totalsize;
}


/*
=======================
=
= ScaleLine
=
= linescale should have the high word set to the segment of the scaler
=
=======================
*/

extern	int			slinex,slinewidth;
extern	unsigned	*linecmds;
extern	long		linescale;
extern	unsigned	maskword;

byte	mask1,mask2,mask3;

uint16_t ReadShort (void *ptr)
{
	unsigned value;
	byte     *work;

	work = ptr;
	value = work[0] | (work[1] << 8);

	return value;
}

void ScaleLine (int16_t x, int16_t toppix, fixed fracstep, byte *linesrc, byte *linecmds, byte *shade)
{
	byte  *src,*dest;
	int   color;
	int   start,end,top;
	int   startpix,endpix;
	fixed frac;

	for (end = ReadShort(linecmds) >> 1; end; end = ReadShort(linecmds) >> 1)
	{
		top = (int16_t)ReadShort(linecmds + 2);
		start = ReadShort(linecmds + 4) >> 1;

		frac = start * fracstep;

		endpix = (frac >> FRACBITS) + toppix;

		for (src = &linesrc[top + start]; start != end; start++, src++)
		{
			startpix = endpix;

			if (startpix >= viewheight)
				break;                          // off the bottom of the view area

			frac += fracstep;
			endpix = (frac >> FRACBITS) + toppix;

			if (endpix < 0)
				continue;                       // not into the view area

			if (startpix < 0)
				startpix = 0;                   // clip upper boundary

			if (endpix > viewheight)
				endpix = viewheight;            // clip lower boundary

#ifdef USE_SHADING
			color = shade[*src];
#else
			color = *src;
#endif
			dest = vbuf + ylookup[startpix] + x;

			while (startpix < endpix)
			{
				*dest = color;
				dest += bufferPitch;
				startpix++;
			}
		}

		linecmds += 6;                          // next segment list
	}
}

/*
=======================
=
= ScaleShape
=
= Draws a compiled shape at [scale] pixels high
=
= each vertical line of the shape has a pointer to segment data:
= 	end of segment pixel*2 (0 terminates line) used to patch rtl in scaler
= 	top of virtual line with segment in proper place
=	start of segment pixel*2, used to jsl into compiled scaler
=	<repeat>
=
= Setup for call
= --------------
= GC_MODE			read mode 1, write mode 2
= GC_COLORDONTCARE  set to 0, so all reads from video memory return 0xff
= GC_INDEX			pointing at GC_BITMASK
=
=======================
*/

static	long		longtemp;

void ScaleShape (int xxcenter, int shapenum, unsigned xheight)
{
	int         i;
	compshape_t *shape;
	byte        *linesrc,*linecmds;
	byte        *shade = NULL;
	int         height,toppix;
	int         x1,x2,xcenter;
	fixed       frac,fracstep;

	height = xheight >> 3;        // low three bits are fractional

	if (!height)
		return;                 // too close or far away

	linesrc = PM_GetSpritePage(shapenum);
	shape = (compshape_t *)linesrc;
#ifdef USE_SHADING
	shade = GetShade(sprite->viewheight,sprite->flags);
#endif
	fracstep = ((int64_t)height << FRACBITS) / (int64_t)TEXTURESIZE/2;
	frac = shape->leftpix * fracstep;

	xcenter = xxcenter - height;
	toppix = centery - height;

	x2 = (frac >> FRACBITS) + xcenter;

	for (i = shape->leftpix; i <= shape->rightpix; i++)
	{
		//
		// calculate edges of the shape
		//
		x1 = x2;

		if (x1 >= viewwidth)
			break;                // off the right side of the view area

		frac += fracstep;
		x2 = (frac >> FRACBITS) + xcenter;

		if (x2 < 0)
			continue;             // not into the view area

		if (x1 < 0)
			x1 = 0;               // clip left boundary

		if (x2 > viewwidth)
			x2 = viewwidth;       // clip right boundary

		while (x1 < x2)
		{
			if (wallheight[x1] < height)
			{
				linecmds = &linesrc[shape->dataofs[i - shape->leftpix]];

				ScaleLine (x1,toppix,fracstep,linesrc,linecmds,shade);
			}

			x1++;
		}
	}
}



/*
=======================
=
= SimpleScaleShape
=
= NO CLIPPING, height in pixels
=
= Draws a compiled shape at [scale] pixels high
=
= each vertical line of the shape has a pointer to segment data:
= 	end of segment pixel*2 (0 terminates line) used to patch rtl in scaler
= 	top of virtual line with segment in proper place
=	start of segment pixel*2, used to jsl into compiled scaler
=	<repeat>
=
= Setup for call
= --------------
= GC_MODE			read mode 1, write mode 2
= GC_COLORDONTCARE  set to 0, so all reads from video memory return 0xff
= GC_INDEX			pointing at GC_BITMASK
=
=======================
*/

void SimpleScaleShape (int dispx, int shapenum, unsigned xheight)
{
	int         i;
	compshape_t *shape;
	byte        *linesrc,*linecmds;
	byte        *shade = NULL;
	int         height,toppix;
	int         x1,x2,xcenter;
	fixed       frac,fracstep;

	height = xheight >> 1;

	linesrc = PM_GetSpritePage(shapenum);
	shape = (compshape_t *)linesrc;
#ifdef USE_SHADING
	shade = GetShade(dispheight,FL_FULLBRIGHT);
#endif
	fracstep =((int64_t)height << FRACBITS) / (int64_t)TEXTURESIZE/2;
	frac = shape->leftpix * fracstep;

	xcenter = dispx - height;
	toppix = centery - height;

	x2 = (frac >> FRACBITS) + xcenter;

	for (i = shape->leftpix; i <= shape->rightpix; i++)
	{
		//
		// calculate edges of the shape
		//
		x1 = x2;

		frac += fracstep;
		x2 = (frac >> FRACBITS) + xcenter;

		while (x1 < x2)
		{
			linecmds = &linesrc[shape->dataofs[i - shape->leftpix]];

			ScaleLine (x1,toppix,fracstep,linesrc,linecmds,shade);

			x1++;
		}
	}
}




//
// bit mask tables for drawing scaled strips up to eight pixels wide
//
// down here so the STUPID inline assembler doesn't get confused!
//


byte	mapmasks1[4][8] = {
{1 ,3 ,7 ,15,15,15,15,15},
{2 ,6 ,14,14,14,14,14,14},
{4 ,12,12,12,12,12,12,12},
{8 ,8 ,8 ,8 ,8 ,8 ,8 ,8} };

byte	mapmasks2[4][8] = {
{0 ,0 ,0 ,0 ,1 ,3 ,7 ,15},
{0 ,0 ,0 ,1 ,3 ,7 ,15,15},
{0 ,0 ,1 ,3 ,7 ,15,15,15},
{0 ,1 ,3 ,7 ,15,15,15,15} };

byte	mapmasks3[4][8] = {
{0 ,0 ,0 ,0 ,0 ,0 ,0 ,0},
{0 ,0 ,0 ,0 ,0 ,0 ,0 ,1},
{0 ,0 ,0 ,0 ,0 ,0 ,1 ,3},
{0 ,0 ,0 ,0 ,0 ,1 ,3 ,7} };


unsigned	wordmasks[8][8] = {
{0x0080,0x00c0,0x00e0,0x00f0,0x00f8,0x00fc,0x00fe,0x00ff},
{0x0040,0x0060,0x0070,0x0078,0x007c,0x007e,0x007f,0x807f},
{0x0020,0x0030,0x0038,0x003c,0x003e,0x003f,0x803f,0xc03f},
{0x0010,0x0018,0x001c,0x001e,0x001f,0x801f,0xc01f,0xe01f},
{0x0008,0x000c,0x000e,0x000f,0x800f,0xc00f,0xe00f,0xf00f},
{0x0004,0x0006,0x0007,0x8007,0xc007,0xe007,0xf007,0xf807},
{0x0002,0x0003,0x8003,0xc003,0xe003,0xf003,0xf803,0xfc03},
{0x0001,0x8001,0xc001,0xe001,0xf001,0xf801,0xfc01,0xfe01} };

int			slinex,slinewidth;
unsigned	*linecmds;
long		linescale;
unsigned	maskword;
