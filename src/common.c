/* hacktv - Analogue video transmitter for the HackRF                    */
/*=======================================================================*/
/* Copyright 2018 Philip Heron <phil@sanslogic.co.uk>                    */
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <ctype.h>
#include "common.h"

int64_t gcd(int64_t a, int64_t b)
{
	int64_t c;
	
	while((c = a % b))
	{
		a = b;
		b = c;
	}
	
	return(b);
}

static r64_t _normalise(int64_t *num, int64_t *den)
{
	int64_t e;
	
	if(*den == 0)
	{
		*num = 0;
		return((r64_t) { });
	}
	
	if(*den < 0)
	{
		*num = -*num;
		*den = -*den;
	}
	
	e = gcd(*num, *den);
	
	return((r64_t) { *num /= e, *den /= e });
}

r64_t r64_mul(r64_t a, r64_t b)
{
	int64_t c, d;
	c = (int64_t) a.num * b.num;
	d = (int64_t) a.den * b.den;
	return(_normalise(&c, &d));
}

r64_t r64_div(r64_t a, r64_t b)
{
	int64_t c, d;
	c = (int64_t) a.num * b.den;
	d = (int64_t) a.den * b.num;
	return(_normalise(&c, &d));
}

int r64_cmp(r64_t a, r64_t b)
{
	int64_t c = (int64_t) a.num * b.den - (int64_t) a.den * b.num;
	return(c < 0 ? -1 : (c > 0 ? 1 : 0));
}

r64_t r64_nearest(r64_t ref, r64_t a, r64_t b)
{
	/* Return "a" or "b" depending on which is nearest "ref", or "a" if equal */
	r64_t h = { a.num * b.den + a.den * b.num, a.den * b.den * 2 };
	return(r64_cmp(ref, h) <= 0 ? a : b);
}

r64_t r64_parse_decimal(const char *str, const char **endptr)
{
	/* Parse decimal number with exponent */
	const char *s = str;
	int64_t e, num, den = 1;
	
	if(endptr != NULL) *endptr = str;
	
	/* Skip any leading spaces */
	while(isspace(*s)) s++;
	
	/* Test for the sign */
	if(*s == '+' || *s == '-')
	{
		if(*s == '-') den = -1;
		s++;
	}
	
	/* Test for no number */
	if((s[0] != '.' && !isdigit(s[0])) ||
	   (s[0] == '.' && !isdigit(s[1])))
	{
		return((r64_t) { });
	}
	
	/* Read first number/integer part */
	for(num = 0; isdigit(*s); s++)
	{
		num = num * 10 + *s - '0';
	}
	
	/* Read the fractional part */
	if(*s == '.')
	{
		for(s++; isdigit(*s); s++)
		{
			num = num * 10 + *s - '0';
			den *= 10;
		}
		
		(void) _normalise(&num, &den);
	}
	
	/* Read the exponent part */
	if(*s == 'e' || *s == 'E')
	{
		int neg = 0;
		
		s++;
		
		/* Test for the sign */
		if(*s == '+' || *s == '-')
		{
			if(*s == '-') neg = 1;
			s++;
		}
		
		if(!isdigit(*s)) return((r64_t) { });
		
		for(e = 0; isdigit(*s); s++)
		{
			e = e * 10 + *s - '0';
		}
		
		if(neg) { for(; e > 0; e--) den *= 10; }
		else    { for(; e > 0; e--) num *= 10; }
	}
	
	/* Test for empty string or invalid trailing characters */
	if(*s == '.' || *s == '+' || *s == '-' ||
	   *s == 'e' || *s == 'E' || s == str)
	{
		return((r64_t) { });
	}
	
	/* Looks good, return the result */
	if(endptr != NULL) *endptr = s;
	return(_normalise(&num, &den));
}

r64_t r64_parse(const char *str, const char **endptr)
{
	/* Parse decimal number with exponent,
	 * individually or as a ratio pair x:y or x/y */
	const char *s;
	r64_t a, b;
	
	if(endptr != NULL) *endptr = str;
	
	/* Parse the first part */
	a = r64_parse_decimal(str, &s);
	if(a.den == 0) return(a);
	
	if(*s == ':' || *s == '/')
	{
		/* Don't allow spaces after the divider */
		s++;
		if(*s == ' ') return((r64_t) { });
		
		/* Parse the second part */
		b = r64_parse_decimal(s, &str);
		if(b.num == 0 || b.den == 0)
		{
			return((r64_t) { });
		}
		
		/* Test for too many dividers */
		s = str;
		if(*s == ':' || *s == '/')
		{
			return((r64_t) { });
		}
		
		/* Apply divider */
		a = r64_div(a, b);
	}
	
	/* Looks good, return the result */
	if(endptr != NULL) *endptr = s;
	return(a);
}

double r64_double(r64_t a)
{
	/* Convert the rational value a to a double */
	if(a.den == 0)
	{
		if(a.num > 0) return(INFINITY);
		if(a.num < 0) return(-INFINITY);
		return(NAN);
	}
	
	return((double) a.num / a.den);
}

/* r64_would_round(): Return !0 if the rational value would be
 *                    rounded with specified precision.
 * 
 *         a: The rational value to be tested.
 * precision: Number of decimal places to test.
 * 
 * Returns 0 if no rounding required.
*/

int r64_would_round(r64_t a, int precision)
{
	int i;
	
	/* Ensure the denominator is positive */
	if(a.den < 0) a = (r64_t) { -a.num, -a.den };
	
	/* Handle not-a-number or infinity */
	if(a.den == 0) return(0);
	
	a.num = (a.num < 0 ? -a.num : a.num) % a.den;
	
	for(i = 0; i < precision && a.num > 0; i++)
	{
		a.num = (a.num * 10) % a.den;
	}
	
	return(a.num != 0);
}

/* r64_snprint(): Convert r64_t rational value to a string.
 * 
 *       str: Pointer to memory where the string will be written.
 *      size: Maximum number of characters to write, including
 *            the terminating null byte '\0'.
 *         a: The rational value to be converted.
 * precision: Number of decimal places to output. A precision < 0
 *            indicates no limit (up to 'size' characters) and
 *            no rounding.
 *       pad: Zero pad the output up to 'precision' characters.
 * 
 * Returns the value passed to 'str'.
*/

char *r64_snprint(char *str, size_t size, r64_t a, int precision, int pad)
{
	char w[22];
	uint64_t n;
	int i, r;
	int ni;
	char c;
	int sign = 1;
	
	/* Sanity check arguments */
	if(str == NULL || size <= 0) return(str);
	
	/* Ensure the denominator is positive */
	if(a.den < 0) a = (r64_t) { -a.num, -a.den };
	
	/* Handle not-a-number or infinity */
	if(a.den == 0)
	{
		if(a.num) snprintf(str, size, "%sinf", a.num < 0 ? "-" : "");
		else snprintf(str, size, "nan");
		return(str);
	}
	
	/* Separate out the sign, integer and fractional parts */
	sign = a.num < 0 ? -1 : 1;
	n = a.num / a.den * sign;
	a.num = (a.num * sign) % a.den;
	
	/* Print the fractional part */
	if(precision < 0) precision = INT_MAX;
	
	for(ni = -1, i = 0; i < precision && (pad == 1 || a.num > 0); i++)
	{
		a.num *= 10;
		c = '0' + a.num / a.den;
		a.num %= a.den;
		
		if(c != '9') ni = i;
		
		if(i < size) str[i] = c;
		else if(precision == INT_MAX) break;
	}
	
	/* Rounding */
	if(a.num > 0 && precision != INT_MAX)
	{
		if(a.num * 10 / a.den >= 5)
		{
			if(ni < 0) n++;
			else str[ni]++;
			
			for(r = ni + 1; r < i && r < size; r++)
			{
				str[r] = '0';
			}
		}
	}
	
	/* Print the integer part, inc. the sign and decimal point */
	r = 0;
	
	if(i > 0) w[r++] = '.';
	
	while(r < sizeof(w) - 1)
	{
		w[r++] = '0' + (n % 10);
		n /= 10;
		if(n == 0) break;
	}
	
	if(sign == -1) w[r++] = '-';
	
	/* Combine the integer (reversed) and fractional parts */
	/* r == length of integer part, i == length of fractional part */
	
	for(ni = i + r - 1; ni >= 0; ni--)
	{
		if(ni < size)
		{
			if(ni >= r) str[ni] = str[ni - r];
			else str[ni] = w[r - 1 - ni];
		}
	}
	
	/* Always null terminate */
	i += r;
	i = (i < size ? i : size - 1);
	
	str[i] = '\0';
	
	return(str);
}

cint16_t *sin_cint16(unsigned int length, unsigned int cycles, double level)
{
	cint16_t *lut;
	unsigned int i;
	double d;
	
	lut = malloc(length * sizeof(cint16_t));
	if(!lut)
	{
		return(NULL);
	}
	
	d = 2.0 * M_PI / length * cycles;
	for(i = 0; i < length; i++)
	{
		lut[i].i = round(cos(d * i) * level * INT16_MAX);
		lut[i].q = round(sin(d * i) * level * INT16_MAX);
	}
	
	return(lut);
}

double rc_window(double t, double left, double width, double rise)
{
	double r;
	
	t -= left + width / 2;
	t = fabs(t) - (width - rise) / 2;
	
	if(t <= 0)
	{
		r = 1.0;
	}
	else if(t < rise)
	{
		/* Raised cosine edge */
		//r = 0.5 + cos(t / rise * M_PI) / 2;
		
		/* Integrated raised cosine edge */
		t = 1.0 - t / rise * 2;
		r = 0.5 * (1.0 + t + sin(M_PI * t) / M_PI);
	}
	else
	{
		r = 0.0;
	}
	
	return(r);
}

double rrc(double x, double b, double t)
{
	double r;
	
	/* Based on the Wikipedia page, https://en.wikipedia.org/w/index.php?title=Root-raised-cosine_filter&oldid=787851747 */
	
	if(x == 0)
	{
		r = (1.0 / t) * (1.0 + b * (4.0 / M_PI - 1));
	}
	else if(fabs(x) == t / (4.0 * b))
	{
		r = b / (t * sqrt(2.0)) * ((1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * b)) + (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * b)));
	}
	else
	{
		double t1 = (4.0 * b * (x / t));
		double t2 = (sin(M_PI * (x / t) * (1.0 - b)) + 4.0 * b * (x / t) * cos(M_PI * (x / t) * (1.0 + b)));
		double t3 = (M_PI * (x / t) * (1.0 - t1 * t1));
		
		r = (1.0 / t) * (t2 / t3);
	}
	
	return(r);
}

#if !defined(_POSIX_BARRIERS) || _POSIX_BARRIERS <= 0

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
	pthread_cond_destroy(&barrier->cond);
	pthread_mutex_destroy(&barrier->mutex);
	return(0);
}

int pthread_barrier_init(pthread_barrier_t *restrict barrier, void *attr, unsigned count)
{
	int r;
	
	r = pthread_mutex_init(&barrier->mutex, NULL);
	if(r) return(r);
	
	r = pthread_cond_init(&barrier->cond, NULL);
	if(r) return(r);
	
	barrier->count = count;
	barrier->pending = count;
	barrier->cycle = 0;
	
	return(0);
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
	int r, c;
	
	r = pthread_mutex_lock(&barrier->mutex);
	if(r) return(r);
	
	if(--barrier->pending == 0)
	{
		/* We're the last thread to reach the barrier,
		 * reset for the next cycle and send signal */
		barrier->pending = barrier->count;
		barrier->cycle++;
		
		r = pthread_cond_broadcast(&barrier->cond);
		if(r) return(r);
		
		r = pthread_mutex_unlock(&barrier->mutex);
		if(r) return(r);
		
		return(PTHREAD_BARRIER_SERIAL_THREAD);
	}
	
	/* Wait here until a new cycle is signalled */
	
	c = barrier->cycle;
	
	do
	{
		r = pthread_cond_wait(&barrier->cond, &barrier->mutex);
		if(r) return(r);
	}
	while(barrier->cycle == c);
	
	r = pthread_mutex_unlock(&barrier->mutex);
	
	return(r);
}

#endif

