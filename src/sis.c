/* hacktv - Analogue video transmitter for the HackRF                    */
/*=======================================================================*/
/* Copyright 2024 Philip Heron <phil@sanslogic.co.uk>                    */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* -=== SiS (Sound-in-Syncs) encoder ===- */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "video.h"
#include "vbidata.h"

static double _raised_cosine(double x)
{
	if(x <= -1 || x >= 1) return(0);
	return((1.0 + cos(M_PI * x)) / 2);
}

static int __init_quits(vbidata_lut_t *lut, unsigned int nsymbols, unsigned int dwidth, int level, double bwidth, double offset)
{
	int l;
	int b, x;
	int levels[2];
	vbidata_lut_t lc;
	vbidata_lut_t *lptr = (lut ? lut : &lc);
	
	nsymbols *= 2;
	
	levels[0] = level / 2 / 0.75;
	levels[1] = level / 4 / 0.75;
	
	l = 0;
	
	for(b = 0; b < nsymbols; b++)
	{
		double t = -bwidth * (b / 2) - offset;
		
		lptr->offset = lptr->length = 0;
		
		for(x = 0; x < dwidth; x++)
		{
			double h = _raised_cosine((t + x) / bwidth) * levels[b & 1];
			vbidata_update(lptr, lut ? 1 : 0, x, round(h));
		}
		
		l += 2 + lptr->length;
		
		if(lut)
		{
			lptr = (vbidata_lut_t *) &lptr->value[lptr->length];
		}
	}
	
	/* End of LUT marker */
	if(lut)
	{
		lptr->length = -1;
	}
	
	l++;
	
	return(l * sizeof(int16_t));
}

vbidata_lut_t *_init_quits(unsigned int nsymbols, unsigned int dwidth, int level, double bwidth, double offset)
{
	int l;
	vbidata_lut_t *lut;
	
	/* Calculate the length of the lookup-table and allocate memory */
	l = __init_quits(NULL, nsymbols, dwidth, level, bwidth, offset);
	
	lut = malloc(l);
	if(!lut)
	{
		return(NULL);
	}
	
	/* Generate the lookup-table and return */
	__init_quits(lut, nsymbols, dwidth, level, bwidth, offset);
	
	return(lut);
}

int sis_init(sis_t *s, const char *sismode, vid_t *vid, uint8_t mode, uint8_t reserve)
{
	double left, rise, width = 0;
	int i;
	
	if(strcmp(sismode, "dcsis") == 0)
	{
		s->mode = 0;
		
		/* Render the "quits" - the 4-level symbols */
		s->lut_phases = 1;
		s->lut = malloc(sizeof(vbidata_lut_t *));
		if(!s->lut)
		{
			return(VID_OUT_OF_MEMORY);
		}
		
		s->lut[0] = _init_quits(
			25, vid->width,
			round((vid->white_level - vid->black_level)),
			(double) vid->width / 382,
			(double) vid->width / 382 * 3.32 /* Measured */
		);
		if(!s->lut)
		{
			free(s->lut);
			return(VID_OUT_OF_MEMORY);
		}
		
		s->rt = (r64_t) { 0, 1 };
		
		/* Blanking window parameters - timings measured from captures */
		left = 0.2e-6;
		rise = 80e-9;
		width = 4.56e-6;
		
		/* Init the NICAM encoder */
		nicam_encode_init(&s->nicam, mode, reserve);
	}
	else if(strcmp(sismode, "dsis") == 0)
	{
		r64_t sr, a, o, m, t;
		
		s->mode = 1;
		
		/* Calculate samples per symbol */
		sr = r64_div(vid->pixel_rate, (r64_t) { 6552000, 1 });
		
		/* Calculate symbols per line */
		m = r64_div((r64_t) { 6552000, 1 }, (r64_t) { 25 * 625, 1 });
		
		/* Target offset from 0H, in symbols (TODO: Tune to match encoder) */
		o = (r64_t) { 7, 2 };
		
		/* The DSIS symbol clock is not line locked, each line has a different
		 * phase relative to the vsync edge (0H) from the previous line.
		 * Calculate the number of phases needed...
		 * Spoiler, it's always 125 for normal 25 Hz / 625-line modes, but I'll
		 * leave the calculation here in case it's useful in the future */
		a = r64_div((r64_t) { vid->width, 1 }, sr);
		s->lut_phases = a.den;
		
		s->lut = calloc(sizeof(vbidata_lut_t *), s->lut_phases);
		if(!s->lut)
		{
			return(VID_OUT_OF_MEMORY);
		}
		
		/* Render the "quits" - the 4-level symbols for each phase */
		for(i = 0; i < s->lut_phases; i++)
		{
			/* Calculate offset for this line / phase */
			t = r64_mul((r64_t) { (o.num + o.den / 2) / o.den, 1 }, sr);
			t.num %= vid->width * t.den;
			o = r64_add(o, m);
			
			s->lut[i] = _init_quits(
				25, vid->width,
				round((vid->white_level - vid->black_level)),
				(double) vid->width / (6552000 / 25 / 625),
				r64_double(t)
			);
			if(!s->lut)
			{
				for(i--; i >= 0; i--) free(s->lut[i]);
				free(s->lut);
				return(VID_OUT_OF_MEMORY);
			}
		}
		
		s->rt = (r64_t) { 0, 1 };
		s->sr = 0xFF;
		
		/* Init the NICAM encoder */
		nicam_encode_init(&s->nicam, mode, reserve);
	}
	else
	{
		fprintf(stderr, "Unrecognised SiS mode '%s'.\n", sismode);
		return(VID_ERROR);
	}
	
	if(width > 0)
	{
		/* Render the blank window */
		s->blank_left = floor(r64_double(vid->pixel_rate) * (left - rise / 2));
		s->blank_width = ceil(r64_double(vid->pixel_rate) * (width + rise));
		s->blank_win = malloc(s->blank_width * sizeof(int16_t));
		s->blank_level = vid->sync_level; /* Blank to the sync level */
		if(!s->blank_win)
		{
			for(i = 0; i < s->lut_phases; i++) free(s->lut[i]);
			free(s->lut);
			return(VID_OUT_OF_MEMORY);
		}
		
		for(i = s->blank_left; i < s->blank_left + s->blank_width; i++)
		{
			double t = 1.0 / r64_double(vid->pixel_rate) * i;
			s->blank_win[i - s->blank_left] = round(rc_window(t, left, width, rise) * INT16_MAX);
		}
	}
	
	return(VID_OK);
}

void sis_free(sis_t *s)
{
	int i;
	
	if(!s) return;
	
	free(s->blank_win);
	for(i = 0; i < s->lut_phases; i++) free(s->lut[i]);
	free(s->lut);
	
	memset(s, 0, sizeof(sis_t));
}

int _dcsis_render(vid_t *s, void *arg, int nlines, vid_line_t **lines)
{
	sis_t *sis = arg;
	vid_line_t *l = lines[0];
	const uint8_t gc[2][4] = { { 3, 0, 2, 1 }, { 0, 3, 1, 2 } };
	uint8_t vbi[7];
	int x, nb;
	int16_t audio[NICAM_AUDIO_LEN * 2];
	
	/* Target NICAM bitrate - 46.592 bits/line */
	sis->rt = r64_add(sis->rt, (r64_t) { 5824, 125 });
	
	/* Rate limit by varying the length of the data burst (nb) */
	nb = 50;
	if(sis->rt.num < 0) nb -= 4;
	sis->rt = r64_sub(sis->rt, (r64_t) { nb - 2, 1 });	
	
	memset(vbi, 0, 7);
	vbi[0] = 0xC0;
	
	for(x = 2; x < nb; x += 2, sis->frame_bit += 2)
	{
		uint8_t sym;
		
		if(sis->frame_bit >= NICAM_FRAME_BITS)
		{
			/* Encode the next frame */
			if(sis->audio_out == sis->audio_in) memset(audio, 0, NICAM_AUDIO_LEN * 2 * sizeof(int16_t));
			else memcpy(audio, sis->audio[sis->audio_out++ & 1], NICAM_AUDIO_LEN * 2 * sizeof(int16_t));
			
			nicam_encode_frame(&sis->nicam, sis->frame, audio);
			sis->frame_bit = 0;
		}
		
		/* Read the next NICAM bit pair */
		sym = (sis->frame[sis->frame_bit >> 3] >> (6 - (sis->frame_bit & 0x07))) & 0x03;
		
		/* Apply grey coding */
		sym = gc[x & 4 ? 1 : 0][sym];
		
		/* Push it into the data burst */
		vbi[x >> 3] |= sym << (6 - (x & 0x07));
	}
	
	/* Blank the data area */
	for(x = sis->blank_left; x < sis->blank_left + sis->blank_width; x++)
	{
		l->output[x * 2] = (l->output[x * 2] * (INT16_MAX - sis->blank_win[x - sis->blank_left])
		                 + sis->blank_level * sis->blank_win[x - sis->blank_left]) >> 15;
	}
	
	vbidata_render(sis->lut[sis->lut_phase++], vbi, 50 - nb, nb, VBIDATA_MSB_FIRST, l);
	if(sis->lut_phase == sis->lut_phases) sis->lut_phase = 0;
	
	return(1);
}

int _dsis_render(vid_t *s, void *arg, int nlines, vid_line_t **lines)
{
	sis_t *sis = arg;
	vid_line_t *l = lines[0];
	const uint8_t gc[4] = { 2, 1, 0, 3 };
	uint8_t vbi[7];
	int x, nb;
	int16_t audio[NICAM_AUDIO_LEN * 2];
	
	/* Target NICAM bitrate - 46.592 bits/line */
	sis->rt = r64_add(sis->rt, (r64_t) { 5824, 125 });
	
	/* The phase is updated every line */
	if(++sis->lut_phase == sis->lut_phases) sis->lut_phase = 0;
	
	if(l->line ==   5 || l->line == 313 ||
	   l->line == 318 || l->line == 625)
	{
		/* These lines carry various service signals:
		 *   5: Service Channel C
		 * 313: Service Channel B
		 * 318: Service Channel A
		 * 625: AIS (Alarm Indicating Signal)
		 * 
		 * "When the service bit is high, a word of code 11 will be inserted.
		 * On the other hand when the bit is low, a word of code 00 will be inserted."
		 * 
		 * Captures from an encoder show 3-4 symbols set to 11.
		 * 
		 */
		
		//vbi[0] = 0xFC;
		//vbidata_render(sis->lut[sis->lut_phase], vbi, 0, 8, VBIDATA_MSB_FIRST, l);
		
		return(1);
	}
	
	if((l->line >=   4 && l->line <=   5) ||
	   (l->line >= 311 && l->line <= 313) ||
	   (l->line >= 316 && l->line <= 318) ||
	   (l->line >= 624 && l->line <= 625))
	{
		/* Lines that begin with a short (equalisation) pulse do not get SIS data */
		return(1);
	}
	
	/* Rate limit by varying the length of the
	 * data burst (nb) on every 8th line */
	nb = 50;
	if((l->line % 8) == 2 && sis->rt.num < 0) nb -= 8;
	sis->rt = r64_sub(sis->rt, (r64_t) { nb - 2, 1 });
	
	/* NOTE: The hardware encoder sometimes inserts a low level
	 * (1 / 01) pulse at the start of short (20 symbol) lines before
	 * the full 3 / 11 pulse. I'm unsure if it's intentional, required
	 * and/or a bug in the encoder. They're not added here. */
	
	memset(vbi, 0, 7);
	vbi[0] = 0xC0;
	
	for(x = 2; x < nb; x += 2, sis->frame_bit += 2)
	{
		uint8_t sym;
		
		if(sis->frame_bit >= NICAM_FRAME_BITS)
		{
			/* Encode the next frame */
			if(sis->audio_out == sis->audio_in) memset(audio, 0, NICAM_AUDIO_LEN * 2 * sizeof(int16_t));
			else memcpy(audio, sis->audio[sis->audio_out++ & 1], NICAM_AUDIO_LEN * 2 * sizeof(int16_t));
			
			nicam_encode_frame(&sis->nicam, sis->frame, audio);
			sis->frame_bit = 0;
		}
		
		/* Read the next NICAM bit pair */
		sym = (sis->frame[sis->frame_bit >> 3] >> (6 - (sis->frame_bit & 0x07))) & 0x03;
		
		/* Apply and update the PRNG */
		sym ^= ((sis->sr >> 0) & 1) << 0;
		sym ^= ((sis->sr >> 3) & 1) << 1;
		
		sis->sr = (sis->sr >> 1) | ((((sis->sr >> 1) ^ (sis->sr >> 2)) & 1) << 7);
		if(x == 2 && l->line == 1) sis->sr = 0xFF;
		
		/* Apply grey coding */
		sym = gc[sym];
		
		/* Push it into the data burst */
		vbi[x >> 3] |= sym << (6 - (x & 0x07));
	}
	
	vbidata_render(sis->lut[sis->lut_phase], vbi, 50 - nb, nb, VBIDATA_MSB_FIRST, l);
	
	return(1);
}

int sis_render(vid_t *s, void *arg, int nlines, vid_line_t **lines)
{
	sis_t *sis = arg;
	
	switch(sis->mode)
	{
	case 0: return(_dcsis_render(s, arg, nlines, lines));
	case 1: return(_dsis_render(s, arg, nlines, lines));
	}
	
	return(1);
}

int sis_write_audio(sis_t *s, const int16_t *audio)
{
	memcpy(s->audio[s->audio_in++ & 1], audio, sizeof(int16_t) * NICAM_AUDIO_LEN * 2);
	return(VID_OK);
}

