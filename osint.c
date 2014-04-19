// VecXGL 1.2 (SDL/Win32 and SDL/Linux)
//
// This is a port of the vectrex emulator "vecx", by Valavan Manohararajah.
// Portions of this code copyright James Higgs 2005/2007.
// These portions are:
// 1. Ay38910 PSG (audio) emulation wave-buffering code.
// 2. Drawing of vectors using OpenGL.
//
// Comand-line parsing code gratefully borrowed from vecxsdl (Thomas Mathys).
// Key mapping and command-line options were also changed
// to be compatible with Thomas Mathys' vecxsdl.
//
// Other vecx ports by JH:
// - VecXPS2 (Playsyation 2)
// - VecXWin32 (Windows/DirectX) (unreleased)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "vecx.h"
#include "bios.h"						// bios rom data
#include "wnoise.h"						// White noise waveform
#include "overlay.h"					// overlay texture info

static const char *version = "1.2";

#define EMU_TIMER			20			// milliseconds per frame
#define DEFAULT_WIDTH		330
#define DEFAULT_HEIGHT		410
#define DEFAULT_LINEWIDTH	1.0f
#define DEFAULT_OVERLAYTRANSPARENCY	0.5f

//#define ENABLE_OVERLAY

// AY38910 emulation stuff
extern unsigned snd_regs[16];
Uint8 AY_vol[3];
Uint16 AY_spufreq[3];
Uint16 AY_noisefreq;
Uint8 AY_tone_enable[3];
Uint8 AY_noise_enable[3];
static Uint8 AY_debug = 0;

// SDL audio stuff
SDL_AudioSpec reqSpec;
SDL_AudioSpec givenSpec;
SDL_AudioSpec *usedSpec;
Uint8 *pWave;


static const char *appname = "vecx";
//static const char *romname = "rom.dat";
static const char* cartname = NULL;
static char* overlayname = NULL;

static long screen_x = DEFAULT_WIDTH;
static long screen_y = DEFAULT_HEIGHT;
static long scl_factor;

static long bytes_per_pixel;
GLfloat color_set[VECTREX_COLORS];
GLfloat line_width = DEFAULT_LINEWIDTH;
GLfloat overlay_transparency = DEFAULT_OVERLAYTRANSPARENCY;

// Global texture image info
#ifdef ENABLE_OVERLAY
TextureImage g_overlay;							// Storage For One Texture
extern int LoadTGA (char *filename);			// Loads A TGA File Into Memory
#endif

static void osint_updatescale (void)
{
	long sclx, scly;

	sclx = ALG_MAX_X / screen_x;
	scly = ALG_MAX_Y / screen_y;

	if (sclx > scly) {
		scl_factor = sclx;
	} else {
		scl_factor = scly;
	}
}

static int osint_defaults (void)
{
	unsigned b;
	//FILE *rom_file;

	screen_x = DEFAULT_WIDTH;
	screen_y = DEFAULT_HEIGHT;

	osint_updatescale ();

	/* load up the rom */
/*
	rom_file = fopen (romname, "rb");

	if (rom_file == NULL) {
		sprintf (gbuffer, "cannot open '%s'", romname);
		MessageBox (NULL, gbuffer, NULL, MB_OK);
		return 1;
	}

	b = fread (rom, 1, sizeof (rom), rom_file);

	if (b < sizeof (rom)) {
		sprintf (gbuffer, "read %d bytes from '%s'. need %d bytes.",
			b, romname, sizeof (rom));
		MessageBox (NULL, gbuffer, NULL, MB_OK);
		return 1;
	}

	fclose (rom_file);
*/
	// JH - built-in bios
	memcpy(rom, bios_data, bios_data_size);

	/* the cart is empty by default */
	for (b = 0; b < sizeof (cart); b++) {
		cart[b] = 0;
	}

	return 0;
}

// Load a custom vectrex bios rom
static void osint_load_bios(const char *filename) {

	FILE *f;

	f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "Can't open bios image (%s).\n", filename);
		exit(1);
	}

	if (sizeof(rom) != fread(rom, 1, sizeof(rom), f)) {
		fprintf(
			stderr,
			"%s is not a valid Vectrex BIOS.\n"
			"It's smaller than %d bytes.\n",
			filename, sizeof(rom)
		);
		fclose(f);
		exit(1);
	}

	fclose(f);
}

static void osint_print_usage(FILE *f) {

	fprintf(f, "Usage: vecxgl [options] [file]\n");
	fprintf(f, "Options:\n");
	fprintf(f, "  -b <file>         Load BIOS image from file\n");
	fprintf(f, "                    If the -b parameter is omitted,\n");
	fprintf(f, "                    a built-in BIOS image will be used.\n");
	fprintf(f, "  -h                Display this help\n");
	fprintf(f, "  -l <#>            Set line width (default is %d)\n", DEFAULT_LINEWIDTH);
	fprintf(f, "  -o <file>         Load overlay from file\n");
	fprintf(f, "  -t <#>            Overlay transparency (0.0 to 1.0, default is %g)\n", DEFAULT_OVERLAYTRANSPARENCY);
	//fprintf(f, "  -v <######>       Vector color (hex, 6 digits, default is %02x%02x%02x)\n", DEFAULT_VECTORCOLOR_R, DEFAULT_VECTORCOLOR_G, DEFAULT_VECTORCOLOR_B);
	fprintf(f, "  -x <xsize>        Window x size (default is %d)\n", DEFAULT_WIDTH);
	fprintf(f, "  -y <ysize>        Window y size (default is %d)\n", DEFAULT_HEIGHT);
}

// Get next argument value in the 
static char *getnextarg(int *index, int argc, char *argv[]) {

	if (*index >= argc) {
		return NULL;
	} else {
		char *result = argv[*index];
		(*index)++;
		return result;
	}
}

// Set emulator options from command line arguments
static void osint_parse_cmdline (int argc, char *argv[])
{
	// Most of this code from Thomas Mathys' vecxsdl
	int index;
	char *arg;
	int have_xres = 0;
	int have_yres = 0;

	// scan for -h first
	index = 0;
	while (arg = getnextarg(&index, argc, argv)) {
		if ( 0 == strcmp(arg, "-h") ) {
			osint_print_usage(stderr);
			exit(0);
		}
	}

	// parse other arguments
	index = 1;
	while (arg = getnextarg(&index, argc, argv)) {

		// -b
		if ( 0 == strcmp(arg, "-b") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				osint_print_usage(stderr);
				fprintf(stderr, "\nError : No filename given for -b.\n");
				exit(1);
			} else {
				//args->biosname = arg;
				osint_load_bios(arg);
			}
		}
		// -l
		else if ( 0 == strcmp(arg, "-l") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				osint_print_usage(stderr);
				fprintf(stderr, "\nError : No line width given for -l.\n");
				exit(1);
			} else {
				line_width = (float) atof(arg);
				if (line_width < 0) {
					osint_print_usage(stderr);
					fprintf(stderr, "\nError : Line width must be positive.\n");
					exit(1);
				}
			}
		}
		// -o
		else if ( 0 == strcmp(arg, "-o") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				osint_print_usage(stderr);
				fprintf(stderr, "\nError : no filename given for -o.\n");
				exit(1);
			} else {
				overlayname = arg;
			}
		}
		// -t
		else if( 0 == strcmp(arg, "-t") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				osint_print_usage(stderr);
				fprintf(stderr, "\nError : no transparency given for -t.\n");
				exit(1);
			} else {
				float v = (float) atof(arg);
				if ( (v < 0) || (v > 1) ) {
					osint_print_usage(stderr);
					fprintf(stderr, "\nError : overlay transparency must be in the range [0.0,1.0].\n");
					exit(1);
				} else {
					overlay_transparency = v;
				}
			}
		}
/*		// -v
		else if ( 0 == strcmp(arg, "-v") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				printusage(stderr);
				fprintf(stderr, "\nError : no color given for -v.\n");
				quit(1);
			} else {
				unsigned long v;
				char *c;
				v = strtoul(arg, &c, 16);
				if ( (strlen(arg) != 6) || (*c != 0) ) {
					printusage(stderr);
					fprintf(stderr, "\nError : invalid format for vector color.\n");
					quit(1);
				} else {
					args->vectorcolor[0] = (unsigned char) ((v >> 16) & 255);
					args->vectorcolor[1] = (unsigned char) ((v >> 8) & 255);
					args->vectorcolor[2] = (unsigned char) (v & 255);
				}
			}
		}
*/		// -x
		else if ( 0 == strcmp(arg, "-x") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				osint_print_usage(stderr);
				fprintf(stderr, "\nError : no window width given for -x.\n");
				exit(1);
			} else {
				screen_x = atoi(arg);
				have_xres = 1;
				if (screen_x < 0) {
					osint_print_usage(stderr);
					fprintf(stderr, "\nError : window width must be positive.\n");
					exit(1);
				}
			}
		}
		// -y
		else if ( 0 == strcmp(arg, "-y") ) {
			arg = getnextarg(&index, argc, argv);
			if (!arg) {
				osint_print_usage(stderr);
				fprintf(stderr, "\nError : no window height given for -y.\n");
				exit(1);
			} else {
				screen_y = atoi(arg);
				have_yres = 1;
				if (screen_y < 0) {
					osint_print_usage(stderr);
					fprintf(stderr, "\nError : window height must be positive.\n");
					exit(1);
				}
			}
		}
		else {
			cartname = arg;
		}
	}

	// if only x or only y given, calculate the other,
	// so that the window gets a sane aspect ratio.
	if ( (have_xres) && (!have_yres) ) {
		screen_y = screen_x*DEFAULT_HEIGHT/DEFAULT_WIDTH;
	}
	else if ( (!have_xres) && (have_yres) ) {
		screen_x = screen_y*DEFAULT_WIDTH/DEFAULT_HEIGHT;
	}
	// screen width or height may have changed, so need to update scale
	osint_updatescale();
}

static void osint_maskinfo (int mask, int *shift, int *precision)
{
	*shift = 0;

	while ((mask & 1L) == 0) {
		mask >>= 1;
		(*shift)++;
	}

	*precision = 0;

	while ((mask & 1L) != 0) {
		mask >>= 1;
		(*precision)++;
	}
}

static void osint_gencolors (void)
{
	int c;
	int rcomp, gcomp, bcomp;

	for (c = 0; c < VECTREX_COLORS; c++) {
		rcomp = c * 256 / VECTREX_COLORS;
		gcomp = c * 256 / VECTREX_COLORS;
		bcomp = c * 256 / VECTREX_COLORS;

		color_set[c] = (GLfloat)c/128;
		if(color_set[c] > 1.0f) color_set[c] = 1.0f;
	}
}

static void osint_clearscreen (void)
{
    // Clear color buffer
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClear( GL_COLOR_BUFFER_BIT );
}

/*
    JH - there some were nice low-level line drawing routines here,
         which have been replaced by OpenGL calls
*/

void osint_render (void)
{
	// GL rendering code by James Higgs
	int     width, height;
	long v;
	GLfloat c;
	//GLfloat alpha;

    // Get window size (may be different than the requested size)
	width = screen_x;
	height = screen_y;

    height = height > 0 ? height : 1;

    // Set viewport
    glViewport( 0, 0, width, height );
	glScissor( 0, 0, width, height );

#ifdef ENABLE_OVERLAY
	// draw overlay or clear screen if no overlay is used
	if (g_overlay.width > 0) {
		GLfloat alpha = overlay_transparency;
		glColor3f(alpha, alpha, alpha);
		glEnable(GL_TEXTURE_2D);
		glBegin(GL_QUADS);
			if (g_overlay.upsideDown)
			{
				glTexCoord2f(1, 1); //0.8f, 1);
				glVertex2f(ALG_MAX_X, 0);
				glTexCoord2f(0, 1); //0.2f, 1);
				glVertex2f(0, 0);
				glTexCoord2f(0, 0); //0.2f, 0);
				glVertex2f(0, ALG_MAX_Y);
				glTexCoord2f(1, 0); //0.8f, 0);
				glVertex2f(ALG_MAX_X, ALG_MAX_Y);
			}
			else
			{
				glTexCoord2f(1, 0); //0.8f, 1);
				glVertex2f(ALG_MAX_X, 0);
				glTexCoord2f(0, 0); //0.2f, 1);
				glVertex2f(0, 0);
				glTexCoord2f(0, 1); //0.2f, 0);
				glVertex2f(0, ALG_MAX_Y);
				glTexCoord2f(1, 1); //0.8f, 0);
				glVertex2f(ALG_MAX_X, ALG_MAX_Y);
			}
		glEnd();
		glDisable(GL_TEXTURE_2D);
	} else {
	    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear(GL_COLOR_BUFFER_BIT);
	}
#else
    // Clear color buffer
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear(GL_COLOR_BUFFER_BIT);
#endif

    // Select and setup the projection matrix
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
	glOrtho( 0, -33000, 41000, 0, 1.0, 50.0 );

    // Select and setup the modelview matrix
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    gluLookAt( 0.0f, 0.0f, -10.0f,    // Eye-position
               0.0f, 0.0f, 0.0f,   // View-point
               0.0f, 1.0f, 0.0f );  // Up-vector

	glEnable(GL_LINE_SMOOTH);
	glLineWidth(line_width);
	glEnable(GL_POINT_SMOOTH);
	glPointSize(line_width);

	// blend lines with overlay image
	if (g_overlay.width > 0) {
		glEnable(GL_BLEND);
		//glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
		glBlendFunc(GL_DST_COLOR, GL_ONE);
	}

    glBegin( GL_LINES );

//	// undraw lines from previous frame
//	// (JH - We actually draw the erase vectors to get a blurry effect)
//	for (v = 0; v < vector_erse_cnt; v++) {
//		if (vectors_erse[v].color != VECTREX_COLORS) {
////				osint_line (vectors_erse[v].x0, vectors_erse[v].y0,
////							vectors_erse[v].x1, vectors_erse[v].y1, 0);
//			c = color_set[vectors_draw[v].color];
//			glColor3f( c*0.5f, c*0.5f, c*0.5f );
//			glVertex3i( vectors_erse[v].x0, vectors_erse[v].y0, 0 );
//			glVertex3i( vectors_erse[v].x1, vectors_erse[v].y1, 0 );
//		}
//	}

	// draw lines for this frame
	for (v = 0; v < vector_draw_cnt; v++) {
//		osint_line (vectors_draw[v].x0, vectors_draw[v].y0,
//					vectors_draw[v].x1, vectors_draw[v].y1,
//					vectors_draw[v].color);
		c = color_set[vectors_draw[v].color];
		glColor4f( c, c, c, 0.75f );
		glVertex3i( vectors_draw[v].x0, vectors_draw[v].y0, 0 );
		glVertex3i( vectors_draw[v].x1, vectors_draw[v].y1, 0 );

	}

// DEBUG sound output visualisation
	if(AY_debug) {
		glVertex3i( 1000, 20000, 0 );
		glVertex3i( 1000 + (snd_regs[8] & 0xFF) * 100, 20000,  0 );

		glVertex3i( 1000, 20500, 0 );
		glVertex3i( 1000 + (snd_regs[9] & 0xFF) * 100, 20500,  0 );

		glVertex3i( 1000, 21000, 0 );
		glVertex3i( 1000 + (snd_regs[10] & 0xFF) * 100, 21000,  0 );

		glVertex3i( 1000, 24000, 0 );
		glVertex3i( 1000 + (snd_regs[7] & 0x01) * 100, 24000,  0 );

		glVertex3i( 1000, 24500, 0 );
		glVertex3i( 1000 + (snd_regs[7] & 0x08) * 100, 24500,  0 );


		glVertex3i( 1000, 28000, 0 );
		glVertex3i( 1000 + (snd_regs[0] + snd_regs[1] * 256), 28000,  0 );

		glVertex3i( 1000, 28500, 0 );
		glVertex3i( 1000 + (snd_regs[6] & 0x1F) * 100, 28500,  0 );


		if(pWave) {
			glColor3f( 0.0f, 0.0f, 1.0f );
			for(v=0; v<300; v++) {
				glVertex3i( v*100, 39500, 0 );
				glVertex3i( v*100, 39500 - 20 * pWave[v],  0 );
			}
		}
	}

	glEnd();

	// we have to redraw points, because zero-length line doesn't get drawn
	glBegin(GL_POINTS);
	for (v = 0; v < vector_draw_cnt; v++) {
//		osint_line (vectors_draw[v].x0, vectors_draw[v].y0,
//					vectors_draw[v].x1, vectors_draw[v].y1,
//					vectors_draw[v].color);
		c = color_set[vectors_draw[v].color];
		glColor3f( c,c,c );
		glVertex3i( vectors_draw[v].x0, vectors_draw[v].y0, 0 );
		glVertex3i( vectors_draw[v].x1, vectors_draw[v].y1, 0 );
	}

	if(AY_debug) {
		glVertex3i( 1000, 20000, 0 );
		glVertex3i( 1000, 20500, 0 );
		glVertex3i( 1000, 21000, 0 );
	}

	glEnd();

	glDisable(GL_BLEND);

    // Swap buffers
    SDL_GL_SwapBuffers( );
}

void osint_emuloop (void)
{
	int frames, running;
    double t, t0, t1, fps;
    char    titlestr[ 200 ];
    SDL_Event event;
	
	// reset the vectrex hardware
	vecx_reset ();

    frames = 0;
	running = 1;
	t0 = SDL_GetTicks();
	t1 = t0;
	while (running) {

	    // Grab all the events off the queue. 
	    while( SDL_PollEvent( &event ) ) {
		   switch( event.type ) {
			case SDL_KEYDOWN:
				// Handle key presses
				switch(event.key.keysym.sym) {
					case SDLK_LEFT :
						alg_jch0 = 0x00;
						break;
					case SDLK_RIGHT :
						alg_jch0 = 0xFF;
						break;
					case SDLK_UP :
						alg_jch1 = 0xFF;
						break;
					case SDLK_DOWN :
						alg_jch1 = 0x00;
						break;
					case SDLK_a :
						snd_regs[14] &= ~0x01;
						break;
					case SDLK_s :
						snd_regs[14] &= ~0x02;
						break;
					case SDLK_d:
						snd_regs[14] &= ~0x04;
						break;
					case SDLK_f :
						snd_regs[14] &= ~0x08;
						break;
					case SDLK_p :					// pause
					case SDLK_SPACE :
						if(1 == running) {
							running = 2;		// 2 = "pause state"
							SDL_PauseAudio(1);
						}
						else {
							running = 1;
							SDL_PauseAudio(0);
						}
						break;
					case SDLK_w :					// toggle sound debug on/off
						if(AY_debug) AY_debug = 0;
						else AY_debug = 1;
						break;
					case SDLK_q :					// quit
					case SDLK_ESCAPE :
						running = 0;
						break;
				} // end switch keydown
				break;
			case SDL_KEYUP:
				// Handle key releases
				switch(event.key.keysym.sym) {
					case SDLK_LEFT :
						alg_jch0 = 0x80;
						break;
					case SDLK_RIGHT :
						alg_jch0 = 0x80;
						break;
					case SDLK_UP :
						alg_jch1 = 0x80;
						break;
					case SDLK_DOWN :
						alg_jch1 = 0x80;
						break;
					case SDLK_a :
						snd_regs[14] |= 0x01;
						break;
					case SDLK_s :
						snd_regs[14] |= 0x02;
						break;
					case SDLK_d:
						snd_regs[14] |= 0x04;
						break;
					case SDLK_f :
						snd_regs[14] |= 0x08;
						break;
				} //end switch keyup
				break;
			case SDL_QUIT:
				/* Handle quit requests (like Ctrl-c). */
				running = 0;
				break;
			} // end outer switch

		} // wend events

        // Calculate and display Window caption info
		if(2 == running) {
			SDL_WM_SetCaption("VecX/SDL/GL (PAUSED)", NULL);
		}
		else {
			// Get time
			t = SDL_GetTicks();
			if(AY_debug) {
				// update AY debug info 10x a second
				if( (t-t1) >= 100)
				{
					fps = (double)frames;
					sprintf(titlestr, "F: %04d %04d %04d  V: %02d %02d %02d  TE: %d %d %d",
							AY_spufreq[0], AY_spufreq[1], AY_spufreq[2],
							AY_vol[0], AY_vol[1], AY_vol[2], 
							AY_tone_enable[0], AY_tone_enable[1], AY_tone_enable[2]);
					SDL_WM_SetCaption(titlestr, NULL);
					t1 = t;
					frames = 0;
				}
			}
			else {
				// update fps display once per second
				if( (t-t1) >= 1000)
				{
					fps = (double)frames;
					sprintf( titlestr, "VecX/SDL/GL (%.1f FPS) Drawn: %d ", 
							fps, vector_draw_cnt );
					SDL_WM_SetCaption(titlestr, NULL);
					t1 = t;
					frames = 0;
				}
			}
	        frames ++;
		}

		// emulate this "frame" (if not paused)
		if(1 == running)
			vecx_emu ((VECTREX_MHZ / 1000) * EMU_TIMER, 0);

		// speed control
		while(SDL_GetTicks() < (t0 + EMU_TIMER)) ;
		t0 = SDL_GetTicks();

	} // wend running

printf("Exit emuloop.\n");
}



// Initialise SDL video buffer
const SDL_VideoInfo* init_sdl()
{
    const SDL_VideoInfo* info = NULL;
    // Color depth in bits of our window
    int bpp = 0;
    // Flags we will pass into SDL_SetVideoMode
    int flags = 0;

    // First, initialize SDL's video subsystem. */
    if( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
        /* Failed, exit. */
        fprintf( stderr, "Video initialization failed: %s\n",
             SDL_GetError( ) );
        exit( 1 );
    }

    /* Let's get some video information. */
    info = SDL_GetVideoInfo( );

    if( !info ) {
        /* This should probably never happen. */
        fprintf( stderr, "Video query failed: %s\n",
             SDL_GetError( ) );
        exit( 1 );
    }

    /*
     * Get bpp of current displpay
     */
    bpp = info->vfmt->BitsPerPixel;

    /*
     * Now, we want to setup our requested
     * window attributes for our OpenGL window.
     * We want *at least* 5 bits of red, green
     * and blue. We also want at least a 16-bit
     * depth buffer.
     *
     * The last thing we do is request a double
     * buffered window. '1' turns on double
     * buffering, '0' turns it off.
     *
     * Note that we do not use SDL_DOUBLEBUF in
     * the flags to SDL_SetVideoMode. That does
     * not affect the GL attribute state, only
     * the standard 2D blitting setup.
     */
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

    /*
     * We want to request that SDL provide us
     * with an OpenGL window, in a fullscreen
     * video mode.
     */
    flags = SDL_OPENGL;

    /*
     * Set the video mode
     */
//    if( SDL_SetVideoMode( width, height, bpp, flags ) == 0 ) {
    if( SDL_SetVideoMode( screen_x, screen_y, bpp, flags ) == 0 ) {
        /* 
         * This could happen for a variety of reasons,
         * including DISPLAY not being set, the specified
         * resolution not being available, etc.
         */
        fprintf( stderr, "Video mode set failed: %s\n", SDL_GetError( ) );
        exit( 1 );
    }

    /*
     * At this point, we should have a properly setup
     * double-buffered window for use with OpenGL.
     */

	return info;
}

#ifdef ENABLE_OVERLAY
// load overlay and set it as current texture
static void load_overlay(char *filename)
{

	if (!LoadTGA(filename))				// Load The Font Texture
	{
		return;										// If Loading Failed, Return False
	}

	//BuildFont();											// Build The Font

	glShadeModel(GL_SMOOTH);								// Enable Smooth Shading
	glClearColor(0.0f, 0.0f, 0.0f, 0.5f);					// Black Background
	glClearDepth(1.0f);										// Depth Buffer Setup
	glBindTexture(GL_TEXTURE_2D, g_overlay.texID);		// Select Our Font Texture
	//glScissor(1	,64,637,288);								// Define Scissor Region
	
	//return TRUE;											// Initialization Went OK
}
#endif

// sound mixer callback
static void fillsoundbuffer(void *userdata, Uint8 *stream, int len) {
    // PS2 AY to SPU conversion:
    // SPU freq = (0x1000 * 213) / divisor

	// AY regs:
	// 0, 1 divisor channel A (12-bit)
	// 2, 3 divisor channel B
	// 4, 5 divisor channel C
	// 6    noise divisor (5-bit)
	// 7    mixer (|x|x|nC|nB|nA|tC|tB|tA)
	// 8	volume A
	// 9   volume B
	// 10   volume C
	static unsigned int noisepos = 0;
    int i;
    int divisor;

	float step[3];
	float noisestep;
	float flip[3];
	Uint8 val[3];
	Uint8 lastval;

    AY_tone_enable[0] = snd_regs[7] & 0x01;
    AY_tone_enable[1] = snd_regs[7] & 0x02;
    AY_tone_enable[2] = snd_regs[7] & 0x04;

    AY_noise_enable[0] = snd_regs[7] & 0x08;
    AY_noise_enable[1] = snd_regs[7] & 0x10;
    AY_noise_enable[2] = snd_regs[7] & 0x20;

    // calc noise freq
    divisor = (snd_regs[6] & 0x1F) << 4;
    if(divisor == 0) divisor = 256;
    
    AY_noisefreq = (440 * 213) / divisor;

    // calc tone freq and vols
    for(i=0; i<3; i++) {
        //divisor = snd_regs[i*2] + snd_regs[i*2+1] * 256;
        divisor = snd_regs[i*2] | (snd_regs[i*2+1] << 8);
        if(divisor == 0) divisor = 4095;

        //AY_spufreq[i] = (440 * 213) / divisor;
		AY_spufreq[i] = divisor;

        AY_vol[i] = (snd_regs[8+i] & 0x0F) << 2; //<< 9;

    }

	step[0] = 441.0f*(float)AY_spufreq[0]/(float)22050;
	step[1] = 441.0f*(float)AY_spufreq[1]/(float)22050;
	step[2] = 441.0f*(float)AY_spufreq[2]/(float)22050;
	//step[0] = 660.0f*(float)AY_spufreq[0]/(float)22050;
	//step[1] = 660.0f*(float)AY_spufreq[1]/(float)22050;
	//step[2] = 660.0f*(float)AY_spufreq[2]/(float)22050;
	noisestep = (441.0f * AY_noisefreq) / (float)22050;
	//noisestep = (660.0f * AY_noisefreq) / (float)22050;
	flip[0] = step[0];
	flip[1] = step[1];
	flip[2] = step[2];
	val[0] = AY_vol[0];
	val[1] = AY_vol[1];
	val[2] = AY_vol[2];

/* JH - REFERENCE CODE FROM VECXPS2
	    // update tone voices
    for(i=0; i<3; i++) {
//        spu_remote(1,spuSetCore,1,0,0,0,0,0);
        voice_att.mask = SPU_VOICE_PITCH | SPU_VOICE_VOL_LEFT | SPU_VOICE_VOL_RIGHT;
        voice_att.voice = SPU_VOICE_X(i);
        voice_att.pitch = spufreq[i];
        voice_att.vol.left = voice_att.vol.right = 0;
        if( !tone_enable[i] ) 
            voice_att.vol.left = voice_att.vol.right = vol[i];
        spu_remote(1,spuSetVoiceAttr,(u32)&voice_att,sizeof(struct spu_voice_attr),0,0,0,0);
    }


    // update noise voices
    for(i=0; i<3; i++) {
        voice_att.mask = SPU_VOICE_PITCH | SPU_VOICE_VOL_LEFT | SPU_VOICE_VOL_RIGHT;
        voice_att.voice = SPU_VOICE_X(i+3);
        voice_att.pitch = noisefreq;
        voice_att.vol.left = voice_att.vol.right = 0;
        if( !noise_enable[i] ) 
            voice_att.vol.left = voice_att.vol.right = vol[i];
        spu_remote(1,spuSetVoiceAttr,(u32)&voice_att,sizeof(struct spu_voice_attr),0,0,0,0);
    }
*/
	// fill buffer
	lastval = 0;
	for(i=0; i<len; i++)
	{
		//stream[i] = usedSpec->silence;
		stream[i] = 0;

		// do tones
		if(!AY_tone_enable[0] && AY_spufreq[0] < 4095)
			stream[i] += val[0];
		if(!AY_tone_enable[1] && AY_spufreq[1] < 4095)
			stream[i] += val[1];
		if(!AY_tone_enable[2] && AY_spufreq[1] < 4095)
			stream[i] += val[2];

		if(i>(int)flip[0]) {
			if(val[0] == AY_vol[0]) val[0] = 0;
			else val[0] = AY_vol[0];
			flip[0] = flip[0] + step[0];
		}
		if(i>(int)flip[1]) {
			if(val[1] == AY_vol[1]) val[1] = 0;
			else val[1] = AY_vol[1];
			flip[1] = flip[1] + step[1];
		}
		if(i>(int)flip[2]) {
			if(val[2] == AY_vol[2]) val[2] = 0;
			else val[2] = AY_vol[2];
			flip[2] = flip[2] + step[2];
		}

		// do noise
		if(!AY_noise_enable[0])
			stream[i] += (AY_vol[0] * wnoise[noisepos] >> 9);
		if(!AY_noise_enable[1])
			stream[i] += (AY_vol[1] * wnoise[noisepos] >> 9);
		if(!AY_noise_enable[2])
			stream[i] += (AY_vol[2] * wnoise[noisepos] >> 9);

		noisepos += (int)noisestep;
		if(noisepos > wnoise_size)
			noisepos -= wnoise_size;

		// average last 2 samples
		stream[i] = (stream[i] + lastval) >> 1;				
		lastval = stream[i];
	}

	pWave = stream;

}

//========================================================================
// main()
//========================================================================

int main(int argc, char *argv[] )
{
	char msg[1024];
	FILE *cartfile;


    /* Information about the current video settings. */
    const SDL_VideoInfo* info = NULL;

	// get defaults and parse command-line params
	if (osint_defaults ()) {
		return 1;
	}

	osint_parse_cmdline (argc, argv);

	cartfile = fopen (cartname, "rb");

	if (cartfile != NULL) {
		fread (cart, 1, sizeof (cart), cartfile);
		fclose (cartfile);
	} else {
		sprintf (msg, "cannot open '%s'", cartname);
		fprintf(stderr, msg);
	}

    // Initialize SDL's video subsystem
	info = init_sdl();
    //setup_opengl( width, height );

	/* determine a set of colors to use based */
	osint_gencolors ();

#ifdef ENABLE_OVERLAY
	// Load overlay if neccessary (TGA 24-bit uncompressed)
	g_overlay.width = 0;
	if (overlayname)
		load_overlay(overlayname);
#endif

	// set up audio buffering
	reqSpec.freq = 22050;						// Audio frequency in samples per second
	reqSpec.format = AUDIO_U8;					// Audio data format
	reqSpec.channels = 1;						// Number of channels: 1 mono, 2 stereo
	reqSpec.samples = 441;						// Audio buffer size in samples
	reqSpec.callback = fillsoundbuffer;			// Callback function for filling the audio buffer
	reqSpec.userdata = NULL;
	usedSpec = &givenSpec;
	/* Open the audio device */
	if ( SDL_OpenAudio(&reqSpec, usedSpec) < 0 ){
	  fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
	  exit(-1);
	}

	if(usedSpec == NULL)
		usedSpec = &reqSpec;

	// Start playing audio
	SDL_PauseAudio(0);

	/* message loop handler and emulator code */

	osint_emuloop ();

    /*
     * Quit SDL so we can release the fullscreen
     * mode and restore the previous video settings,
     * etc.
     */
    SDL_Quit( );

    /* Exit program. */
    exit( 0 );

	return 0;
// END OF MAIN!!!

}
