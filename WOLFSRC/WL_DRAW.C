// WL_DRAW.C

#include "WL_DEF.H"
#pragma hdrstop

//#define DEBUGWALLS
//#define DEBUGTICS

/*
=============================================================================

						 LOCAL CONSTANTS

=============================================================================
*/

// the door is the last picture before the sprites
#define DOORWALL	(PMSpriteStart-8)

#define ACTORSIZE	0x4000

/*
=============================================================================

						 GLOBAL VARIABLES

=============================================================================
*/


#ifdef DEBUGWALLS
unsigned screenloc[3]= {0,0,0};
#else
unsigned screenloc[3]= {PAGE1START,PAGE2START,PAGE3START};
#endif
unsigned freelatch = FREESTART;

int32_t	lasttimecount;
long 	frameon;

unsigned	wallheight[MAXVIEWWIDTH];

fixed	tileglobal	= TILEGLOBAL;
fixed	mindist		= MINDIST;
byte    *vbuf;


//
// math tables
//
int			pixelangle[MAXVIEWWIDTH];
int32_t		finetangent[FINEANGLES/4];
fixed 		sintable[ANGLES+ANGLES/4],*costable = sintable+(ANGLES/4);

//
// refresh variables
//
fixed	viewx,viewy;			// the focal point
int		viewangle;
fixed	viewsin,viewcos;



fixed	FixedByFrac (fixed a, fixed b);
void	TransformActor (objtype *ob);
void	BuildTables (void);
void	ClearScreen (void);
int		CalcRotate (objtype *ob);
void	DrawScaleds (void);
void	CalcTics (void);
void	FixOfs (void);
void	ThreeDRefresh (void);



//
// wall optimization variables
//
int		lastside;		// true for vertical
long	lastintercept;
int		lasttilehit;


//
// ray tracing variables
//
int			focaltx,focalty,viewtx,viewty;

short		midangle,angle;
short		xpartial,ypartial;
longword	xpartialup,xpartialdown,ypartialup,ypartialdown;
fixed		xinttile,yinttile;

word		tilehit;
int			pixx;

short		xtile,ytile;
short		xtilestep,ytilestep;
fixed		xintercept,yintercept;
fixed		xstep,ystep;
word		texdelta;

word	horizwall[MAXWALLTILES],vertwall[MAXWALLTILES];


/*
=============================================================================

						 LOCAL VARIABLES

=============================================================================
*/


void AsmRefresh (void) {}	// in WL_DR_A.ASM

/*
============================================================================

			   3 - D  DEFINITIONS

============================================================================
*/


//==========================================================================


/*
========================
=
= FixedByFrac
=
= multiply a 16/16 bit, 2's complement fixed point number by a 16 bit
= fraction, passed as a signed magnitude 32 bit number
=
========================
*/

#pragma warn -rvl			// I stick the return value in with ASMs

#define FIXED_SHIFT 16
#define FIXED_ONE (1 << FIXED_SHIFT)
fixed FixedByFrac (fixed a, fixed b)
{
	int64_t result = (int64_t)a * (int64_t)b;
	return (fixed)(result >> FIXED_SHIFT);
}

#pragma warn +rvl

//==========================================================================

/*
========================
=
= TransformActor
=
= Takes paramaters:
=   gx,gy		: globalx/globaly of point
=
= globals:
=   viewx,viewy		: point of view
=   viewcos,viewsin	: sin/cos of viewangle
=   scale		: conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
========================
*/


//
// transform actor
//
void TransformActor (objtype *ob)
{
	int ratio;
	fixed gx,gy,gxt,gyt,nx,ny;
	long	temp;

//
// translate point to view centered coordinates
//
	gx = ob->x-viewx;
	gy = ob->y-viewy;

//
// calculate newx
//
	gxt = FixedByFrac(gx,viewcos);
	gyt = FixedByFrac(gy,viewsin);
	nx = gxt-gyt-ACTORSIZE;		// fudge the shape forward a bit, because
								// the midpoint could put parts of the shape
								// into an adjacent wall

//
// calculate newy
//
	gxt = FixedByFrac(gx,viewsin);
	gyt = FixedByFrac(gy,viewcos);
	ny = gyt+gxt;

//
// calculate perspective ratio
//
	ob->transx = nx;
	ob->transy = ny;

	if (nx<mindist)			// too close, don't overflow the divide
	{
	  ob->viewheight = 0;
	  return;
	}

	ob->viewx = centerx + ny*scale/nx;	// DEBUG: use assembly divide

//
// calculate height (heightnumerator/(nx>>8))
//
	ob->viewheight = (word)(heightnumerator/(nx>>8));
}

//==========================================================================

/*
========================
=
= TransformTile
=
= Takes paramaters:
=   tx,ty		: tile the object is centered in
=
= globals:
=   viewx,viewy		: point of view
=   viewcos,viewsin	: sin/cos of viewangle
=   scale		: conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
= Returns true if the tile is withing getting distance
=
========================
*/

bool TransformTile (int tx, int ty, int *dispx, int *dispheight)
{
	int ratio;
	fixed gx,gy,gxt,gyt,nx,ny;
	long	temp;

//
// translate point to view centered coordinates
//
	gx = ((long)tx<<TILESHIFT)+0x8000-viewx;
	gy = ((long)ty<<TILESHIFT)+0x8000-viewy;

//
// calculate newx
//
	gxt = FixedByFrac(gx,viewcos);
	gyt = FixedByFrac(gy,viewsin);
	nx = gxt-gyt-0x2000;		// 0x2000 is size of object

//
// calculate newy
//
	gxt = FixedByFrac(gx,viewsin);
	gyt = FixedByFrac(gy,viewcos);
	ny = gyt+gxt;


//
// calculate perspective ratio
//
	if (nx<mindist)			// too close, don't overflow the divide
	{
		*dispheight = 0;
		return false;
	}

	*dispx = centerx + ny*scale/nx;	// DEBUG: use assembly divide

//
// calculate height (heightnumerator/(nx>>8))
//
	if (nx<MINDIST)                 // too close, don't overflow the divide
		*dispheight = 0;
	else
	{
		*dispx = (short)(centerx + ny*scale/nx);
		*dispheight = (short)(heightnumerator/(nx>>8));
	}

//
// see if it should be grabbed
//
	if (nx<TILEGLOBAL && ny>-TILEGLOBAL/2 && ny<TILEGLOBAL/2)
		return true;
	else
		return false;
}

//==========================================================================

/*
====================
=
= CalcHeight
=
= Calculates the height of xintercept,yintercept from viewx,viewy
=
====================
*/

#pragma warn -rvl			// I stick the return value in with ASMs

int	CalcHeight (void)
{
	int	transheight;
	int ratio;
	fixed gxt,gyt,nx,ny;
	long	gx,gy;

	gx = xintercept-viewx;
	gxt = FixedByFrac(gx,viewcos);

	gy = yintercept-viewy;
	gyt = FixedByFrac(gy,viewsin);

	nx = gxt-gyt;

  //
  // calculate perspective ratio (heightnumerator/(nx>>8))
  //
	if (nx<mindist)
		nx=mindist;			// don't let divide overflow

	return (int16_t)(heightnumerator / (nx >> 8));
}


//==========================================================================

/*
===================
=
= ScalePost
=
===================
*/

long		postsource;
unsigned	postx;
unsigned	postwidth;

void ScalePost (void)		// VGA version
{
	int ywcount, yoffs, yw, yd, yendoffs;
	byte col;

#ifdef USE_SKYWALLPARALLAX
	if (tilehit == 16)
	{
		ScaleSkyPost();
		return;
	}
#endif

#ifdef USE_SHADING
	byte *shade = GetShade(wallheight[postx],0);
#endif

	ywcount = yd = wallheight[postx] >> 3;
	if(yd <= 0) yd = 100;

	yoffs = (centery - ywcount) * bufferPitch;
	if(yoffs < 0) yoffs = 0;
	yoffs += postx;

	yendoffs = centery + ywcount - 1;
	yw=TEXTURESIZE-1;

	while(yendoffs >= viewheight)
	{
		ywcount -= TEXTURESIZE/2;
		while(ywcount <= 0)
		{
			ywcount += yd;
			yw--;
		}
		yendoffs--;
	}
	if(yw < 0) return;

#ifdef USE_SHADING
	col = shade[postsource[yw]];
#else
	col = postsource;
#endif
	yendoffs = yendoffs * bufferPitch + postx;
	while(yoffs <= yendoffs)
	{
		vbuf[yendoffs] = col;
		ywcount -= TEXTURESIZE/2;
		if(ywcount <= 0)
		{
			do
			{
				ywcount += yd;
				yw--;
			}
			while(ywcount <= 0);
			if(yw < 0) break;
#ifdef USE_SHADING
			col = shade[postsource[yw]];
#else
			col = postsource;
#endif
		}
		yendoffs -= bufferPitch;
	}
}

void  FarScalePost (void)				// just so other files can call
{
	ScalePost ();
}


/*
====================
=
= HitVertWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitVertWall (void)
{
	int			wallpic;
	unsigned	texture;

	texture = (yintercept>>4)&0xfc0;
	if (xtilestep == -1)
	{
		texture = 0xfc0-texture;
		xintercept += TILEGLOBAL;
	}
	wallheight[pixx] = CalcHeight();

	if (lastside==1 && lastintercept == xtile && lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == (unsigned)postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lastside = true;
		lastintercept = xtile;

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		if (tilehit & 0x40)
		{								// check for adjacent doors
			ytile = yintercept>>TILESHIFT;
			if ( tilemap[xtile-xtilestep][ytile]&0x80 )
				wallpic = DOORWALL+3;
			else
				wallpic = vertwall[tilehit & ~0x40];
		}
		else
			wallpic = vertwall[tilehit];

		// *( ((unsigned *)&postsource)+1) = (unsigned)PM_GetPage(wallpic);
		postsource = texture;

	}
}


/*
====================
=
= HitHorizWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitHorizWall (void)
{
	int			wallpic;
	unsigned	texture;

	texture = (xintercept>>4)&0xfc0;
	if (ytilestep == -1)
		yintercept += TILEGLOBAL;
	else
		texture = 0xfc0-texture;
	wallheight[pixx] = CalcHeight();

	if (lastside==0 && lastintercept == ytile && lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == (unsigned)postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lastside = 0;
		lastintercept = ytile;

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		if (tilehit & 0x40)
		{								// check for adjacent doors
			xtile = xintercept>>TILESHIFT;
			if ( tilemap[xtile][ytile-ytilestep]&0x80 )
				wallpic = DOORWALL+2;
			else
				wallpic = horizwall[tilehit & ~0x40];
		}
		else
			wallpic = horizwall[tilehit];

		// *( ((unsigned *)&postsource)+1) = (unsigned)PM_GetPage(wallpic);
		postsource = texture;
	}

}

//==========================================================================

/*
====================
=
= HitHorizDoor
=
====================
*/

void HitHorizDoor (void)
{
	unsigned	texture,doorpage,doornum;

	doornum = tilehit&0x7f;
	texture = ( (xintercept-doorposition[doornum]) >> 4) &0xfc0;

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
	// in the same door as last time, so check for optimized draw
		if (texture == (unsigned)postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();			// draw last post
	// first pixel in this door
		lastside = 2;
		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		switch (doorobjlist[doornum].lock)
		{
		case dr_normal:
			doorpage = DOORWALL;
			break;
		case dr_lock1:
		case dr_lock2:
		case dr_lock3:
		case dr_lock4:
			doorpage = DOORWALL+6;
			break;
		case dr_elevator:
			doorpage = DOORWALL+4;
			break;
		}

		// *( ((unsigned *)&postsource)+1) = (unsigned)PM_GetPage(doorpage);
		postsource = texture;
	}
}

//==========================================================================

/*
====================
=
= HitVertDoor
=
====================
*/

void HitVertDoor (void)
{
	unsigned	texture,doorpage,doornum;

	doornum = tilehit&0x7f;
	texture = ( (yintercept-doorposition[doornum]) >> 4) &0xfc0;

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
	// in the same door as last time, so check for optimized draw
		if (texture == (unsigned)postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();			// draw last post
	// first pixel in this door
		lastside = 2;
		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		switch (doorobjlist[doornum].lock)
		{
		case dr_normal:
			doorpage = DOORWALL;
			break;
		case dr_lock1:
		case dr_lock2:
		case dr_lock3:
		case dr_lock4:
			doorpage = DOORWALL+6;
			break;
		case dr_elevator:
			doorpage = DOORWALL+4;
			break;
		}

		// *( ((unsigned *)&postsource)+1) = (unsigned)PM_GetPage(doorpage+1);
		postsource = texture;
	}
}

//==========================================================================


/*
====================
=
= HitHorizPWall
=
= A pushable wall in action has been hit
=
====================
*/

void HitHorizPWall (void)
{
	int			wallpic;
	unsigned	texture,offset;

	texture = (xintercept>>4)&0xfc0;
	offset = pwallpos<<10;
	if (ytilestep == -1)
		yintercept += TILEGLOBAL-offset;
	else
	{
		texture = 0xfc0-texture;
		yintercept += offset;
	}

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == (unsigned)postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		wallpic = horizwall[tilehit&63];

		// *( ((unsigned *)&postsource)+1) = (unsigned)PM_GetPage(wallpic);
		postsource = texture;
	}

}


/*
====================
=
= HitVertPWall
=
= A pushable wall in action has been hit
=
====================
*/

void HitVertPWall (void)
{
	int			wallpic;
	unsigned	texture,offset;

	texture = (yintercept>>4)&0xfc0;
	offset = pwallpos<<10;
	if (xtilestep == -1)
	{
		xintercept += TILEGLOBAL-offset;
		texture = 0xfc0-texture;
	}
	else
		xintercept += offset;

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == (unsigned)postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		wallpic = vertwall[tilehit&63];

		// *( ((unsigned *)&postsource)+1) = (unsigned)PM_GetPage(wallpic);
		postsource = texture;
	}

}

//==========================================================================

//==========================================================================

#if 0
/*
=====================
=
= ClearScreen
=
=====================
*/

void ClearScreen (void)
{
 unsigned floor=egaFloor[gamestate.episode*10+mapon],
	  ceiling=egaCeiling[gamestate.episode*10+mapon];

  //
  // clear the screen
  //
asm	mov	dx,GC_INDEX
asm	mov	ax,GC_MODE + 256*2		// read mode 0, write mode 2
asm	out	dx,ax
asm	mov	ax,GC_BITMASK + 255*256
asm	out	dx,ax

asm	mov	dx,40
asm	mov	ax,[viewwidth]
asm	shr	ax,3
asm	sub	dx,ax					// dx = 40-viewwidth/8

asm	mov	bx,[viewwidth]
asm	shr	bx,4					// bl = viewwidth/16
asm	mov	bh,BYTE PTR [viewheight]
asm	shr	bh,1					// half height

asm	mov	ax,[ceiling]
asm	mov	es,[screenseg]
asm	mov	di,[bufferofs]

toploop:
asm	mov	cl,bl
asm	rep	stosw
asm	add	di,dx
asm	dec	bh
asm	jnz	toploop

asm	mov	bh,BYTE PTR [viewheight]
asm	shr	bh,1					// half height
asm	mov	ax,[floor]

bottomloop:
asm	mov	cl,bl
asm	rep	stosw
asm	add	di,dx
asm	dec	bh
asm	jnz	bottomloop


asm	mov	dx,GC_INDEX
asm	mov	ax,GC_MODE + 256*10		// read mode 1, write mode 2
asm	out	dx,ax
asm	mov	al,GC_BITMASK
asm	out	dx,al

}
#endif
//==========================================================================

byte vgaCeiling[]=
{
#ifndef SPEAR
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0xbfbf,
 0x4e4e,0x4e4e,0x4e4e,0x1d1d,0x8d8d,0x4e4e,0x1d1d,0x2d2d,0x1d1d,0x8d8d,
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x2d2d,0xdddd,0x1d1d,0x1d1d,0x9898,

 0x1d1d,0x9d9d,0x2d2d,0xdddd,0xdddd,0x9d9d,0x2d2d,0x4d4d,0x1d1d,0xdddd,
 0x7d7d,0x1d1d,0x2d2d,0x2d2d,0xdddd,0xd7d7,0x1d1d,0x1d1d,0x1d1d,0x2d2d,
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0xdddd,0xdddd,0x7d7d,0xdddd,0xdddd,0xdddd
#else
 0x6f6f,0x4f4f,0x1d1d,0xdede,0xdfdf,0x2e2e,0x7f7f,0x9e9e,0xaeae,0x7f7f,
 0x1d1d,0xdede,0xdfdf,0xdede,0xdfdf,0xdede,0xe1e1,0xdcdc,0x2e2e,0x1d1d,0xdcdc
#endif
};

/*
=====================
=
= VGAClearScreen
=
=====================
*/

void VGAClearScreen (void)
{
	byte ceiling = vgaCeiling[(gamestate.episode * 10) + gamestate.mapon];

	int y;
	byte *src,*dest = vbuf;
#ifdef USE_SHADING
	for (y = 0; y < centery; y++, dest += bufferPitch)
	{
		src = GetShade((centery - y) << 3,0);

		memset (dest,src[ceiling],viewwidth);
	}

	for (; y < viewheight; y++, dest += bufferPitch)
	{
		src = GetShade((y - centery) << 3,0);

		memset (dest,src[0x19],viewwidth);
	}
#else
	for (y = 0; y < centery; y++, dest += bufferPitch)
		memset (dest,ceiling,viewwidth);

	for (; y < viewheight; y++, dest += bufferPitch)
		memset (dest,0x19,viewwidth);
#endif
}

//==========================================================================

/*
=====================
=
= CalcRotate
=
=====================
*/

int	CalcRotate (objtype *ob)
{
	int	angle,viewangle;

	// this isn't exactly correct, as it should vary by a trig value,
	// but it is close enough with only eight rotations

	viewangle = player->angle + (centerx - ob->viewx)/8;

	if (ob->obclass == rocketobj || ob->obclass == hrocketobj)
		angle =  (viewangle-180)- ob->angle;
	else
		angle =  (viewangle-180)- dirangle[ob->dir];

	angle+=ANGLES/16;
	while (angle>=ANGLES)
		angle-=ANGLES;
	while (angle<0)
		angle+=ANGLES;

	if (ob->state->rotate == 2)             // 2 rotation pain frame
		return 4*(angle/(ANGLES/2));        // seperated by 3 (art layout...)

	return angle/(ANGLES/8);
}


/*
=====================
=
= DrawScaleds
=
= Draws all objects that are visable
=
=====================
*/

#define MAXVISABLE	50

typedef struct
{
	byte	viewx,
			viewheight,
			shapenum;
} visobj_t;

visobj_t	vislist[MAXVISABLE],*visptr,*visstep,*farthest;

void DrawScaleds (void)
{
	int 		i,j,least,numvisable,height;
	memptr		shape;
	byte		*tilespot,*visspot;
	int			shapenum;
	unsigned	spotloc;

	statobj_t	*statptr;
	objtype		*obj;

	visptr = &vislist[0];

//
// place static objects
//
	for (statptr = &statobjlist[0] ; statptr !=laststatobj ; statptr++)
	{
		if ((visptr->shapenum = statptr->shapenum) == -1)
			continue;						// object has been deleted

		if (!*statptr->visspot)
			continue;						// not visable

		if (TransformTile (statptr->tilex,statptr->tiley
			,&visptr->viewx,&visptr->viewheight) && statptr->flags & FL_BONUS)
		{
			GetBonus (statptr);
			continue;
		}

		if (!visptr->viewheight)
			continue;						// to close to the object

		if (visptr < &vislist[MAXVISABLE-1])	// don't let it overflow
			visptr++;
	}

//
// place active objects
//
	for (obj = player->next;obj;obj=obj->next)
	{
		if (!(visptr->shapenum = obj->state->shapenum))
			continue;						// no shape

		spotloc = (obj->tilex<<6)+obj->tiley;	// optimize: keep in struct?
		visspot = &spotvis[0][0]+spotloc;
		tilespot = &tilemap[0][0]+spotloc;

		//
		// could be in any of the nine surrounding tiles
		//
		if (*visspot
		|| ( *(visspot-1) && !*(tilespot-1) )
		|| ( *(visspot+1) && !*(tilespot+1) )
		|| ( *(visspot-65) && !*(tilespot-65) )
		|| ( *(visspot-64) && !*(tilespot-64) )
		|| ( *(visspot-63) && !*(tilespot-63) )
		|| ( *(visspot+65) && !*(tilespot+65) )
		|| ( *(visspot+64) && !*(tilespot+64) )
		|| ( *(visspot+63) && !*(tilespot+63) ) )
		{
			obj->active = ac_yes;
			TransformActor (obj);
			if (!obj->viewheight)
				continue;						// too close or far away

			visptr->viewx = obj->viewx;
			visptr->viewheight = obj->viewheight;
			if (visptr->shapenum == -1)
				visptr->shapenum = obj->temp1;	// special shape

			if (obj->state->rotate)
				visptr->shapenum += CalcRotate (obj);

			if (visptr < &vislist[MAXVISABLE-1])	// don't let it overflow
				visptr++;
			obj->flags |= FL_VISABLE;
		}
		else
			obj->flags &= ~FL_VISABLE;
	}

//
// draw from back to front
//
	numvisable = visptr-&vislist[0];

	if (!numvisable)
		return;									// no visable objects

	for (i = 0; i<numvisable; i++)
	{
		least = 32000;
		for (visstep=&vislist[0] ; visstep<visptr ; visstep++)
		{
			height = visstep->viewheight;
			if (height < least)
			{
				least = height;
				farthest = visstep;
			}
		}
		//
		// draw farthest
		//
		ScaleShape(farthest->viewx,farthest->shapenum,farthest->viewheight);

		farthest->viewheight = 32000;
	}

}

//==========================================================================

/*
==============
=
= DrawPlayerWeapon
=
= Draw the player's hands
=
==============
*/

int	weaponscale[NUMWEAPONS] = {SPR_KNIFEREADY,SPR_PISTOLREADY
	,SPR_MACHINEGUNREADY,SPR_CHAINREADY};

void DrawPlayerWeapon (void)
{
	int	shapenum;

#ifndef SPEAR
	if (gamestate.victoryflag)
	{
		if (player->state == &s_deathcam && (TimeCount&32) )
			SimpleScaleShape(viewwidth/2,SPR_DEATHCAM,viewheight+1);
		return;
	}
#endif

	if (gamestate.weapon != -1)
	{
		shapenum = weaponscale[gamestate.weapon]+gamestate.weaponframe;
		SimpleScaleShape(viewwidth/2,shapenum,viewheight+1);
	}

	if (demorecord || demoplayback)
		SimpleScaleShape(viewwidth/2,SPR_DEMO,viewheight+1);
}


//==========================================================================


/*
=====================
=
= CalcTics
=
=====================
*/

void CalcTics (void)
{
	long	newtime,oldtimecount;

//
// calculate tics since last refresh for adaptive timing
//
	if (lasttimecount > TimeCount)
		TimeCount = lasttimecount;		// if the game was paused a LONG time

	do
	{
		newtime = TimeCount;
		tics = newtime-lasttimecount;
	} while (!tics);			// make sure at least one tic passes

	lasttimecount = newtime;

#ifdef FILEPROFILE
		strcpy (scratch,"\tTics:");
		itoa (tics,str,10);
		strcat (scratch,str);
		strcat (scratch,"\n");
		write (profilehandle,scratch,strlen(scratch));
#endif

	if (tics>MAXTICS)
	{
		TimeCount -= (tics-MAXTICS);
		tics = MAXTICS;
	}
}


//==========================================================================


/*
========================
=
= FixOfs
=
========================
*/

void	FixOfs (void)
{
	VW_ScreenToScreen (displayofs,bufferofs,viewwidth/8,viewheight);
}


//==========================================================================

#define FRACBITS         16

fixed FixedMul (fixed a, fixed b)
{
	return (fixed)(((int64_t)a * b + 0x8000) >> FRACBITS);
}

/*
====================
=
= WallRefresh
=
====================
*/

void WallRefresh (void)
{
    int16_t   angle;
    int32_t   xstep,ystep;
    fixed     xinttemp,yinttemp;                            // holds temporary intercept position
    longword  xpartial,ypartial;
    doorobj_t *door;
    int       pwallposnorm,pwallposinv,pwallposi;           // holds modified pwallpos

    for (pixx = 0; pixx < viewwidth; pixx++)
    {
        //
        // setup to trace a ray through pixx view pixel
        //
        angle = midangle + pixelangle[pixx];                // delta for this pixel

        if (angle < 0)                                      // -90 - -1 degree arc
            angle += ANG360;                                // -90 is the same as 270
        if (angle >= ANG360)                                // 360-449 degree arc
            angle -= ANG360;                                // -449 is the same as 89

        //
        // setup xstep/ystep based on angle
        //
        if (angle < ANG90)                                  // 0-89 degree arc
        {
            xtilestep = 1;
            ytilestep = -1;
            xstep = finetangent[ANG90 - 1 - angle];
            ystep = -finetangent[angle];
            xpartial = xpartialup;
            ypartial = ypartialdown;
        }
        else if (angle < ANG180)                            // 90-179 degree arc
        {
            xtilestep = -1;
            ytilestep = -1;
            xstep = -finetangent[angle - ANG90];
            ystep = -finetangent[ANG180 - 1 - angle];
            xpartial = xpartialdown;
            ypartial = ypartialdown;
        }
        else if (angle < ANG270)                            // 180-269 degree arc
        {
            xtilestep = -1;
            ytilestep = 1;
            xstep = -finetangent[ANG270 - 1 - angle];
            ystep = finetangent[angle - ANG180];
            xpartial = xpartialdown;
            ypartial = ypartialup;
        }
        else if (angle < ANG360)                            // 270-359 degree arc
        {
            xtilestep = 1;
            ytilestep = 1;
            xstep = finetangent[angle - ANG270];
            ystep = finetangent[ANG360 - 1 - angle];
            xpartial = xpartialup;
            ypartial = ypartialup;
        }

        //
        // initialise variables for intersection testing
        //
        yintercept = FixedMul(ystep,xpartial) + viewy;
        yinttile = yintercept >> TILESHIFT;
        xtile = focaltx + xtilestep;

        xintercept = FixedMul(xstep,ypartial) + viewx;
        xinttile = xintercept >> TILESHIFT;
        ytile = focalty + ytilestep;

        texdelta = 0;

        //
        // special treatment when player is in back tile of pushwall
        //
        if (tilemap[focaltx][focalty] == BIT_WALL)
        {
            if ((pwalldir == di_east && xtilestep == 1) || (pwalldir == di_west && xtilestep == -1))
            {
                yinttemp = yintercept - ((ystep * (64 - pwallpos)) >> 6);

                //
                //  trace hit vertical pushwall back?
                //
                if (yinttemp >> TILESHIFT == focalty)
                {
                    if (pwalldir == di_east)
                        xintercept = (focaltx << TILESHIFT) + (pwallpos << 10);
                    else
                        xintercept = ((focaltx << TILESHIFT) - TILEGLOBAL) + ((64 - pwallpos) << 10);

                    yintercept = yinttemp;
                    yinttile = yintercept >> TILESHIFT;
                    tilehit = pwalltile;
                    HitVertWall();
                    continue;
                }
            }
            else if ((pwalldir == di_south && ytilestep == 1) || (pwalldir == di_north && ytilestep == -1))
            {
                xinttemp = xintercept - ((xstep * (64 - pwallpos)) >> 6);

                //
                // trace hit horizontal pushwall back?
                //
                if (xinttemp >> TILESHIFT == focaltx)
                {
                    if (pwalldir == di_south)
                        yintercept = (focalty << TILESHIFT) + (pwallpos << 10);
                    else
                        yintercept = ((focalty << TILESHIFT) - TILEGLOBAL) + ((64 - pwallpos) << 10);

                    xintercept = xinttemp;
                    xinttile = xintercept >> TILESHIFT;
                    tilehit = pwalltile;
                    HitHorizWall();
                    continue;
                }
            }
        }

//
// trace along this angle until we hit a wall
//
// CORE LOOP!
//
        while (1)
        {
            //
            // check intersections with vertical walls
            //
            if ((xtile - xtilestep) == xinttile && (ytile - ytilestep) == yinttile)
                yinttile = ytile;

            if ((ytilestep == -1 && yinttile <= ytile) || (ytilestep == 1 && yinttile >= ytile))
                goto horizentry;
vertentry:
#ifdef REVEALMAP
            mapseen[xtile][yinttile] = true;
#endif
            tilehit = tilemap[xtile][yinttile];

            if (tilehit)
            {
                if (tilehit & BIT_DOOR)
                {
                    //
                    // hit a vertical door, so find which coordinate the door would be
                    // intersected at, and check to see if the door is open past that point
                    //
                    door = &doorobjlist[tilehit & ~BIT_DOOR];

                    if (door->action == dr_open)
                        goto passvert;                       // door is open, continue tracing

                    yinttemp = yintercept + (ystep >> 1);    // add halfstep to current intercept position

                    //
                    // midpoint is outside tile, so it hit the side of the wall before a door
                    //
                    if (yinttemp >> TILESHIFT != yinttile)
                        goto passvert;

                    if (door->action != dr_closed)
                    {
                        //
                        // the trace hit the door plane at pixel position yintercept, see if the door is
                        // closed that much
                        //
                        if ((word)yinttemp < door->position)
                            goto passvert;
                    }

                    yintercept = yinttemp;
                    xintercept = ((fixed)xtile << TILESHIFT) + (TILEGLOBAL/2);

                    HitVertDoor();
                }
                else if (tilehit == BIT_WALL)
                {
                    //
                    // hit a sliding vertical wall
                    //
                    if (pwalldir == di_west || pwalldir == di_east)
                    {
                        if (pwalldir == di_west)
                        {
                            pwallposnorm = 64 - pwallpos;
                            pwallposinv = pwallpos;
                        }
                        else
                        {
                            pwallposnorm = pwallpos;
                            pwallposinv = 64 - pwallpos;
                        }

                        if ((pwalldir == di_east && xtile == pwallx && yinttile == pwally)
                         || (pwalldir == di_west && !(xtile == pwallx && yinttile == pwally)))
                        {
                            yinttemp = yintercept + ((ystep * pwallposnorm) >> 6);

                            if (yinttemp >> TILESHIFT != yinttile)
                                goto passvert;

                            yintercept = yinttemp;
                            xintercept = (((fixed)xtile << TILESHIFT) + TILEGLOBAL) - (pwallposinv << 10);
                            yinttile = yintercept >> TILESHIFT;
                            tilehit = pwalltile;

                            HitVertWall();
                        }
                        else
                        {
                            yinttemp = yintercept + ((ystep * pwallposinv) >> 6);

                            if (yinttemp >> TILESHIFT != yinttile)
                                goto passvert;

                            yintercept = yinttemp;
                            xintercept = ((fixed)xtile << TILESHIFT) - (pwallposinv << 10);
                            yinttile = yintercept >> TILESHIFT;
                            tilehit = pwalltile;

                            HitVertWall();
                        }
                    }
                    else
                    {
                        if (pwalldir == di_north)
                            pwallposi = 64 - pwallpos;
                        else
                            pwallposi = pwallpos;

                        if ((pwalldir == di_south && (word)yintercept < (pwallposi << 10))
                         || (pwalldir == di_north && (word)yintercept > (pwallposi << 10)))
                        {
                            if (xtile == pwallx && yinttile == pwally)
                            {
                                if ((pwalldir == di_south && (int32_t)((word)yintercept) + ystep < (pwallposi << 10))
                                 || (pwalldir == di_north && (int32_t)((word)yintercept) + ystep > (pwallposi << 10)))
                                    goto passvert;

                                //
                                // set up a horizontal intercept position
                                //
                                if (pwalldir == di_south)
                                    yintercept = (yinttile << TILESHIFT) + (pwallposi << 10);
                                else
                                    yintercept = ((yinttile << TILESHIFT) - TILEGLOBAL) + (pwallposi << 10);

                                xintercept -= (xstep * (64 - pwallpos)) >> 6;
                                xinttile = xintercept >> TILESHIFT;
                                tilehit = pwalltile;

                                HitHorizWall();
                            }
                            else
                            {
                                texdelta = pwallposi << 10;
                                xintercept = (fixed)xtile << TILESHIFT;
                                tilehit = pwalltile;

                                HitVertWall();
                            }
                        }
                        else
                        {
                            if (xtile == pwallx && yinttile == pwally)
                            {
                                texdelta = pwallposi << 10;
                                xintercept = (fixed)xtile << TILESHIFT;
                                tilehit = pwalltile;

                                HitVertWall();
                            }
                            else
                            {
                                if ((pwalldir == di_south && (int32_t)((word)yintercept) + ystep > (pwallposi << 10))
                                 || (pwalldir == di_north && (int32_t)((word)yintercept) + ystep < (pwallposi << 10)))
                                    goto passvert;

                                //
                                // set up a horizontal intercept position
                                //
                                if (pwalldir == di_south)
                                    yintercept = (yinttile << TILESHIFT) - ((64 - pwallpos) << 10);
                                else
                                    yintercept = (yinttile << TILESHIFT) + ((64 - pwallpos) << 10);

                                xintercept -= (xstep * pwallpos) >> 6;
                                xinttile = xintercept >> TILESHIFT;
                                tilehit = pwalltile;

                                HitHorizWall();
                            }
                        }
                    }
                }
                else
                {
                    xintercept = (fixed)xtile << TILESHIFT;

                    HitVertWall();
                }

                break;
            }

passvert:
            //
            // mark the tile as visible and setup for next step
            //
            spotvis[xtile][yinttile] = true;
            xtile += xtilestep;
            yintercept += ystep;
            yinttile = yintercept >> TILESHIFT;
        }

        continue;

        while (1)
        {
            //
            // check intersections with horizontal walls
            //
            if ((xtile - xtilestep) == xinttile && (ytile - ytilestep) == yinttile)
                xinttile = xtile;

            if ((xtilestep == -1 && xinttile <= xtile) || (xtilestep == 1 && xinttile >= xtile))
                goto vertentry;

horizentry:
#ifdef REVEALMAP
            mapseen[xinttile][ytile] = true;
#endif
            tilehit = tilemap[xinttile][ytile];

            if (tilehit)
            {
                if (tilehit & BIT_DOOR)
                {
                    //
                    // hit a horizontal door, so find which coordinate the door would be
                    // intersected at, and check to see if the door is open past that point
                    //
                    door = &doorobjlist[tilehit & ~BIT_DOOR];

                    if (door->action == dr_open)
                        goto passhoriz;                      // door is open, continue tracing

                    xinttemp = xintercept + (xstep >> 1);    // add half step to current intercept position

                    //
                    // midpoint is outside tile, so it hit the side of the wall before a door
                    //
                    if (xinttemp >> TILESHIFT != xinttile)
                        goto passhoriz;

                    if (door->action != dr_closed)
                    {
                        //
                        // the trace hit the door plane at pixel position xintercept, see if the door is
                        // closed that much
                        //
                        if ((word)xinttemp < door->position)
                            goto passhoriz;
                    }

                    xintercept = xinttemp;
                    yintercept = ((fixed)ytile << TILESHIFT) + (TILEGLOBAL/2);

                    HitHorizDoor();
                }
                else if (tilehit == BIT_WALL)
                {
                    //
                    // hit a sliding horizontal wall
                    //
                    if (pwalldir == di_north || pwalldir == di_south)
                    {
                        if (pwalldir == di_north)
                        {
                            pwallposnorm = 64 - pwallpos;
                            pwallposinv = pwallpos;
                        }
                        else
                        {
                            pwallposnorm = pwallpos;
                            pwallposinv = 64 - pwallpos;
                        }

                        if ((pwalldir == di_south && xinttile == pwallx && ytile == pwally)
                         || (pwalldir == di_north && !(xinttile == pwallx && ytile == pwally)))
                        {
                            xinttemp = xintercept + ((xstep * pwallposnorm) >> 6);

                            if (xinttemp >> TILESHIFT != xinttile)
                                goto passhoriz;

                            xintercept = xinttemp;
                            yintercept = (((fixed)ytile << TILESHIFT) + TILEGLOBAL) - (pwallposinv << 10);
                            xinttile = xintercept >> TILESHIFT;
                            tilehit = pwalltile;

                            HitHorizWall();
                        }
                        else
                        {
                            xinttemp = xintercept + ((xstep * pwallposinv) >> 6);

                            if (xinttemp >> TILESHIFT != xinttile)
                                goto passhoriz;

                            xintercept = xinttemp;
                            yintercept = ((fixed)ytile << TILESHIFT) - (pwallposinv << 10);
                            xinttile = xintercept >> TILESHIFT;
                            tilehit = pwalltile;

                            HitHorizWall();
                        }
                    }
                    else
                    {
                        if (pwalldir == di_west)
                            pwallposi = 64 - pwallpos;
                        else
                            pwallposi = pwallpos;

                        if ((pwalldir == di_east && (word)xintercept < (pwallposi << 10))
                         || (pwalldir == di_west && (word)xintercept > (pwallposi << 10)))
                        {
                            if (xinttile == pwallx && ytile == pwally)
                            {
                                if ((pwalldir == di_east && (int32_t)((word)xintercept) + xstep < (pwallposi << 10))
                                 || (pwalldir == di_west && (int32_t)((word)xintercept) + xstep > (pwallposi << 10)))
                                    goto passhoriz;

                                //
                                // set up a vertical intercept position
                                //
                                yintercept -= (ystep * (64 - pwallpos)) >> 6;
                                yinttile = yintercept >> TILESHIFT;

                                if (pwalldir == di_east)
                                    xintercept = (xinttile << TILESHIFT) + (pwallposi << 10);
                                else
                                    xintercept = ((xinttile << TILESHIFT) - TILEGLOBAL) + (pwallposi << 10);

                                tilehit = pwalltile;

                                HitVertWall();
                            }
                            else
                            {
                                texdelta = pwallposi << 10;
                                yintercept = ytile << TILESHIFT;
                                tilehit = pwalltile;

                                HitHorizWall();
                            }
                        }
                        else
                        {
                            if (xinttile == pwallx && ytile == pwally)
                            {
                                texdelta = pwallposi << 10;
                                yintercept = ytile << TILESHIFT;
                                tilehit = pwalltile;

                                HitHorizWall();
                            }
                            else
                            {
                                if ((pwalldir == di_east && (int32_t)((word)xintercept) + xstep > (pwallposi << 10))
                                 || (pwalldir == di_west && (int32_t)((word)xintercept) + xstep < (pwallposi << 10)))
                                    goto passhoriz;

                                //
                                // set up a vertical intercept position
                                //
                                yintercept -= (ystep * pwallpos) >> 6;
                                yinttile = yintercept >> TILESHIFT;

                                if (pwalldir == di_east)
                                    xintercept = (xinttile << TILESHIFT) - ((64 - pwallpos) << 10);
                                else
                                    xintercept = (xinttile << TILESHIFT) + ((64 - pwallpos) << 10);

                                tilehit = pwalltile;

                                HitVertWall();
                            }
                        }
                    }
                }
                else
                {
                    yintercept = (fixed)ytile << TILESHIFT;

                    HitHorizWall();
                }

                break;
            }

passhoriz:
            //
            // mark the tile as visible and setup for next step
            //
            spotvis[xinttile][ytile] = true;
            ytile += ytilestep;
            xintercept += xstep;
            xinttile = xintercept >> TILESHIFT;
        }
    }
}

//==========================================================================

/*
========================
=
= ThreeDRefresh
=
========================
*/

void	ThreeDRefresh (void)
{
	int tracedir;

	vbuf = VL_LockSurface(sdlScreenBuffer);
	if(vbuf == NULL) return;

	bufferofs += screenofs;
	vbuf += screenofs;

//
// follow the walls from there to the right, drawwing as we go
//
	VGAClearScreen ();

	WallRefresh ();

//
// draw all the scaled images
//
	DrawScaleds();			// draw scaled stuff
	DrawPlayerWeapon ();	// draw player's hands

	VL_UnlockSurface(sdlScreenBuffer);
	vbuf = NULL;

//
// show screen and time last cycle
//
	if (fizzlein)
	{
		FizzleFade(bufferofs,displayofs+screenofs,viewwidth,viewheight,20,false);
		fizzlein = false;

		lasttimecount = TimeCount = 0;		// don't make a big tic count
	} else {
		VW_UpdateScreen ();
	}

	bufferofs -= screenofs;
	displayofs = bufferofs;

	bufferofs += SCREENSIZE;
	if (bufferofs > PAGE3START)
		bufferofs = PAGE1START;

	frameon++;
	PM_NextFrame();
}


//===========================================================================
